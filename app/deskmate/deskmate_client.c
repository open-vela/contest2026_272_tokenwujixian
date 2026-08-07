#include "deskmate_client.h"

#include <float.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static void copy_bounded(char *out, size_t out_size, const char *value)
{
  if (out_size == 0) {
    return;
  }

  if (value == NULL) {
    out[0] = '\0';
    return;
  }

  strncpy(out, value, out_size - 1);
  out[out_size - 1] = '\0';
}

static void clear_prompt(struct deskmate_client_s *client)
{
  memset(&client->prompt, 0, sizeof(client->prompt));
  client->has_prompt = false;
}

static void clear_usage(struct deskmate_client_s *client)
{
  memset(&client->usage, 0, sizeof(client->usage));
}

static void clear_turn(struct deskmate_client_s *client, bool clear_transcript)
{
  client->turn_active = false;
  client->turn_request_id[0] = '\0';
  client->turn_id[0] = '\0';
  client->last_output_seq = 0;
  client->output_gap = false;
  if (clear_transcript) {
    client->transcript[0] = '\0';
  }
}

static bool valid_utf8(const char *text)
{
  const unsigned char *cursor = (const unsigned char *)text;

  if (text == NULL) {
    return false;
  }

  while (*cursor != '\0') {
    if (*cursor < 0x80) {
      cursor++;
    } else if (*cursor >= 0xc2 && *cursor <= 0xdf && cursor[1] != '\0' &&
               (cursor[1] & 0xc0) == 0x80) {
      cursor += 2;
    } else if (*cursor >= 0xe0 && *cursor <= 0xef && cursor[1] != '\0' &&
               cursor[2] != '\0' &&
               (cursor[1] & 0xc0) == 0x80 && (cursor[2] & 0xc0) == 0x80 &&
               !(*cursor == 0xe0 && cursor[1] < 0xa0) &&
               !(*cursor == 0xed && cursor[1] >= 0xa0)) {
      cursor += 3;
    } else if (*cursor >= 0xf0 && *cursor <= 0xf4 && cursor[1] != '\0' &&
               cursor[2] != '\0' && cursor[3] != '\0' &&
               (cursor[1] & 0xc0) == 0x80 && (cursor[2] & 0xc0) == 0x80 &&
               (cursor[3] & 0xc0) == 0x80 &&
               !(*cursor == 0xf0 && cursor[1] < 0x90) &&
               !(*cursor == 0xf4 && cursor[1] >= 0x90)) {
      cursor += 4;
    } else {
      return false;
    }
  }

  return true;
}

static bool valid_identifier(const char *value, size_t maximum)
{
  const unsigned char *cursor = (const unsigned char *)value;

  if (value == NULL || value[0] == '\0' || strlen(value) > maximum ||
      !valid_utf8(value)) {
    return false;
  }

  while (*cursor != '\0') {
    if (*cursor < 0x20 || *cursor == 0x7f) {
      return false;
    }
    cursor++;
  }

  return true;
}

static bool valid_agent_text(const char *value)
{
  const unsigned char *cursor = (const unsigned char *)value;

  if (value == NULL || value[0] == '\0' ||
      strlen(value) > DESKMATE_OUTPUT_TEXT_MAX || !valid_utf8(value)) {
    return false;
  }

  while (*cursor != '\0') {
    if (*cursor < 0x20 && *cursor != '\n') {
      return false;
    }
    cursor++;
  }

  return true;
}

static bool has_nonspace(const char *value)
{
  while (value != NULL && *value != '\0') {
    if (*value != ' ' && *value != '\t' && *value != '\n' && *value != '\r') {
      return true;
    }
    value++;
  }
  return false;
}

static void append_transcript(struct deskmate_client_s *client,
                              const char *text)
{
  size_t existing = strlen(client->transcript);
  size_t added = strlen(text);
  size_t drop = existing + added > DESKMATE_TRANSCRIPT_MAX
                  ? existing + added - DESKMATE_TRANSCRIPT_MAX : 0;

  if (drop >= existing) {
    size_t start = added - (DESKMATE_TRANSCRIPT_MAX < added ?
                           DESKMATE_TRANSCRIPT_MAX : added);
    while (text[start] != '\0' && ((unsigned char)text[start] & 0xc0) == 0x80) {
      start++;
    }
    copy_bounded(client->transcript, sizeof(client->transcript), text + start);
    return;
  }

  if (drop > 0) {
    while (drop < existing && ((unsigned char)client->transcript[drop] & 0xc0) == 0x80) {
      drop++;
    }
    memmove(client->transcript, client->transcript + drop, existing - drop + 1);
  }
  strncat(client->transcript, text,
          sizeof(client->transcript) - strlen(client->transcript) - 1);
}

static bool append_escaped_json(char *out, size_t out_size, size_t *used,
                                const char *value)
{
  static const char hex[] = "0123456789abcdef";
  const unsigned char *cursor = (const unsigned char *)value;

  if (*used >= out_size || cursor == NULL) {
    return false;
  }
  out[(*used)++] = '"';
  while (*cursor != '\0') {
    if (*cursor == '"' || *cursor == '\\') {
      if (*used + 2 >= out_size) return false;
      out[(*used)++] = '\\'; out[(*used)++] = (char)*cursor;
    } else if (*cursor < 0x20) {
      if (*used + 6 >= out_size) return false;
      out[(*used)++] = '\\'; out[(*used)++] = 'u'; out[(*used)++] = '0';
      out[(*used)++] = '0'; out[(*used)++] = hex[*cursor >> 4];
      out[(*used)++] = hex[*cursor & 0x0f];
    } else {
      if (*used + 1 >= out_size) return false;
      out[(*used)++] = (char)*cursor;
    }
    cursor++;
  }
  if (*used + 1 >= out_size) return false;
  out[(*used)++] = '"';
  out[*used] = '\0';
  return true;
}

static bool append_json_literal(char *out, size_t out_size, size_t *used,
                                const char *literal)
{
  size_t length = strlen(literal);

  if (*used + length >= out_size) {
    return false;
  }

  memcpy(out + *used, literal, length);
  *used += length;
  out[*used] = '\0';
  return true;
}

static bool same_prompt(const struct deskmate_prompt_s *left,
                        const struct deskmate_prompt_s *right)
{
  return strcmp(left->id, right->id) == 0 &&
         strcmp(left->tool, right->tool) == 0 &&
         strcmp(left->hint, right->hint) == 0 &&
         left->can_once == right->can_once && left->can_deny == right->can_deny;
}

static bool valid_nonnegative_double(double value)
{
  return value == value && value >= 0 && value <= DBL_MAX;
}

static bool same_usage(const struct deskmate_usage_s *left,
                       const struct deskmate_usage_snapshot_s *right)
{
  return left->has_charge == right->has_charge &&
         (!left->has_charge ||
          (left->charge_kind == right->charge_kind &&
           left->charge_amount == right->charge_amount &&
           strcmp(left->charge_unit, right->charge_unit) == 0)) &&
         left->has_tokens == right->has_tokens &&
         (!left->has_tokens ||
          (left->input_tokens == right->input_tokens &&
           left->output_tokens == right->output_tokens &&
           left->total_tokens == right->total_tokens)) &&
         left->has_context == right->has_context &&
         (!left->has_context ||
          (left->context_unit == right->context_unit &&
           left->context_used == right->context_used &&
           left->context_limit == right->context_limit));
}

static bool valid_usage_snapshot(const struct deskmate_client_s *client,
                                 const struct deskmate_usage_snapshot_s *snapshot)
{
  if (snapshot == NULL || snapshot->connection_nonce == NULL ||
      snapshot->epoch == NULL || client->epoch[0] == '\0' ||
      !valid_identifier(snapshot->connection_nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(snapshot->epoch, DESKMATE_EPOCH_MAX) ||
      strcmp(snapshot->connection_nonce, client->nonce) != 0 ||
      strcmp(snapshot->epoch, client->epoch) != 0 ||
      snapshot->seq == 0 || snapshot->seq <= client->usage.last_seq) {
    return false;
  }

  if (snapshot->has_charge &&
      ((snapshot->charge_kind != DESKMATE_CHARGE_CREDIT &&
        snapshot->charge_kind != DESKMATE_CHARGE_CURRENCY) ||
       snapshot->charge_unit == NULL ||
       strlen(snapshot->charge_unit) > DESKMATE_BILLING_UNIT_MAX ||
       !valid_nonnegative_double(snapshot->charge_amount))) {
    return false;
  }

  return !snapshot->has_context ||
         ((snapshot->context_unit == DESKMATE_CONTEXT_TOKEN ||
           snapshot->context_unit == DESKMATE_CONTEXT_PERCENT) &&
          valid_nonnegative_double(snapshot->context_used) &&
          valid_nonnegative_double(snapshot->context_limit) &&
          snapshot->context_limit > 0);
}

void deskmate_client_init(struct deskmate_client_s *client, const char *nonce)
{
  memset(client, 0, sizeof(*client));
  copy_bounded(client->nonce, sizeof(client->nonce), nonce);
  client->state = DESKMATE_OFFLINE;
}

void deskmate_client_link_down(struct deskmate_client_s *client)
{
  client->epoch[0] = '\0';
  client->next_seq = 0;
  client->last_snapshot_ms = 0;
  client->summary[0] = '\0';
  clear_prompt(client);
  clear_usage(client);
  client->chat_epoch[0] = '\0';
  client->chat_supported = false;
  clear_turn(client, true);
  client->state = DESKMATE_OFFLINE;
}

bool deskmate_client_snapshot(struct deskmate_client_s *client,
                              const struct deskmate_snapshot_s *snapshot,
                              uint32_t now_ms, bool *display_changed)
{
  enum deskmate_state_e next_state;
  bool next_has_prompt;
  bool changed;

  if (display_changed != NULL) {
    *display_changed = false;
  }

  if (snapshot == NULL || snapshot->connection_nonce == NULL ||
      snapshot->epoch == NULL ||
      snapshot->summary == NULL ||
      strlen(snapshot->summary) > DESKMATE_SUMMARY_MAX ||
      !valid_identifier(snapshot->connection_nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(snapshot->epoch, DESKMATE_EPOCH_MAX) ||
      (snapshot->prompt != NULL &&
       !valid_identifier(snapshot->prompt->id, DESKMATE_PROMPT_ID_MAX)) ||
      strcmp(snapshot->connection_nonce, client->nonce) != 0) {
    return false;
  }

  if (client->epoch[0] == '\0') {
    copy_bounded(client->epoch, sizeof(client->epoch), snapshot->epoch);
    client->next_seq = 1;
    client->usage.last_seq = 0;
    if (strcmp(client->chat_epoch, client->epoch) != 0) {
      client->chat_supported = false;
    }
  }

  if (strcmp(snapshot->epoch, client->epoch) != 0 ||
      snapshot->seq != client->next_seq) {
    return false;
  }

  next_has_prompt = snapshot->prompt != NULL;
  if (next_has_prompt) {
    next_state = DESKMATE_ATTENTION;
  } else if (snapshot->running > 0) {
    next_state = DESKMATE_BUSY;
  } else {
    next_state = DESKMATE_IDLE;
  }

  changed = client->state != next_state ||
            strcmp(client->summary, snapshot->summary) != 0 ||
            client->has_prompt != next_has_prompt ||
            (next_has_prompt && !same_prompt(&client->prompt, snapshot->prompt));

  client->next_seq++;
  client->last_snapshot_ms = now_ms;
  copy_bounded(client->summary, sizeof(client->summary), snapshot->summary);
  if (!next_has_prompt) {
    clear_prompt(client);
  } else {
    client->prompt = *snapshot->prompt;
    client->prompt.id[DESKMATE_PROMPT_ID_MAX] = '\0';
    client->prompt.tool[DESKMATE_TOOL_MAX] = '\0';
    client->prompt.hint[DESKMATE_HINT_MAX] = '\0';
    client->has_prompt = true;
  }
  client->state = next_state;

  if (display_changed != NULL) {
    *display_changed = changed;
  }

  return true;
}

bool deskmate_client_timeout(struct deskmate_client_s *client, uint32_t now_ms)
{
  if (client->state == DESKMATE_OFFLINE ||
      now_ms - client->last_snapshot_ms <= 30000U) {
    return false;
  }

  deskmate_client_link_down(client);
  return true;
}

bool deskmate_client_hello_ack(struct deskmate_client_s *client,
                               const struct deskmate_hello_ack_s *ack)
{
  if (client == NULL || ack == NULL || ack->connection_nonce == NULL ||
      ack->epoch == NULL || strcmp(ack->connection_nonce, client->nonce) != 0 ||
      !valid_identifier(ack->connection_nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(ack->epoch, DESKMATE_EPOCH_MAX)) {
    return false;
  }

  copy_bounded(client->chat_epoch, sizeof(client->chat_epoch), ack->epoch);
  client->chat_supported = ack->chat_turns;
  return true;
}

bool deskmate_client_build_prompt_submit(const struct deskmate_client_s *client,
                                         const char *request_id,
                                         const char *text, char *out,
                                         size_t out_size)
{
  static const char prefix[] = "{\"v\":1,\"cmd\":\"prompt_submit\",\"connection_nonce\":";
  static const char epoch[] = ",\"epoch\":";
  static const char request[] = ",\"request_id\":";
  static const char body[] = ",\"text\":";
  size_t used;

  if (client == NULL || out == NULL || out_size == 0 ||
      client->state == DESKMATE_OFFLINE || !client->chat_supported ||
      strcmp(client->chat_epoch, client->epoch) != 0 || client->turn_active ||
      client->has_prompt ||
      !valid_identifier(request_id, DESKMATE_REQUEST_ID_MAX) || text == NULL ||
      text[0] == '\0' || strlen(text) > DESKMATE_PROMPT_TEXT_MAX ||
      !valid_utf8(text) || !has_nonspace(text)) {
    return false;
  }

  if (sizeof(prefix) - 1 >= out_size) return false;
  memcpy(out, prefix, sizeof(prefix) - 1); used = sizeof(prefix) - 1; out[used] = '\0';
  if (!append_escaped_json(out, out_size, &used, client->nonce) ||
      sizeof(epoch) - 1 + used >= out_size) return false;
  memcpy(out + used, epoch, sizeof(epoch) - 1); used += sizeof(epoch) - 1; out[used] = '\0';
  if (!append_escaped_json(out, out_size, &used, client->epoch) ||
      sizeof(request) - 1 + used >= out_size) return false;
  memcpy(out + used, request, sizeof(request) - 1); used += sizeof(request) - 1; out[used] = '\0';
  if (!append_escaped_json(out, out_size, &used, request_id) ||
      sizeof(body) - 1 + used >= out_size) return false;
  memcpy(out + used, body, sizeof(body) - 1); used += sizeof(body) - 1; out[used] = '\0';
  if (!append_escaped_json(out, out_size, &used, text) || used + 2 > out_size) return false;
  out[used++] = '}'; out[used] = '\0';
  return true;
}

bool deskmate_client_turn_submitted(struct deskmate_client_s *client,
                                    const char *request_id)
{
  if (client == NULL || client->turn_active ||
      !valid_identifier(request_id, DESKMATE_REQUEST_ID_MAX)) {
    return false;
  }

  clear_turn(client, true);
  copy_bounded(client->turn_request_id, sizeof(client->turn_request_id), request_id);
  client->turn_active = true;
  return true;
}

bool deskmate_client_prompt_ack(struct deskmate_client_s *client,
                                const struct deskmate_prompt_ack_s *ack)
{
  if (client == NULL || ack == NULL || ack->connection_nonce == NULL ||
      ack->epoch == NULL || !valid_identifier(ack->request_id, DESKMATE_REQUEST_ID_MAX) ||
      !valid_identifier(ack->connection_nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(ack->epoch, DESKMATE_EPOCH_MAX) ||
      strcmp(ack->connection_nonce, client->nonce) != 0 ||
      strcmp(ack->epoch, client->epoch) != 0 || !client->turn_active ||
      strcmp(ack->request_id, client->turn_request_id) != 0) {
    return false;
  }

  if (!ack->accepted) {
    clear_turn(client, false);
    return true;
  }
  if (!valid_identifier(ack->turn_id, DESKMATE_TURN_ID_MAX)) return false;
  if (client->turn_id[0] != '\0' && strcmp(client->turn_id, ack->turn_id) != 0) return false;
  copy_bounded(client->turn_id, sizeof(client->turn_id), ack->turn_id);
  return true;
}

bool deskmate_client_agent_output(struct deskmate_client_s *client,
                                  const struct deskmate_agent_output_s *output,
                                  bool *gap_detected)
{
  bool gap;

  if (gap_detected != NULL) *gap_detected = false;
  if (client == NULL || output == NULL || output->connection_nonce == NULL ||
      output->epoch == NULL || !valid_identifier(output->request_id, DESKMATE_REQUEST_ID_MAX) ||
      !valid_identifier(output->turn_id, DESKMATE_TURN_ID_MAX) ||
      !valid_identifier(output->connection_nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(output->epoch, DESKMATE_EPOCH_MAX) ||
      !valid_agent_text(output->text) || output->seq == 0 ||
      strcmp(output->connection_nonce, client->nonce) != 0 ||
      strcmp(output->epoch, client->epoch) != 0 || !client->turn_active ||
      strcmp(output->request_id, client->turn_request_id) != 0 ||
      output->seq <= client->last_output_seq) {
    return false;
  }

  if (client->turn_id[0] != '\0' && strcmp(client->turn_id, output->turn_id) != 0) return false;
  if (client->turn_id[0] == '\0') {
    copy_bounded(client->turn_id, sizeof(client->turn_id), output->turn_id);
  }
  gap = client->last_output_seq != 0 && output->seq > client->last_output_seq + 1;
  client->last_output_seq = output->seq;
  client->output_gap = client->output_gap || gap;
  append_transcript(client, output->text);
  if (gap_detected != NULL) *gap_detected = gap;
  return true;
}

bool deskmate_client_turn_result(struct deskmate_client_s *client,
                                 const struct deskmate_turn_result_s *result)
{
  if (client == NULL || result == NULL || result->connection_nonce == NULL ||
      result->epoch == NULL || !valid_identifier(result->request_id, DESKMATE_REQUEST_ID_MAX) ||
      !valid_identifier(result->turn_id, DESKMATE_TURN_ID_MAX) || result->status == NULL ||
      !valid_identifier(result->connection_nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(result->epoch, DESKMATE_EPOCH_MAX) ||
      (strcmp(result->status, "completed") != 0 && strcmp(result->status, "failed") != 0) ||
      strcmp(result->connection_nonce, client->nonce) != 0 ||
      strcmp(result->epoch, client->epoch) != 0 || !client->turn_active ||
      strcmp(result->request_id, client->turn_request_id) != 0) {
    return false;
  }

  if (client->turn_id[0] != '\0' && strcmp(client->turn_id, result->turn_id) != 0) return false;
  snprintf(client->summary, sizeof(client->summary), "Agent turn %s", result->status);
  clear_turn(client, false);
  return true;
}

bool deskmate_client_build_decision(const struct deskmate_client_s *client,
                                    enum deskmate_decision_e decision,
                                    char *out, size_t out_size)
{
  const char *value;
  size_t used = 0;

  if (!client->has_prompt || out == NULL || out_size == 0 ||
      !valid_identifier(client->nonce, DESKMATE_NONCE_MAX) ||
      !valid_identifier(client->epoch, DESKMATE_EPOCH_MAX) ||
      !valid_identifier(client->prompt.id, DESKMATE_PROMPT_ID_MAX)) {
    return false;
  }

  if (decision == DESKMATE_DECISION_ONCE && client->prompt.can_once) {
    value = "once";
  } else if (decision == DESKMATE_DECISION_DENY && client->prompt.can_deny) {
    value = "deny";
  } else {
    return false;
  }

  out[0] = '\0';
  return append_json_literal(out, out_size, &used,
                             "{\"v\":1,\"cmd\":\"permission\",\"connection_nonce\":") &&
         append_escaped_json(out, out_size, &used, client->nonce) &&
         append_json_literal(out, out_size, &used, ",\"epoch\":") &&
         append_escaped_json(out, out_size, &used, client->epoch) &&
         append_json_literal(out, out_size, &used, ",\"id\":") &&
         append_escaped_json(out, out_size, &used, client->prompt.id) &&
         append_json_literal(out, out_size, &used, ",\"decision\":") &&
         append_escaped_json(out, out_size, &used, value) &&
         append_json_literal(out, out_size, &used, "}");
}

bool deskmate_client_usage_snapshot(
    struct deskmate_client_s *client,
    const struct deskmate_usage_snapshot_s *snapshot,
    bool *display_changed)
{
  bool changed;

  if (display_changed != NULL) {
    *display_changed = false;
  }

  if (!valid_usage_snapshot(client, snapshot)) {
    return false;
  }

  changed = !same_usage(&client->usage, snapshot);
  client->usage.last_seq = snapshot->seq;
  client->usage.has_charge = snapshot->has_charge;
  client->usage.charge_kind = snapshot->charge_kind;
  client->usage.charge_amount = snapshot->charge_amount;
  copy_bounded(client->usage.charge_unit, sizeof(client->usage.charge_unit),
               snapshot->has_charge ? snapshot->charge_unit : "");
  client->usage.has_tokens = snapshot->has_tokens;
  client->usage.input_tokens = snapshot->input_tokens;
  client->usage.output_tokens = snapshot->output_tokens;
  client->usage.total_tokens = snapshot->total_tokens;
  client->usage.has_context = snapshot->has_context;
  client->usage.context_unit = snapshot->context_unit;
  client->usage.context_used = snapshot->context_used;
  client->usage.context_limit = snapshot->context_limit;

  if (display_changed != NULL) {
    *display_changed = changed;
  }

  return true;
}

bool deskmate_client_build_usage_query(const struct deskmate_client_s *client,
                                       char *out, size_t out_size)
{
  int written;

  if (client == NULL || out == NULL || out_size == 0 ||
      client->state == DESKMATE_OFFLINE || client->epoch[0] == '\0') {
    return false;
  }

  written = snprintf(out, out_size,
                     "{\"v\":1,\"cmd\":\"usage_query\",\"connection_nonce\":\"%s\",\"epoch\":\"%s\"}",
                     client->nonce, client->epoch);
  return written >= 0 && (size_t)written < out_size;
}

bool deskmate_client_billing_text(const struct deskmate_client_s *client,
                                  char *out, size_t out_size)
{
  int written;
  size_t used = 0;

  if (client == NULL || out == NULL || out_size == 0) {
    return false;
  }

  written = snprintf(out, out_size, "Billing: ");
  if (written < 0 || (size_t)written >= out_size) {
    return false;
  }
  used = (size_t)written;

  if (client->usage.has_charge) {
    written = snprintf(out + used, out_size - used, "%.6g %s",
                       client->usage.charge_amount,
                       client->usage.charge_unit);
    if (written < 0 || (size_t)written >= out_size - used) {
      return false;
    }
    used += (size_t)written;
  }

  written = snprintf(out + used, out_size - used, " / ");
  if (written < 0 || (size_t)written >= out_size - used) {
    return false;
  }
  used += (size_t)written;

  if (client->usage.has_tokens) {
    written = snprintf(out + used, out_size - used, "%" PRIu64 " tokens",
                       client->usage.total_tokens);
    if (written < 0 || (size_t)written >= out_size - used) {
      return false;
    }
  }

  return true;
}

bool deskmate_client_tool_result(struct deskmate_client_s *client,
                                 const char *tool_call_id,
                                 const char *status)
{
  if (client == NULL || tool_call_id == NULL || status == NULL ||
      (strcmp(status, "completed") != 0 && strcmp(status, "failed") != 0)) {
    return false;
  }

  /* A tool result is not authority to clear a still-pending permission. Tool
   * calls that did not request device permission can complete at any time. */
  if (client->has_prompt) {
    return false;
  }

  snprintf(client->summary, sizeof(client->summary), "%s: %s", tool_call_id,
           status);
  clear_prompt(client);
  client->state = DESKMATE_RESULT;
  return true;
}

const char *deskmate_state_name(enum deskmate_state_e state)
{
  switch (state) {
    case DESKMATE_OFFLINE: return "Offline";
    case DESKMATE_IDLE: return "Idle";
    case DESKMATE_BUSY: return "Busy";
    case DESKMATE_ATTENTION: return "Attention";
    case DESKMATE_RESULT: return "Result";
    default: return "Unknown";
  }
}
