#include "deskmate_mqtt.h"
#include "deskmate_console.h"

#include <arpa/inet.h>
#include <errno.h>
#include <float.h>
#include <fcntl.h>
#include <netdb.h>
#include <nuttx/clock.h>
#include <netutils/cJSON.h>
#include <mqtt.h>
#include <poll.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define DESKMATE_MQTT_BUFFER_SIZE 4608
#define DESKMATE_MQTT_TOPIC_MAX 128

struct deskmate_mqtt_s {
  struct mqtt_client mqtt;
  struct deskmate_controller_s *controller;
  struct deskmate_transport_s transport;
  char host[DESKMATE_MQTT_HOST_MAX + 1];
  char port[DESKMATE_MQTT_PORT_MAX + 1];
  char username[DESKMATE_MQTT_USER_MAX + 1];
  char password[DESKMATE_MQTT_PASSWORD_MAX + 1];
  char device_id[DESKMATE_MQTT_DEVICE_ID_MAX + 1];
  char to_bridge[DESKMATE_MQTT_TOPIC_MAX];
  char to_device[DESKMATE_MQTT_TOPIC_MAX];
  char json[4097];
  uint8_t sendbuf[DESKMATE_MQTT_BUFFER_SIZE] __attribute__((aligned(sizeof(uintptr_t))));
  uint8_t recvbuf[DESKMATE_MQTT_BUFFER_SIZE];
};

static struct deskmate_mqtt_s g_transport_storage;
static struct deskmate_mqtt_s *g_transport;

static uint32_t now_ms(void)
{
  return clock_systime_ticks() / TICK_PER_MSEC;
}

static bool valid_string(const cJSON *item)
{
  return cJSON_IsString(item) && item->valuestring != NULL;
}

static const cJSON *field(const cJSON *root, const char *name)
{
  return cJSON_GetObjectItemCaseSensitive(root, name);
}

/* cJSON decodes JSON \u0000 inside a C string. Reject its only legal wire
 * representation before parsing so strcmp cannot turn an extended nonce or
 * epoch into a prefix match. */
static bool contains_escaped_nul(const char *payload, size_t length)
{
  size_t index;

  for (index = 0; index < length; index++) {
    if (payload[index] != '\\' || index + 1 >= length) {
      continue;
    }
    if (payload[index + 1] == '\\') {
      index++;
      continue;
    }
    if (payload[index + 1] == 'u' && index + 5 < length) {
      if (payload[index + 2] == '0' && payload[index + 3] == '0' &&
          payload[index + 4] == '0' && payload[index + 5] == '0') {
        return true;
      }
      index += 5;
    } else {
      index++;
    }
  }

  return false;
}

static bool has_chat_turns_capability(const cJSON *capabilities)
{
  const cJSON *entry;

  if (capabilities == NULL) {
    return false;
  }
  if (!cJSON_IsArray(capabilities)) {
    return false;
  }

  cJSON_ArrayForEach(entry, capabilities) {
    if (!valid_string(entry) || strlen(entry->valuestring) > 32) {
      return false;
    }
    if (strcmp(entry->valuestring, "chat_turns") == 0) {
      return true;
    }
  }

  return false;
}

static bool copy_config(const struct deskmate_mqtt_config_s *config,
                        struct deskmate_mqtt_s *transport)
{
  if (strlen(config->host) > DESKMATE_MQTT_HOST_MAX ||
      strlen(config->port) > DESKMATE_MQTT_PORT_MAX ||
      strlen(config->username) > DESKMATE_MQTT_USER_MAX ||
      strlen(config->password) > DESKMATE_MQTT_PASSWORD_MAX ||
      strlen(config->device_id) > DESKMATE_MQTT_DEVICE_ID_MAX) {
    return false;
  }

  strcpy(transport->host, config->host);
  strcpy(transport->port, config->port);
  strcpy(transport->username, config->username);
  strcpy(transport->password, config->password);
  strcpy(transport->device_id, config->device_id);
  return true;
}

static bool parse_prompt(const cJSON *value, struct deskmate_prompt_s *prompt)
{
  const cJSON *id;
  const cJSON *tool;
  const cJSON *hint;
  const cJSON *can_once;
  const cJSON *can_deny;

  if (cJSON_IsNull(value)) {
    return true;
  }

  if (!cJSON_IsObject(value)) {
    return false;
  }

  id = field(value, "id");
  tool = field(value, "tool");
  hint = field(value, "hint");
  can_once = field(value, "canOnce");
  can_deny = field(value, "canDeny");
  if (!valid_string(id) || !valid_string(tool) || !valid_string(hint) ||
      !cJSON_IsBool(can_once) || !cJSON_IsBool(can_deny) ||
      strlen(id->valuestring) > DESKMATE_PROMPT_ID_MAX ||
      strlen(tool->valuestring) > DESKMATE_TOOL_MAX ||
      strlen(hint->valuestring) > DESKMATE_HINT_MAX) {
    return false;
  }

  memset(prompt, 0, sizeof(*prompt));
  strcpy(prompt->id, id->valuestring);
  strcpy(prompt->tool, tool->valuestring);
  strcpy(prompt->hint, hint->valuestring);
  prompt->can_once = cJSON_IsTrue(can_once);
  prompt->can_deny = cJSON_IsTrue(can_deny);
  return true;
}

static bool parse_uint64(const cJSON *value, uint64_t *out)
{
  /* JSON numbers pass through cJSON as doubles. Keep within JavaScript's
   * exact-integer range because the Node bridge cannot faithfully emit larger
   * integer token counts either. */
  const double maximum = 9007199254740991.0;
  uint64_t converted;

  if (!cJSON_IsNumber(value) || value->valuedouble != value->valuedouble ||
      value->valuedouble < 0 ||
      value->valuedouble > maximum) {
    return false;
  }

  converted = (uint64_t)value->valuedouble;
  if (value->valuedouble != (double)converted) {
    return false;
  }

  *out = converted;
  return true;
}

static bool parse_uint32(const cJSON *value, uint32_t *out)
{
  const double maximum = 4294967295.0;
  uint32_t converted;

  if (!cJSON_IsNumber(value) || value->valuedouble != value->valuedouble ||
      value->valuedouble < 0 || value->valuedouble > maximum) {
    return false;
  }

  converted = (uint32_t)value->valuedouble;
  if (value->valuedouble != (double)converted) {
    return false;
  }

  *out = converted;
  return true;
}

static bool parse_nonnegative_number(const cJSON *value, double *out)
{
  if (!cJSON_IsNumber(value) || value->valuedouble < 0 ||
      value->valuedouble != value->valuedouble || value->valuedouble > DBL_MAX) {
    return false;
  }

  *out = value->valuedouble;
  return true;
}

static bool valid_currency(const char *value)
{
  return value != NULL && strlen(value) == 3 &&
         value[0] >= 'A' && value[0] <= 'Z' &&
         value[1] >= 'A' && value[1] <= 'Z' &&
         value[2] >= 'A' && value[2] <= 'Z';
}

static bool parse_charge(const cJSON *value,
                         struct deskmate_usage_snapshot_s *snapshot)
{
  const cJSON *kind;
  const cJSON *amount;
  const cJSON *unit;

  if (cJSON_IsNull(value)) {
    snapshot->has_charge = false;
    return true;
  }

  if (!cJSON_IsObject(value)) {
    return false;
  }

  kind = field(value, "kind");
  amount = field(value, "amount");
  if (!valid_string(kind) || !parse_nonnegative_number(amount,
                                                        &snapshot->charge_amount)) {
    return false;
  }

  if (strcmp(kind->valuestring, "credit") == 0) {
    snapshot->charge_kind = DESKMATE_CHARGE_CREDIT;
    unit = field(value, "unit");
  } else if (strcmp(kind->valuestring, "currency") == 0) {
    snapshot->charge_kind = DESKMATE_CHARGE_CURRENCY;
    unit = field(value, "currency");
  } else {
    return false;
  }

  if (!valid_string(unit) || unit->valuestring[0] == '\0' ||
      strlen(unit->valuestring) > DESKMATE_BILLING_UNIT_MAX ||
      (snapshot->charge_kind == DESKMATE_CHARGE_CREDIT &&
       strcmp(unit->valuestring, "credits") != 0) ||
      (snapshot->charge_kind == DESKMATE_CHARGE_CURRENCY &&
       !valid_currency(unit->valuestring))) {
    return false;
  }

  snapshot->has_charge = true;
  snapshot->charge_unit = unit->valuestring;
  return true;
}

static bool parse_tokens(const cJSON *value,
                         struct deskmate_usage_snapshot_s *snapshot)
{
  const cJSON *input;
  const cJSON *output;
  const cJSON *total;

  if (cJSON_IsNull(value)) {
    snapshot->has_tokens = false;
    return true;
  }

  if (!cJSON_IsObject(value)) {
    return false;
  }

  input = field(value, "input");
  output = field(value, "output");
  total = field(value, "total");
  if (!parse_uint64(input, &snapshot->input_tokens) ||
      !parse_uint64(output, &snapshot->output_tokens) ||
      !parse_uint64(total, &snapshot->total_tokens)) {
    return false;
  }

  snapshot->has_tokens = true;
  return true;
}

static bool parse_context(const cJSON *value,
                          struct deskmate_usage_snapshot_s *snapshot)
{
  const cJSON *unit;
  const cJSON *used;
  const cJSON *limit;

  if (cJSON_IsNull(value)) {
    snapshot->has_context = false;
    return true;
  }

  if (!cJSON_IsObject(value)) {
    return false;
  }

  unit = field(value, "unit");
  used = field(value, "used");
  limit = field(value, "limit");
  if (!valid_string(unit) ||
      !parse_nonnegative_number(used, &snapshot->context_used) ||
      !parse_nonnegative_number(limit, &snapshot->context_limit) ||
      snapshot->context_limit <= 0) {
    return false;
  }

  if (strcmp(unit->valuestring, "token") == 0) {
    snapshot->context_unit = DESKMATE_CONTEXT_TOKEN;
  } else if (strcmp(unit->valuestring, "percent") == 0) {
    snapshot->context_unit = DESKMATE_CONTEXT_PERCENT;
  } else {
    return false;
  }

  snapshot->has_context = true;
  return true;
}

static void receive_usage_snapshot(struct deskmate_mqtt_s *transport,
                                   const cJSON *root)
{
  const cJSON *nonce = field(root, "connection_nonce");
  const cJSON *epoch = field(root, "epoch");
  const cJSON *sequence = field(root, "usage_seq");
  const cJSON *usage = field(root, "usage");
  const cJSON *scope;
  const cJSON *billing;
  const cJSON *charge;
  const cJSON *tokens;
  const cJSON *context;
  struct deskmate_usage_snapshot_s snapshot = { 0 };
  uint64_t sequence_value;
  bool display_changed;
  char text[96];

  if (!valid_string(nonce) || !valid_string(epoch) ||
      !parse_uint64(sequence, &sequence_value) || sequence_value == 0 ||
      !cJSON_IsObject(usage)) {
    return;
  }

  scope = field(usage, "scope");
  billing = field(usage, "billing");
  context = field(usage, "context");
  if (!valid_string(scope) || strcmp(scope->valuestring, "bridge_session") != 0 ||
      !cJSON_IsObject(billing) || context == NULL) {
    return;
  }

  charge = field(billing, "charge");
  tokens = field(billing, "tokens");
  if (charge == NULL || tokens == NULL ||
      !parse_charge(charge, &snapshot) || !parse_tokens(tokens, &snapshot) ||
      !parse_context(context, &snapshot)) {
    return;
  }

  snapshot.connection_nonce = nonce->valuestring;
  snapshot.epoch = epoch->valuestring;
  snapshot.seq = sequence_value;
  if (!deskmate_controller_usage_snapshot(transport->controller, &snapshot,
                                          &display_changed) ||
      !display_changed ||
      !deskmate_client_billing_text(&transport->controller->client, text,
                                    sizeof(text))) {
    return;
  }

  printf("DeskMate: %s\n", text);
  deskmate_console_prompt();
}

static void receive_snapshot(struct deskmate_mqtt_s *transport,
                             const char *payload, size_t length)
{
  cJSON *root;
  const char *end = NULL;
  const cJSON *version;
  const cJSON *type;
  const cJSON *nonce;
  const cJSON *epoch;
  const cJSON *seq;
  const cJSON *running;
  const cJSON *message;
  const cJSON *prompt;
  struct deskmate_snapshot_s snapshot;
  struct deskmate_prompt_s prompt_value;
  uint32_t sequence;
  uint32_t running_value;
  bool display_changed;

  if (length > 4096) {
    return;
  }

  memcpy(transport->json, payload, length);
  transport->json[length] = '\0';
  root = cJSON_ParseWithLengthOpts(transport->json, length + 1, &end, 1);
  if (root == NULL || end != transport->json + length || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return;
  }

  version = field(root, "v");
  type = field(root, "type");
  nonce = field(root, "connection_nonce");
  epoch = field(root, "epoch");
  seq = field(root, "seq");
  running = field(root, "running");
  message = field(root, "msg");
  prompt = field(root, "prompt");
  if (!cJSON_IsNumber(version) || version->valuedouble != 1 ||
      !valid_string(type) || strcmp(type->valuestring, "snapshot") != 0 ||
      !valid_string(nonce) || !valid_string(epoch) ||
      strlen(nonce->valuestring) > DESKMATE_NONCE_MAX ||
      strlen(epoch->valuestring) > DESKMATE_EPOCH_MAX ||
      !parse_uint32(seq, &sequence) || sequence == 0 ||
      !parse_uint32(running, &running_value) || running_value > UINT8_MAX ||
      !valid_string(message) || strlen(message->valuestring) > DESKMATE_SUMMARY_MAX ||
      prompt == NULL || !parse_prompt(prompt, &prompt_value)) {
    cJSON_Delete(root);
    return;
  }

  snapshot.connection_nonce = nonce->valuestring;
  snapshot.epoch = epoch->valuestring;
  snapshot.seq = sequence;
  snapshot.running = (uint8_t)running_value;
  snapshot.summary = message->valuestring;
  snapshot.prompt = cJSON_IsNull(prompt) ? NULL : &prompt_value;
  if (deskmate_controller_snapshot(transport->controller, &snapshot, now_ms(),
                                   &display_changed) && display_changed) {
    printf("DeskMate: %s: %s\n",
           deskmate_state_name(transport->controller->client.state),
           transport->controller->client.summary);
    deskmate_console_prompt();
  }

  cJSON_Delete(root);
}

static void receive_tool_result(struct deskmate_mqtt_s *transport,
                                const cJSON *root)
{
  const cJSON *nonce = field(root, "connection_nonce");
  const cJSON *epoch = field(root, "epoch");
  const cJSON *tool_call_id = field(root, "toolCallId");
  const cJSON *status = field(root, "status");

  if (!valid_string(nonce) || !valid_string(epoch) || !valid_string(tool_call_id) ||
      !valid_string(status) || strlen(nonce->valuestring) > DESKMATE_NONCE_MAX ||
      strlen(epoch->valuestring) > DESKMATE_EPOCH_MAX ||
      strlen(tool_call_id->valuestring) > DESKMATE_PROMPT_ID_MAX ||
      strcmp(nonce->valuestring,
                                      transport->controller->client.nonce) != 0 ||
      strcmp(epoch->valuestring, transport->controller->client.epoch) != 0 ||
      !deskmate_controller_tool_result(transport->controller,
                                       tool_call_id->valuestring,
                                       status->valuestring)) {
    return;
  }

  printf("DeskMate: Result: %s\n", transport->controller->client.summary);
  deskmate_console_prompt();
}

static void receive_hello_ack(struct deskmate_mqtt_s *transport,
                              const cJSON *root)
{
  const cJSON *nonce = field(root, "connection_nonce");
  const cJSON *epoch = field(root, "epoch");
  const cJSON *capabilities = field(root, "capabilities");
  struct deskmate_hello_ack_s ack;

  if (!valid_string(nonce) || !valid_string(epoch) ||
      strlen(nonce->valuestring) > DESKMATE_NONCE_MAX ||
      strlen(epoch->valuestring) > DESKMATE_EPOCH_MAX ||
      (capabilities != NULL && !cJSON_IsArray(capabilities))) {
    return;
  }

  ack.connection_nonce = nonce->valuestring;
  ack.epoch = epoch->valuestring;
  ack.chat_turns = has_chat_turns_capability(capabilities);
  deskmate_controller_hello_ack(transport->controller, &ack);
}

static void receive_prompt_ack(struct deskmate_mqtt_s *transport,
                               const cJSON *root)
{
  const cJSON *nonce = field(root, "connection_nonce");
  const cJSON *epoch = field(root, "epoch");
  const cJSON *request_id = field(root, "request_id");
  const cJSON *turn_id = field(root, "turn_id");
  const cJSON *accepted = field(root, "accepted");
  const cJSON *error = field(root, "error");
  struct deskmate_prompt_ack_s ack;

  if (!valid_string(nonce) || !valid_string(epoch) || !valid_string(request_id) ||
      !cJSON_IsBool(accepted) || strlen(request_id->valuestring) > DESKMATE_REQUEST_ID_MAX ||
      strlen(nonce->valuestring) > DESKMATE_NONCE_MAX ||
      strlen(epoch->valuestring) > DESKMATE_EPOCH_MAX ||
      (cJSON_IsTrue(accepted) &&
       (!valid_string(turn_id) || strlen(turn_id->valuestring) > DESKMATE_TURN_ID_MAX)) ||
      (!cJSON_IsTrue(accepted) && error != NULL &&
       (!valid_string(error) || strlen(error->valuestring) > 32))) {
    return;
  }

  ack.connection_nonce = nonce->valuestring;
  ack.epoch = epoch->valuestring;
  ack.request_id = request_id->valuestring;
  ack.turn_id = cJSON_IsTrue(accepted) ? turn_id->valuestring : NULL;
  ack.accepted = cJSON_IsTrue(accepted);
  if (!deskmate_controller_prompt_ack(transport->controller, &ack)) {
    return;
  }

  if (!ack.accepted) {
    printf("DeskMate: prompt rejected%s%s\n", error == NULL ? "" : ": ",
           error == NULL ? "" : error->valuestring);
    deskmate_console_prompt();
  }
}

static void receive_agent_output(struct deskmate_mqtt_s *transport,
                                 const cJSON *root)
{
  const cJSON *nonce = field(root, "connection_nonce");
  const cJSON *epoch = field(root, "epoch");
  const cJSON *request_id = field(root, "request_id");
  const cJSON *turn_id = field(root, "turn_id");
  const cJSON *seq = field(root, "output_seq");
  const cJSON *text = field(root, "text");
  struct deskmate_agent_output_s output;
  uint32_t sequence;
  bool gap;
  size_t text_length;

  if (!valid_string(nonce) || !valid_string(epoch) || !valid_string(request_id) ||
      !valid_string(turn_id) || !valid_string(text) || !parse_uint32(seq, &sequence) ||
      sequence == 0 || strlen(request_id->valuestring) > DESKMATE_REQUEST_ID_MAX ||
      strlen(nonce->valuestring) > DESKMATE_NONCE_MAX ||
      strlen(epoch->valuestring) > DESKMATE_EPOCH_MAX ||
      strlen(turn_id->valuestring) > DESKMATE_TURN_ID_MAX ||
      (text_length = strlen(text->valuestring)) == 0 ||
      text_length > DESKMATE_OUTPUT_TEXT_MAX) {
    return;
  }

  output.connection_nonce = nonce->valuestring;
  output.epoch = epoch->valuestring;
  output.request_id = request_id->valuestring;
  output.turn_id = turn_id->valuestring;
  output.seq = sequence;
  output.text = text->valuestring;
  if (!deskmate_controller_agent_output(transport->controller, &output, &gap)) {
    return;
  }

  if (gap) {
    printf("DeskMate: agent output may be incomplete\n");
  }
  {
    size_t index;

    printf("DeskMate [agent] ");
    for (index = 0; index < text_length; index++) {
      putchar((unsigned char)output.text[index]);
      if (output.text[index] == '\n' && index + 1 < text_length) {
        printf("DeskMate [agent] ");
      }
    }
    if (output.text[text_length - 1] != '\n') {
      printf("\n");
    }
  }
  deskmate_console_prompt();
}

static void receive_turn_result(struct deskmate_mqtt_s *transport,
                                const cJSON *root)
{
  const cJSON *nonce = field(root, "connection_nonce");
  const cJSON *epoch = field(root, "epoch");
  const cJSON *request_id = field(root, "request_id");
  const cJSON *turn_id = field(root, "turn_id");
  const cJSON *status = field(root, "status");
  struct deskmate_turn_result_s result;

  if (!valid_string(nonce) || !valid_string(epoch) || !valid_string(request_id) ||
      !valid_string(turn_id) || !valid_string(status) ||
      strlen(request_id->valuestring) > DESKMATE_REQUEST_ID_MAX ||
      strlen(nonce->valuestring) > DESKMATE_NONCE_MAX ||
      strlen(epoch->valuestring) > DESKMATE_EPOCH_MAX ||
      strlen(turn_id->valuestring) > DESKMATE_TURN_ID_MAX) {
    return;
  }

  result.connection_nonce = nonce->valuestring;
  result.epoch = epoch->valuestring;
  result.request_id = request_id->valuestring;
  result.turn_id = turn_id->valuestring;
  result.status = status->valuestring;
  if (deskmate_controller_turn_result(transport->controller, &result)) {
    printf("DeskMate: %s\n", transport->controller->client.summary);
    deskmate_console_prompt();
  }
}

static void receive_message(struct deskmate_mqtt_s *transport,
                            const char *payload, size_t length)
{
  cJSON *root;
  const char *end = NULL;
  const cJSON *version;
  const cJSON *type;

  if (length > 4096) {
    return;
  }

  if (contains_escaped_nul(payload, length)) {
    return;
  }

  memcpy(transport->json, payload, length);
  transport->json[length] = '\0';
  root = cJSON_ParseWithLengthOpts(transport->json, length + 1, &end, 1);
  if (root == NULL || end != transport->json + length || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return;
  }

  version = field(root, "v");
  type = field(root, "type");
  if (!cJSON_IsNumber(version) || version->valuedouble != 1 || !valid_string(type)) {
    cJSON_Delete(root);
    return;
  }

  if (strcmp(type->valuestring, "hello_ack") == 0) {
    receive_hello_ack(transport, root);
  } else if (strcmp(type->valuestring, "snapshot") == 0) {
    receive_snapshot(transport, payload, length);
  } else if (strcmp(type->valuestring, "usage_snapshot") == 0) {
    receive_usage_snapshot(transport, root);
  } else if (strcmp(type->valuestring, "tool_result") == 0) {
    receive_tool_result(transport, root);
  } else if (strcmp(type->valuestring, "prompt_ack") == 0) {
    receive_prompt_ack(transport, root);
  } else if (strcmp(type->valuestring, "agent_output") == 0) {
    receive_agent_output(transport, root);
  } else if (strcmp(type->valuestring, "turn_result") == 0) {
    receive_turn_result(transport, root);
  }

  cJSON_Delete(root);
}

static void publish_callback(void **unused, struct mqtt_response_publish *published)
{
  struct deskmate_mqtt_s *transport = g_transport;
  (void)unused;

  if (transport == NULL || published->topic_name_size != strlen(transport->to_device) ||
      memcmp(published->topic_name, transport->to_device,
             published->topic_name_size) != 0) {
    return;
  }

  receive_message(transport, published->application_message,
                  published->application_message_size);
}

static int connect_socket(const char *host, const char *port)
{
  struct addrinfo hints;
  struct addrinfo *result;
  struct addrinfo *item;
  int fd = -1;
  int flags;
  int error;
  socklen_t error_size;
  struct pollfd pollfd;

  memset(&hints, 0, sizeof(hints));
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  if (getaddrinfo(host, port, &hints, &result) != 0) {
    return -1;
  }

  for (item = result; item != NULL; item = item->ai_next) {
    fd = socket(item->ai_family, item->ai_socktype, item->ai_protocol);
    if (fd < 0) {
      continue;
    }

    flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0 || fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
      close(fd);
      fd = -1;
      continue;
    }

    if (connect(fd, item->ai_addr, item->ai_addrlen) == 0) {
      break;
    }

    if (errno == EINPROGRESS) {
      pollfd.fd = fd;
      pollfd.events = POLLOUT;
      pollfd.revents = 0;
      if (poll(&pollfd, 1, 5000) > 0) {
        error = 0;
        error_size = sizeof(error);
        if (getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &error_size) == 0 &&
            error == 0) {
          break;
        }
      }
    }

    if (fd >= 0) {
      close(fd);
      fd = -1;
    }
  }

  freeaddrinfo(result);
  if (fd < 0) {
    return -1;
  }

  return fd;
}

static int make_topics(const struct deskmate_mqtt_config_s *config,
                       struct deskmate_mqtt_s *transport)
{
  int written;

  written = snprintf(transport->to_bridge, sizeof(transport->to_bridge),
                     "deskmate/%s/device-to-bridge", transport->device_id);
  if (written < 0 || (size_t)written >= sizeof(transport->to_bridge)) {
    return -1;
  }

  written = snprintf(transport->to_device, sizeof(transport->to_device),
                     "deskmate/%s/bridge-to-device", transport->device_id);
  return written < 0 || (size_t)written >= sizeof(transport->to_device) ? -1 : 0;
}

static bool send_json(void *context, const char *json, size_t length)
{
  struct deskmate_mqtt_s *transport = context;

  return transport != NULL && length <= 4096 &&
         mqtt_publish(&transport->mqtt, transport->to_bridge, json, length,
                      MQTT_PUBLISH_QOS_0) == MQTT_OK;
}

static int send_hello(struct deskmate_mqtt_s *transport)
{
  char message[128];
  int written = snprintf(message, sizeof(message),
                         "{\"v\":1,\"cmd\":\"hello\",\"connection_nonce\":\"%s\"}",
                         transport->controller->client.nonce);

  if (written < 0 || (size_t)written >= sizeof(message)) {
    return -1;
  }

  return mqtt_publish(&transport->mqtt, transport->to_bridge, message,
                      (size_t)written, MQTT_PUBLISH_QOS_0) == MQTT_OK ? 0 : -1;
}

static int make_nonce(char *out, size_t out_size)
{
  unsigned char bytes[18];
  size_t index;
  int fd;

  if (out_size < sizeof(bytes) * 2 + 1) {
    return -1;
  }

  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0 || read(fd, bytes, sizeof(bytes)) != sizeof(bytes)) {
    if (fd >= 0) {
      close(fd);
    }
    return -1;
  }
  close(fd);

  for (index = 0; index < sizeof(bytes); index++) {
    snprintf(&out[index * 2], 3, "%02x", bytes[index]);
  }
  return 0;
}

int deskmate_mqtt_run(const struct deskmate_mqtt_config_s *config,
                      struct deskmate_controller_s *controller,
                      deskmate_input_poll_t poll_input,
                      void *input_context)
{
  struct deskmate_mqtt_s *transport = &g_transport_storage;
  char client_id[DESKMATE_MQTT_DEVICE_ID_MAX + 18];
  char connection_nonce[DESKMATE_NONCE_MAX + 1];
  int fd;
  int written;

  static const struct deskmate_transport_ops_s transport_ops = {
    .send_json = send_json,
  };

  if (config == NULL || controller == NULL || config->host == NULL ||
      config->port == NULL || config->username == NULL ||
      config->password == NULL || config->device_id == NULL ||
      !copy_config(config, transport) || make_topics(config, transport) < 0) {
    return -1;
  }

  written = snprintf(client_id, sizeof(client_id), "deskmate-device-%s",
                     transport->device_id);
  if (written < 0 || (size_t)written >= sizeof(client_id)) {
    return -1;
  }

  for (;;) {
    printf("DeskMate MQTT: connecting to %s:%s\n", transport->host, transport->port);
    fd = connect_socket(transport->host, transport->port);
    if (fd < 0) {
      printf("DeskMate MQTT: broker connection failed\n");
      sleep(1);
      continue;
    }

    transport->controller = controller;
    transport->transport.ops = &transport_ops;
    transport->transport.context = transport;

    if (make_nonce(connection_nonce, sizeof(connection_nonce)) < 0) {
      close(fd);
      sleep(1);
      continue;
    }

    deskmate_controller_new_connection(controller, connection_nonce);
    g_transport = transport;
    mqtt_init(&transport->mqtt, fd, transport->sendbuf, sizeof(transport->sendbuf),
              transport->recvbuf, sizeof(transport->recvbuf), publish_callback);
    if (mqtt_connect(&transport->mqtt, client_id, NULL, NULL, 0, transport->username,
                     transport->password, MQTT_CONNECT_CLEAN_SESSION, 30) != MQTT_OK ||
        mqtt_subscribe(&transport->mqtt, transport->to_device, 0) != MQTT_OK ||
        send_hello(transport) < 0) {
      printf("DeskMate MQTT: connect or subscribe failed\n");
      close(fd);
      g_transport = NULL;
      sleep(1);
      continue;
    }

    printf("DeskMate MQTT connected as %s\n", transport->device_id);
    deskmate_controller_set_active_transport(controller, &transport->transport);
    while (mqtt_sync(&transport->mqtt) == MQTT_OK &&
           transport->mqtt.error == MQTT_OK) {
      if (poll_input != NULL) {
        poll_input(input_context);
      }
      deskmate_controller_timeout(controller, now_ms());
      usleep(100000U);
    }

    printf("DeskMate MQTT: disconnected\n");
    deskmate_controller_link_down(controller);
    deskmate_controller_set_active_transport(controller, NULL);
    close(fd);
    g_transport = NULL;
    sleep(1);
  }
}
