import test from "node:test";
import assert from "node:assert/strict";
import { AcpBridge, promptIdFor, sanitizeAgentText, summarizeArguments, summarizeMessage } from "../src/bridge.js";

function permissionRequest(overrides = {}) {
  return {
    requestId: 42,
    sessionId: "session-1",
    toolCall: {
      toolCallId: "tool-9",
      title: "Run command",
      rawInput: { command: "git status", apiKey: "never-leak" },
    },
    options: [
      { kind: "allow_once", optionId: "agent-choice-allow" },
      { kind: "reject_once", optionId: "agent-choice-reject" },
    ],
    ...overrides,
  };
}

test("maps once to the agent supplied optionId exactly once", () => {
  const bridge = new AcpBridge();
  const snapshot = bridge.ptSubmit(permissionRequest());
  assert.equal(snapshot.prompt.id, promptIdFor("session-1", 42, "tool-9"));
  assert.ok(Buffer.byteLength(snapshot.prompt.id, "utf8") <= 40);
  /* The hint now shows only the operative field (the command), so a credential
   * key is not echoed at all — stronger than the old redacted JSON dump. */
  assert.equal(snapshot.prompt.hint, "git status");
  assert.doesNotMatch(snapshot.prompt.hint, /never-leak|apiKey/);

  const accepted = bridge.receiveDeviceDecision({ id: snapshot.prompt.id, decision: "once" });
  assert.deepEqual(accepted.result, { outcome: { outcome: "selected", optionId: "agent-choice-allow" } });
  assert.deepEqual(accepted.deviceResult, { type: "permission_result", id: snapshot.prompt.id, accepted: true });
  assert.deepEqual(bridge.receiveDeviceDecision({ id: snapshot.prompt.id, decision: "once" }), {
    type: "ignored", reason: "duplicate_decision",
  });
});

test("reject maps to reject_once and foreign IDs never create ACP responses", () => {
  const bridge = new AcpBridge();
  const snapshot = bridge.ptSubmit(permissionRequest());
  assert.deepEqual(bridge.receiveDeviceDecision({ id: "wrong", decision: "deny" }), {
    type: "permission_result", id: "wrong", accepted: false, error: "unknown_or_stale_id",
  });
  const rejected = bridge.receiveDeviceDecision({ id: snapshot.prompt.id, decision: "deny" });
  assert.equal(rejected.result.outcome.optionId, "agent-choice-reject");
});

test("disconnect clears a pending request", () => {
  const bridge = new AcpBridge();
  const snapshot = bridge.ptSubmit(permissionRequest());
  bridge.disconnect();
  assert.equal(bridge.receiveDeviceDecision({ id: snapshot.prompt.id, decision: "once" }).error, "unknown_or_stale_id");
});

test("unavailable options do not expose unsupported device actions", () => {
  const bridge = new AcpBridge();
  const snapshot = bridge.ptSubmit(permissionRequest({ options: [{ kind: "reject_once", optionId: "no" }] }));
  assert.equal(snapshot.prompt.canOnce, false);
  assert.equal(bridge.receiveDeviceDecision({ id: snapshot.prompt.id, decision: "once" }).error, "unsupported_decision");
});

test("hints and messages preserve their separate UTF-8 caps", () => {
  const hint = summarizeArguments({ text: "你".repeat(100) });
  const message = summarizeMessage("你".repeat(100));

  assert.ok(Buffer.byteLength(hint, "utf8") <= 160);
  assert.ok(Buffer.byteLength(message, "utf8") <= 128);
  assert.ok(hint.endsWith("…"));
  assert.ok(message.endsWith("…"));
  assert.doesNotThrow(() => Buffer.from(hint, "utf8").toString("utf8"));
  assert.doesNotThrow(() => Buffer.from(message, "utf8").toString("utf8"));
});

test("hint summarizes the operative field instead of dumping JSON", () => {
  assert.equal(
    summarizeArguments({ command: "git push origin HEAD:refs/for/trunk" }),
    "git push origin HEAD:refs/for/trunk",
  );
  const both = summarizeArguments({ path: "examples/hello/hello_main.c", command: "ls" });
  assert.equal(both, "ls · examples/hello/hello_main.c");
});

test("opaque prompt IDs remain within the wire contract for long ACP IDs", () => {
  const id = promptIdFor("session-".repeat(50), 42, "tool-".repeat(50));
  assert.equal(Buffer.byteLength(id, "utf8"), 40);
});

test("tool title remains within the 40-byte display limit", () => {
  const bridge = new AcpBridge();
  const snapshot = bridge.ptSubmit(permissionRequest({ toolCall: { toolCallId: "tool-9", title: "你".repeat(20) } }));
  assert.ok(Buffer.byteLength(snapshot.prompt.tool, "utf8") <= 40);
});

test("agent text redacts inline and JSON-shaped secret fields", () => {
  const text = sanitizeAgentText('{"token":"very-secret","authorization":"Bearer another-secret"} password=third-secret');
  assert.doesNotMatch(text, /very-secret|another-secret|third-secret/);
  assert.match(text, /\[redacted\]/);
});
