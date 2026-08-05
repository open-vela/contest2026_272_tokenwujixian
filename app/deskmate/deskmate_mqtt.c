#include "deskmate_mqtt.h"
#include "deskmate_console.h"

#include <arpa/inet.h>
#include <errno.h>
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
  if (!cJSON_IsNumber(version) || version->valueint != 1 ||
      !valid_string(type) || strcmp(type->valuestring, "snapshot") != 0 ||
      !valid_string(nonce) || !valid_string(epoch) || !cJSON_IsNumber(seq) ||
      seq->valuedouble < 1 || seq->valuedouble > UINT32_MAX ||
      seq->valuedouble != (double)seq->valueint || !cJSON_IsNumber(running) ||
      running->valuedouble < 0 || running->valuedouble > UINT8_MAX ||
      !valid_string(message) || strlen(message->valuestring) > DESKMATE_SUMMARY_MAX ||
      prompt == NULL || !parse_prompt(prompt, &prompt_value)) {
    cJSON_Delete(root);
    return;
  }

  snapshot.connection_nonce = nonce->valuestring;
  snapshot.epoch = epoch->valuestring;
  snapshot.seq = (uint32_t)seq->valueint;
  snapshot.running = (uint8_t)running->valueint;
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
      !valid_string(status) || strcmp(nonce->valuestring,
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

  memcpy(transport->json, payload, length);
  transport->json[length] = '\0';
  root = cJSON_ParseWithLengthOpts(transport->json, length + 1, &end, 1);
  if (root == NULL || end != transport->json + length || !cJSON_IsObject(root)) {
    cJSON_Delete(root);
    return;
  }

  version = field(root, "v");
  type = field(root, "type");
  if (!cJSON_IsNumber(version) || version->valueint != 1 || !valid_string(type)) {
    cJSON_Delete(root);
    return;
  }

  if (strcmp(type->valuestring, "snapshot") == 0) {
    receive_snapshot(transport, payload, length);
  } else if (strcmp(type->valuestring, "tool_result") == 0) {
    receive_tool_result(transport, root);
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

int deskmate_mqtt_run(const struct deskmate_mqtt_config_s *config,
                      struct deskmate_controller_s *controller,
                      deskmate_input_poll_t poll_input,
                      void *input_context)
{
  struct deskmate_mqtt_s *transport = &g_transport_storage;
  char client_id[DESKMATE_MQTT_DEVICE_ID_MAX + 18];
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
