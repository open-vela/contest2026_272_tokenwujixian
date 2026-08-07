#ifndef DESKMATE_CLIENT_H
#define DESKMATE_CLIENT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DESKMATE_NONCE_MAX 48
#define DESKMATE_EPOCH_MAX 48
#define DESKMATE_PROMPT_ID_MAX 40
#define DESKMATE_TOOL_MAX 24
#define DESKMATE_HINT_MAX 96
#define DESKMATE_SUMMARY_MAX 128
#define DESKMATE_BILLING_UNIT_MAX 16
#define DESKMATE_REQUEST_ID_MAX 40
#define DESKMATE_TURN_ID_MAX 40
#define DESKMATE_PROMPT_TEXT_MAX 2048
#define DESKMATE_OUTPUT_TEXT_MAX 1600
#define DESKMATE_TRANSCRIPT_MAX 8192

enum deskmate_state_e {
  DESKMATE_OFFLINE,
  DESKMATE_IDLE,
  DESKMATE_BUSY,
  DESKMATE_ATTENTION,
  DESKMATE_RESULT,
};

enum deskmate_decision_e {
  DESKMATE_DECISION_ONCE,
  DESKMATE_DECISION_DENY,
};

enum deskmate_charge_kind_e {
  DESKMATE_CHARGE_CREDIT,
  DESKMATE_CHARGE_CURRENCY,
};

enum deskmate_context_unit_e {
  DESKMATE_CONTEXT_TOKEN,
  DESKMATE_CONTEXT_PERCENT,
};

struct deskmate_prompt_s {
  char id[DESKMATE_PROMPT_ID_MAX + 1];
  char tool[DESKMATE_TOOL_MAX + 1];
  char hint[DESKMATE_HINT_MAX + 1];
  bool can_once;
  bool can_deny;
};

struct deskmate_snapshot_s {
  const char *connection_nonce;
  const char *epoch;
  uint32_t seq;
  uint8_t running;
  const char *summary;
  const struct deskmate_prompt_s *prompt;
};

/* Usage is optional telemetry. It is independent from authority snapshots and
 * carries native units only: charge is the left billing slot and tokens the
 * right slot. */
struct deskmate_usage_snapshot_s {
  const char *connection_nonce;
  const char *epoch;
  uint64_t seq;
  bool has_charge;
  enum deskmate_charge_kind_e charge_kind;
  double charge_amount;
  const char *charge_unit;
  bool has_tokens;
  uint64_t input_tokens;
  uint64_t output_tokens;
  uint64_t total_tokens;
  bool has_context;
  enum deskmate_context_unit_e context_unit;
  double context_used;
  double context_limit;
};

struct deskmate_usage_s {
  uint64_t last_seq;
  bool has_charge;
  enum deskmate_charge_kind_e charge_kind;
  double charge_amount;
  char charge_unit[DESKMATE_BILLING_UNIT_MAX + 1];
  bool has_tokens;
  uint64_t input_tokens;
  uint64_t output_tokens;
  uint64_t total_tokens;
  bool has_context;
  enum deskmate_context_unit_e context_unit;
  double context_used;
  double context_limit;
};

struct deskmate_hello_ack_s {
  const char *connection_nonce;
  const char *epoch;
  bool chat_turns;
};

struct deskmate_prompt_ack_s {
  const char *connection_nonce;
  const char *epoch;
  const char *request_id;
  const char *turn_id;
  bool accepted;
};

struct deskmate_agent_output_s {
  const char *connection_nonce;
  const char *epoch;
  const char *request_id;
  const char *turn_id;
  uint32_t seq;
  const char *text;
};

struct deskmate_turn_result_s {
  const char *connection_nonce;
  const char *epoch;
  const char *request_id;
  const char *turn_id;
  const char *status;
};

struct deskmate_client_s {
  enum deskmate_state_e state;
  char nonce[DESKMATE_NONCE_MAX + 1];
  char epoch[DESKMATE_EPOCH_MAX + 1];
  uint32_t next_seq;
  char summary[DESKMATE_SUMMARY_MAX + 1];
  struct deskmate_prompt_s prompt;
  bool has_prompt;
  uint32_t last_snapshot_ms;
  struct deskmate_usage_s usage;
  char chat_epoch[DESKMATE_EPOCH_MAX + 1];
  bool chat_supported;
  bool turn_active;
  char turn_request_id[DESKMATE_REQUEST_ID_MAX + 1];
  char turn_id[DESKMATE_TURN_ID_MAX + 1];
  uint32_t last_output_seq;
  bool output_gap;
  char transcript[DESKMATE_TRANSCRIPT_MAX + 1];
};

void deskmate_client_init(struct deskmate_client_s *client, const char *nonce);
void deskmate_client_link_down(struct deskmate_client_s *client);
bool deskmate_client_snapshot(struct deskmate_client_s *client,
                              const struct deskmate_snapshot_s *snapshot,
                              uint32_t now_ms, bool *display_changed);
bool deskmate_client_usage_snapshot(
    struct deskmate_client_s *client,
    const struct deskmate_usage_snapshot_s *snapshot,
    bool *display_changed);
bool deskmate_client_timeout(struct deskmate_client_s *client, uint32_t now_ms);
bool deskmate_client_hello_ack(struct deskmate_client_s *client,
                               const struct deskmate_hello_ack_s *ack);
bool deskmate_client_build_prompt_submit(const struct deskmate_client_s *client,
                                         const char *request_id,
                                         const char *text, char *out,
                                         size_t out_size);
bool deskmate_client_turn_submitted(struct deskmate_client_s *client,
                                    const char *request_id);
bool deskmate_client_prompt_ack(struct deskmate_client_s *client,
                                const struct deskmate_prompt_ack_s *ack);
bool deskmate_client_agent_output(struct deskmate_client_s *client,
                                  const struct deskmate_agent_output_s *output,
                                  bool *gap_detected);
bool deskmate_client_turn_result(struct deskmate_client_s *client,
                                 const struct deskmate_turn_result_s *result);
bool deskmate_client_build_decision(const struct deskmate_client_s *client,
                                    enum deskmate_decision_e decision,
                                    char *out, size_t out_size);
bool deskmate_client_build_usage_query(const struct deskmate_client_s *client,
                                       char *out, size_t out_size);
bool deskmate_client_billing_text(const struct deskmate_client_s *client,
                                  char *out, size_t out_size);
bool deskmate_client_tool_result(struct deskmate_client_s *client,
                                 const char *tool_call_id,
                                 const char *status);
const char *deskmate_state_name(enum deskmate_state_e state);

#endif
