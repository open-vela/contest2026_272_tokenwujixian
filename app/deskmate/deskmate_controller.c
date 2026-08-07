#include "deskmate_controller.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DESKMATE_PROMPT_MESSAGE_MAX 4097

static char g_prompt_message[DESKMATE_PROMPT_MESSAGE_MAX];

static bool make_request_id(struct deskmate_controller_s *controller,
                            char *out, size_t out_size)
{
  int written;

  controller->next_request_number++;
  written = snprintf(out, out_size, "r-%.24s-%08" PRIx32,
                     controller->client.nonce, controller->next_request_number);
  return written > 0 && (size_t)written < out_size;
}

void deskmate_controller_init(struct deskmate_controller_s *controller,
                              const char *nonce)
{
  memset(controller, 0, sizeof(*controller));
  deskmate_client_init(&controller->client, nonce);
}

void deskmate_controller_new_connection(struct deskmate_controller_s *controller,
                                        const char *nonce)
{
  deskmate_client_init(&controller->client, nonce);
  controller->decision_in_flight = false;
  controller->decided_prompt_id[0] = '\0';
}

void deskmate_controller_set_active_transport(
    struct deskmate_controller_s *controller,
    struct deskmate_transport_s *transport)
{
  controller->active_transport = transport;
}

void deskmate_controller_link_down(struct deskmate_controller_s *controller)
{
  deskmate_client_link_down(&controller->client);
  controller->decision_in_flight = false;
  controller->decided_prompt_id[0] = '\0';
}

bool deskmate_controller_snapshot(struct deskmate_controller_s *controller,
                                  const struct deskmate_snapshot_s *snapshot,
                                  uint32_t now_ms, bool *display_changed)
{
  if (!deskmate_client_snapshot(&controller->client, snapshot, now_ms,
                                display_changed)) {
    return false;
  }

  if (!controller->client.has_prompt ||
      strcmp(controller->client.prompt.id, controller->decided_prompt_id) != 0) {
    controller->decision_in_flight = false;
    controller->decided_prompt_id[0] = '\0';
  }

  return true;
}

bool deskmate_controller_usage_snapshot(
    struct deskmate_controller_s *controller,
    const struct deskmate_usage_snapshot_s *snapshot,
    bool *display_changed)
{
  return deskmate_client_usage_snapshot(&controller->client, snapshot,
                                        display_changed);
}

bool deskmate_controller_tool_result(struct deskmate_controller_s *controller,
                                     const char *tool_call_id,
                                     const char *status)
{
  if (!deskmate_client_tool_result(&controller->client, tool_call_id, status)) {
    return false;
  }

  controller->decision_in_flight = false;
  controller->decided_prompt_id[0] = '\0';
  return true;
}

bool deskmate_controller_timeout(struct deskmate_controller_s *controller,
                                 uint32_t now_ms)
{
  if (!deskmate_client_timeout(&controller->client, now_ms)) {
    return false;
  }

  controller->decision_in_flight = false;
  controller->decided_prompt_id[0] = '\0';
  return true;
}

bool deskmate_controller_decide(struct deskmate_controller_s *controller,
                                enum deskmate_decision_e decision)
{
  char message[512];

  if (controller->decision_in_flight ||
      !deskmate_client_build_decision(&controller->client, decision, message,
                                      sizeof(message))) {
    return false;
  }

  if (!deskmate_transport_send(controller->active_transport, message,
                               strlen(message))) {
    return false;
  }

  strcpy(controller->decided_prompt_id, controller->client.prompt.id);
  controller->decision_in_flight = true;
  return true;
}

bool deskmate_controller_query_usage(struct deskmate_controller_s *controller)
{
  char message[192];

  if (!deskmate_client_build_usage_query(&controller->client, message,
                                         sizeof(message))) {
    return false;
  }

  return deskmate_transport_send(controller->active_transport, message,
                                 strlen(message));
}

bool deskmate_controller_submit_prompt(struct deskmate_controller_s *controller,
                                       const char *text)
{
  char request_id[DESKMATE_REQUEST_ID_MAX + 1];

  if (controller == NULL || !make_request_id(controller, request_id,
                                              sizeof(request_id)) ||
      !deskmate_client_build_prompt_submit(&controller->client, request_id, text,
                                           g_prompt_message,
                                           sizeof(g_prompt_message)) ||
      !deskmate_transport_send(controller->active_transport, g_prompt_message,
                               strlen(g_prompt_message))) {
    return false;
  }

  return deskmate_client_turn_submitted(&controller->client, request_id);
}

bool deskmate_controller_hello_ack(struct deskmate_controller_s *controller,
                                   const struct deskmate_hello_ack_s *ack)
{
  return controller != NULL && deskmate_client_hello_ack(&controller->client, ack);
}

bool deskmate_controller_prompt_ack(struct deskmate_controller_s *controller,
                                    const struct deskmate_prompt_ack_s *ack)
{
  return controller != NULL && deskmate_client_prompt_ack(&controller->client, ack);
}

bool deskmate_controller_agent_output(
    struct deskmate_controller_s *controller,
    const struct deskmate_agent_output_s *output, bool *gap_detected)
{
  return controller != NULL && deskmate_client_agent_output(&controller->client,
                                                              output, gap_detected);
}

bool deskmate_controller_turn_result(struct deskmate_controller_s *controller,
                                     const struct deskmate_turn_result_s *result)
{
  return controller != NULL && deskmate_client_turn_result(&controller->client,
                                                             result);
}
