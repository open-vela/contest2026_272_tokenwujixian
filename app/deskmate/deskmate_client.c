#include "deskmate_client.h"

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

static bool same_prompt(const struct deskmate_prompt_s *left,
                        const struct deskmate_prompt_s *right)
{
  return strcmp(left->id, right->id) == 0 &&
         strcmp(left->tool, right->tool) == 0 &&
         strcmp(left->hint, right->hint) == 0 &&
         left->can_once == right->can_once && left->can_deny == right->can_deny;
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
      strcmp(snapshot->connection_nonce, client->nonce) != 0) {
    return false;
  }

  if (client->epoch[0] == '\0') {
    copy_bounded(client->epoch, sizeof(client->epoch), snapshot->epoch);
    client->next_seq = 1;
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

bool deskmate_client_build_decision(const struct deskmate_client_s *client,
                                    enum deskmate_decision_e decision,
                                    char *out, size_t out_size)
{
  const char *value;
  int written;

  if (!client->has_prompt || out == NULL || out_size == 0) {
    return false;
  }

  if (decision == DESKMATE_DECISION_ONCE && client->prompt.can_once) {
    value = "once";
  } else if (decision == DESKMATE_DECISION_DENY && client->prompt.can_deny) {
    value = "deny";
  } else {
    return false;
  }

  written = snprintf(out, out_size,
                     "{\"v\":1,\"cmd\":\"permission\",\"connection_nonce\":\"%s\",\"epoch\":\"%s\",\"id\":\"%s\",\"decision\":\"%s\"}",
                     client->nonce, client->epoch, client->prompt.id, value);
  return written >= 0 && (size_t)written < out_size;
}

bool deskmate_client_tool_result(struct deskmate_client_s *client,
                                 const char *tool_call_id,
                                 const char *status)
{
  if (client == NULL || tool_call_id == NULL || status == NULL ||
      (strcmp(status, "completed") != 0 && strcmp(status, "failed") != 0)) {
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
