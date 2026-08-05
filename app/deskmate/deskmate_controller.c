#include "deskmate_controller.h"

#include <string.h>

void deskmate_controller_init(struct deskmate_controller_s *controller,
                              const char *nonce)
{
  memset(controller, 0, sizeof(*controller));
  deskmate_client_init(&controller->client, nonce);
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
  char message[256];

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
