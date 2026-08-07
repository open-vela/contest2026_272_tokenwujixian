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
  uint32_t next_request_number;
};

void deskmate_controller_init(struct deskmate_controller_s *controller,
                              const char *nonce);
void deskmate_controller_new_connection(struct deskmate_controller_s *controller,
                                        const char *nonce);
void deskmate_controller_set_active_transport(
    struct deskmate_controller_s *controller,
    struct deskmate_transport_s *transport);
void deskmate_controller_link_down(struct deskmate_controller_s *controller);
bool deskmate_controller_snapshot(struct deskmate_controller_s *controller,
                                  const struct deskmate_snapshot_s *snapshot,
                                  uint32_t now_ms, bool *display_changed);
bool deskmate_controller_usage_snapshot(
    struct deskmate_controller_s *controller,
    const struct deskmate_usage_snapshot_s *snapshot,
    bool *display_changed);
bool deskmate_controller_tool_result(struct deskmate_controller_s *controller,
                                     const char *tool_call_id,
                                     const char *status);
bool deskmate_controller_timeout(struct deskmate_controller_s *controller,
                                 uint32_t now_ms);
bool deskmate_controller_decide(struct deskmate_controller_s *controller,
                                enum deskmate_decision_e decision);
bool deskmate_controller_query_usage(struct deskmate_controller_s *controller);
bool deskmate_controller_submit_prompt(struct deskmate_controller_s *controller,
                                       const char *text);
bool deskmate_controller_hello_ack(struct deskmate_controller_s *controller,
                                   const struct deskmate_hello_ack_s *ack);
bool deskmate_controller_prompt_ack(struct deskmate_controller_s *controller,
                                    const struct deskmate_prompt_ack_s *ack);
bool deskmate_controller_agent_output(
    struct deskmate_controller_s *controller,
    const struct deskmate_agent_output_s *output, bool *gap_detected);
bool deskmate_controller_turn_result(struct deskmate_controller_s *controller,
                                     const struct deskmate_turn_result_s *result);

#endif
