#include "deskmate_console.h"

#include <assert.h>
#include <string.h>

struct test_transport_s {
  unsigned int sends;
  char message[4097];
};

static bool capture_json(void *context, const char *json, size_t length)
{
  struct test_transport_s *transport = context;

  assert(length < sizeof(transport->message));
  memcpy(transport->message, json, length);
  transport->message[length] = '\0';
  transport->sends++;
  return true;
}

int main(void)
{
  struct deskmate_controller_s controller;
  struct test_transport_s capture = { 0 };
  const struct deskmate_transport_ops_s transport_ops = {
    .send_json = capture_json,
  };
  struct deskmate_transport_s transport = {
    .ops = &transport_ops,
    .context = &capture,
  };
  struct deskmate_prompt_s prompt = {
    .id = "prompt-1", .tool = "bash_delete", .hint = "rm /tmp/probe",
    .can_once = true, .can_deny = true,
  };
  struct deskmate_snapshot_s snapshot = {
    .connection_nonce = "nonce", .epoch = "epoch", .seq = 1,
    .running = 0, .summary = "approval required", .prompt = &prompt,
  };
  struct deskmate_usage_snapshot_s usage = {
    .connection_nonce = "nonce", .epoch = "epoch", .seq = 1,
    .has_charge = true, .charge_kind = DESKMATE_CHARGE_CREDIT,
    .charge_amount = 0.375, .charge_unit = "credits",
    .has_tokens = true, .input_tokens = 11, .output_tokens = 3,
    .total_tokens = 14,
    .has_context = true, .context_unit = DESKMATE_CONTEXT_TOKEN,
    .context_used = 17, .context_limit = 1000,
  };
  char decision[256];
  char billing[96];
  char overlong_summary[DESKMATE_SUMMARY_MAX + 2];
  char blank[] = "\n";
  char unsupported[] = "status\n";
  char help[] = "help\n";
  char usage_command[] = "usage\n";
  char deny[] = "deny\n";
  bool display_changed;

  deskmate_controller_init(&controller, "nonce");
  deskmate_controller_set_active_transport(&controller, &transport);
  assert(deskmate_controller_snapshot(&controller, &snapshot, 100,
                                      &display_changed));
  assert(display_changed);
  assert(controller.client.state == DESKMATE_ATTENTION);
  assert(deskmate_controller_usage_snapshot(&controller, &usage,
                                            &display_changed));
  assert(display_changed);
  assert(controller.client.state == DESKMATE_ATTENTION);
  assert(controller.client.has_prompt);
  assert(controller.client.last_snapshot_ms == 100);
  assert(deskmate_client_billing_text(&controller.client, billing,
                                      sizeof(billing)));
  assert(strcmp(billing, "Billing: 0.375 credits / 14 tokens") == 0);
  assert(deskmate_controller_query_usage(&controller));
  assert(capture.sends == 1);
  assert(strstr(capture.message, "\"cmd\":\"usage_query\"") != NULL);
  assert(strstr(capture.message, "\"connection_nonce\":\"nonce\"") != NULL);
  assert(strstr(capture.message, "\"epoch\":\"epoch\"") != NULL);
  assert(!deskmate_controller_usage_snapshot(&controller, &usage,
                                             &display_changed));
  assert(deskmate_console_handle_line(&controller, help));
  assert(capture.sends == 1);
  assert(deskmate_console_handle_line(&controller, usage_command));
  assert(capture.sends == 2);
  assert(strstr(capture.message, "\"cmd\":\"usage_query\"") != NULL);
  /* QoS 0 telemetry may lose a message: a newer usage sequence replaces the
   * old display without altering the authority/permission stream. */
  usage.seq = 3;
  usage.has_charge = false;
  usage.has_tokens = true;
  usage.input_tokens = 20;
  usage.output_tokens = 5;
  usage.total_tokens = 25;
  assert(deskmate_controller_usage_snapshot(&controller, &usage,
                                            &display_changed));
  assert(deskmate_client_billing_text(&controller.client, billing,
                                      sizeof(billing)));
  assert(strcmp(billing, "Billing:  / 25 tokens") == 0);
  usage.seq = 2;
  assert(!deskmate_controller_usage_snapshot(&controller, &usage,
                                             &display_changed));
  usage.seq = 4;
  usage.has_charge = true;
  usage.charge_amount = 0.5;
  usage.has_tokens = false;
  assert(deskmate_controller_usage_snapshot(&controller, &usage,
                                            &display_changed));
  assert(deskmate_client_billing_text(&controller.client, billing,
                                      sizeof(billing)));
  assert(strcmp(billing, "Billing: 0.5 credits / ") == 0);
  assert(deskmate_console_handle_line(&controller, blank));
  assert(capture.sends == 2);
  assert(deskmate_console_handle_line(&controller, unsupported));
  assert(capture.sends == 2);
  assert(deskmate_console_handle_line(&controller, deny));
  assert(capture.sends == 3);
  assert(strstr(capture.message, "\"decision\":\"deny\"") != NULL);
  assert(!deskmate_controller_decide(&controller, DESKMATE_DECISION_DENY));
  assert(capture.sends == 3);
  snapshot.seq = 2;
  assert(deskmate_controller_snapshot(&controller, &snapshot, 101,
                                      &display_changed));
  assert(!display_changed);
  assert(controller.client.last_snapshot_ms == 101);
  assert(controller.decision_in_flight);
  assert(!deskmate_controller_tool_result(&controller, "unrelated-tool", "completed"));
  assert(controller.client.has_prompt);
  assert(controller.decision_in_flight);
  assert(deskmate_client_build_decision(&controller.client, DESKMATE_DECISION_DENY,
                                        decision, sizeof(decision)));
  assert(strstr(decision, "\"decision\":\"deny\"") != NULL);
  memset(overlong_summary, 'x', sizeof(overlong_summary) - 1);
  overlong_summary[sizeof(overlong_summary) - 1] = '\0';
  snapshot.seq = 3;
  snapshot.summary = overlong_summary;
  assert(!deskmate_controller_snapshot(&controller, &snapshot, 102,
                                       &display_changed));
  snapshot.summary = "approval required";
  snapshot.seq = 4;
  assert(!deskmate_controller_snapshot(&controller, &snapshot, 101,
                                       &display_changed));
  assert(deskmate_controller_timeout(&controller, 30102));
  assert(controller.client.state == DESKMATE_OFFLINE);
  assert(!controller.client.usage.has_charge);
  assert(!controller.client.usage.has_tokens);
  assert(deskmate_controller_tool_result(&controller, "tool-1", "completed"));
  assert(controller.client.state == DESKMATE_RESULT);
  assert(strstr(controller.client.summary, "tool-1: completed") != NULL);
  assert(!deskmate_controller_tool_result(&controller, "tool-1", "pending"));
  {
    struct deskmate_controller_s chat_controller;
    struct deskmate_client_s fresh_client;
    struct deskmate_client_s quoted_id_client;
    struct test_transport_s chat_capture = { 0 };
    struct deskmate_transport_s chat_transport = {
      .ops = &transport_ops,
      .context = &chat_capture,
    };
    struct deskmate_hello_ack_s hello_ack = {
      .connection_nonce = "chat-nonce", .epoch = "chat-epoch",
      .chat_turns = true,
    };
    struct deskmate_snapshot_s chat_snapshot = {
      .connection_nonce = "chat-nonce", .epoch = "chat-epoch", .seq = 1,
      .running = 0, .summary = "Agent ready", .prompt = NULL,
    };
    struct deskmate_prompt_ack_s prompt_ack = {
      .connection_nonce = "chat-nonce", .epoch = "chat-epoch",
      .request_id = "r-chat-nonce-00000001", .turn_id = "turn-1",
      .accepted = true,
    };
    struct deskmate_agent_output_s output = {
      .connection_nonce = "chat-nonce", .epoch = "chat-epoch",
      .request_id = "r-chat-nonce-00000001", .turn_id = "turn-1",
      .seq = 1, .text = "first answer\n",
    };
    struct deskmate_turn_result_s turn_result = {
      .connection_nonce = "chat-nonce", .epoch = "chat-epoch",
      .request_id = "r-chat-nonce-00000001", .turn_id = "turn-1",
      .status = "completed",
    };
    char chat_line[] = "@ inspect \"this\"\n";
    bool gap;
    char overlong_epoch[DESKMATE_EPOCH_MAX + 2];
    char escaped_decision[512];

    deskmate_controller_init(&chat_controller, "chat-nonce");
    deskmate_controller_set_active_transport(&chat_controller, &chat_transport);
    assert(deskmate_controller_hello_ack(&chat_controller, &hello_ack));
    assert(deskmate_controller_snapshot(&chat_controller, &chat_snapshot, 1,
                                        &display_changed));
    assert(deskmate_console_handle_line(&chat_controller, chat_line));
    assert(chat_capture.sends == 1);
    assert(strstr(chat_capture.message, "\"cmd\":\"prompt_submit\"") != NULL);
    assert(strstr(chat_capture.message, "inspect \\\"this\\\"") != NULL);
    assert(chat_controller.client.turn_active);
    assert(deskmate_controller_prompt_ack(&chat_controller, &prompt_ack));
    assert(deskmate_controller_agent_output(&chat_controller, &output, &gap));
    assert(!gap);
    assert(strcmp(chat_controller.client.transcript, "first answer\n") == 0);
    output.seq = 3;
    output.text = "third answer";
    assert(deskmate_controller_agent_output(&chat_controller, &output, &gap));
    assert(gap);
    assert(chat_controller.client.output_gap);
    assert(!deskmate_controller_agent_output(&chat_controller, &output, &gap));
    output.seq = 4;
    output.turn_id = "wrong-turn";
    assert(!deskmate_controller_agent_output(&chat_controller, &output, &gap));
    assert(deskmate_controller_turn_result(&chat_controller, &turn_result));
    assert(!chat_controller.client.turn_active);
    assert(strstr(chat_controller.client.transcript, "third answer") != NULL);

    memset(overlong_epoch, 'e', sizeof(overlong_epoch) - 1);
    overlong_epoch[sizeof(overlong_epoch) - 1] = '\0';
    deskmate_client_init(&fresh_client, "chat-nonce");
    chat_snapshot.epoch = overlong_epoch;
    assert(!deskmate_client_snapshot(&fresh_client, &chat_snapshot, 1,
                                     &display_changed));
    assert(fresh_client.epoch[0] == '\0');

    prompt.id[0] = 'p';
    prompt.id[1] = '"';
    strcpy(prompt.id + 2, "quoted");
    snapshot.connection_nonce = "nonce";
    snapshot.epoch = "epoch";
    snapshot.seq = 1;
    snapshot.summary = "approval required";
    snapshot.prompt = &prompt;
    deskmate_client_init(&quoted_id_client, "nonce");
    assert(deskmate_client_snapshot(&quoted_id_client, &snapshot, 1,
                                    &display_changed));
    assert(deskmate_client_build_decision(&quoted_id_client,
                                          DESKMATE_DECISION_DENY,
                                          escaped_decision,
                                          sizeof(escaped_decision)));
    assert(strstr(escaped_decision, "p\\\"quoted") != NULL);
  }
  return 0;
}
