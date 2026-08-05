#include "deskmate_console.h"

#include <assert.h>
#include <string.h>

struct test_transport_s {
  unsigned int sends;
  char message[256];
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
  char decision[256];
  char overlong_summary[DESKMATE_SUMMARY_MAX + 2];
  char blank[] = "\n";
  char unsupported[] = "status\n";
  char deny[] = "deny\n";
  bool display_changed;

  deskmate_controller_init(&controller, "nonce");
  deskmate_controller_set_active_transport(&controller, &transport);
  assert(deskmate_controller_snapshot(&controller, &snapshot, 100,
                                      &display_changed));
  assert(display_changed);
  assert(controller.client.state == DESKMATE_ATTENTION);
  assert(deskmate_console_handle_line(&controller, blank));
  assert(capture.sends == 0);
  assert(deskmate_console_handle_line(&controller, unsupported));
  assert(capture.sends == 0);
  assert(deskmate_console_handle_line(&controller, deny));
  assert(capture.sends == 1);
  assert(strstr(capture.message, "\"decision\":\"deny\"") != NULL);
  assert(!deskmate_controller_decide(&controller, DESKMATE_DECISION_DENY));
  assert(capture.sends == 1);
  snapshot.seq = 2;
  assert(deskmate_controller_snapshot(&controller, &snapshot, 101,
                                      &display_changed));
  assert(!display_changed);
  assert(controller.client.last_snapshot_ms == 101);
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
  assert(deskmate_controller_tool_result(&controller, "tool-1", "completed"));
  assert(controller.client.state == DESKMATE_RESULT);
  assert(strstr(controller.client.summary, "tool-1: completed") != NULL);
  assert(!deskmate_controller_tool_result(&controller, "tool-1", "pending"));
  return 0;
}
