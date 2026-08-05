#ifndef DESKMATE_CONTROLLER_H
#define DESKMATE_CONTROLLER_H

#include "deskmate_client.h"
#include "deskmate_transport.h"

#include <stdbool.h>
#include <stdint.h>

struct deskmate_controller_s {
  struct deskmate_client_s client;
  struct deskmate_transport_s *active_transport;
  char decided_prompt_id[DESKMATE_PROMPT_ID_MAX + 1];
  bool decision_in_flight;
};

void deskmate_controller_init(struct deskmate_controller_s *controller,
                              const char *nonce);
void deskmate_controller_set_active_transport(
    struct deskmate_controller_s *controller,
    struct deskmate_transport_s *transport);
void deskmate_controller_link_down(struct deskmate_controller_s *controller);
bool deskmate_controller_snapshot(struct deskmate_controller_s *controller,
                                  const struct deskmate_snapshot_s *snapshot,
                                  uint32_t now_ms, bool *display_changed);
bool deskmate_controller_tool_result(struct deskmate_controller_s *controller,
                                     const char *tool_call_id,
                                     const char *status);
bool deskmate_controller_timeout(struct deskmate_controller_s *controller,
                                 uint32_t now_ms);
bool deskmate_controller_decide(struct deskmate_controller_s *controller,
                                enum deskmate_decision_e decision);

#endif
