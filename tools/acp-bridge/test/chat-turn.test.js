import test from "node:test";
import assert from "node:assert/strict";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { byteLength, hello, promptSubmit } from "../src/protocol.js";

function deferred() {
  let resolve;
  let reject;
  const promise = new Promise((nextResolve, nextReject) => { resolve = nextResolve; reject = nextReject; });
  return { promise, resolve, reject };
}

test("device prompt creates one turn and streams sanitized bounded output", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const pending = deferred();
  adapter.submitHandler = () => pending.promise;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message), outputFlushMs: 1 });

  await core.receive(hello("nonce"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.receive(promptSubmit("nonce", epoch, "request-1", "Explain the working tree"));
  assert.deepEqual(adapter.submits, ["Explain the working tree"]);
  assert.throws(() => core.submitLocal("must share the device turn lock"), /turn_busy/);
  const ack = outbound.find((message) => message.type === "prompt_ack");
  assert.equal(ack.accepted, true);
  assert.match(ack.turn_id, /^t-/);

  adapter.text("first\u001b[31m token=very-secret\n");
  adapter.text("你".repeat(2_000));
  await new Promise((resolve) => setTimeout(resolve, 20));
  const chunks = outbound.filter((message) => message.type === "agent_output");
  assert.ok(chunks.length >= 2);
  assert.equal(chunks[0].request_id, "request-1");
  assert.equal(chunks[0].turn_id, ack.turn_id);
  assert.ok(chunks.every((message, index) => message.output_seq === index + 1 && byteLength(message) <= 4096));
  assert.doesNotMatch(chunks.map((message) => message.text).join(""), /\u001b|very-secret/);

  pending.resolve({ stopReason: "end_turn" });
  await new Promise((resolve) => setTimeout(resolve, 10));
  const result = outbound.find((message) => message.type === "turn_result");
  assert.equal(result.status, "completed");
  assert.equal(result.request_id, "request-1");
  await core.receive(promptSubmit("nonce", epoch, "request-1", "Explain the working tree"));
  assert.equal(adapter.submits.length, 1);
  assert.equal(outbound.at(-2).type, "prompt_ack");
  assert.equal(outbound.at(-1).type, "turn_result");
  await core.submitLocal("A local prompt after the device turn");
});

test("device prompt rejects stale, invalid, duplicate-conflicting, and busy requests", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const pending = deferred();
  adapter.submitHandler = () => pending.promise;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("nonce"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.receive({ v: 1, cmd: "prompt_submit", connection_nonce: "old", epoch, request_id: "bad", text: "hello" });
  await core.receive({ v: 1, cmd: "prompt_submit", connection_nonce: "nonce", epoch, request_id: "", text: "hello" });
  await core.receive(promptSubmit("nonce", epoch, "one", "first"));
  await core.receive(promptSubmit("nonce", epoch, "one", "changed"));
  await core.receive(promptSubmit("nonce", epoch, "two", "second"));
  const acknowledgements = outbound.filter((message) => message.type === "prompt_ack");
  assert.deepEqual(acknowledgements.map((message) => message.error ?? "accepted"), ["stale_connection", "invalid_request_id", "accepted", "request_conflict", "turn_busy"]);
  assert.deepEqual(adapter.submits, ["first"]);
  pending.resolve({});
  await new Promise((resolve) => setTimeout(resolve, 5));
});

test("disconnect detaches an active device turn without replaying it after reconnect", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const pending = deferred();
  adapter.submitHandler = () => pending.promise;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message), outputFlushMs: 1 });
  await core.receive(hello("first"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.receive(promptSubmit("first", epoch, "one", "first"));
  await core.disconnect();
  await core.receive(hello("second"));
  adapter.text("not for the new device");
  pending.resolve({});
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(outbound.filter((message) => message.type === "agent_output").length, 0);
  assert.equal(outbound.filter((message) => message.type === "turn_result").length, 0);
});

test("a disconnected nonce cannot submit another ACP turn", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("old-nonce"));
  const oldEpoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.disconnect();
  await core.receive(promptSubmit("old-nonce", oldEpoch, "old-request", "must not run"));
  assert.deepEqual(adapter.submits, []);
  assert.equal(outbound.filter((message) => message.type === "prompt_ack").length, 0);
});

test("a detached device turn rejects later ACP permissions instead of rebinding them", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const pending = deferred();
  adapter.submitHandler = () => pending.promise;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("first"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.receive(promptSubmit("first", epoch, "one", "old turn"));
  await core.disconnect();
  await core.receive(hello("second"));
  adapter.requestPermission({
    requestId: "old-permission", sessionId: "session", toolCall: { toolCallId: "tool", title: "Old tool" },
    options: [{ kind: "allow_once", optionId: "allow" }, { kind: "reject_once", optionId: "deny" }],
  });
  await new Promise((resolve) => setTimeout(resolve, 5));
  assert.deepEqual(adapter.responses, [{ id: "old-permission", result: { outcome: { outcome: "selected", optionId: "deny" } } }]);
  assert.equal(outbound.filter((message) => message.type === "snapshot" && message.connection_nonce === "second" && message.prompt).length, 0);
  pending.resolve({});
});

test("a detached device turn never publishes later tool activity to a new nonce", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const pending = deferred();
  adapter.submitHandler = () => pending.promise;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("first"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.receive(promptSubmit("first", epoch, "one", "old turn"));
  await core.disconnect();
  await core.receive(hello("second"));
  adapter.tool({ toolCallId: "old-tool", title: "Sensitive old tool", status: "completed" });
  assert.equal(outbound.filter((message) => message.type === "tool_result" && message.connection_nonce === "second").length, 0);
  assert.equal(outbound.filter((message) => message.type === "snapshot" && message.connection_nonce === "second" && /Sensitive old tool/.test(message.msg)).length, 0);
  pending.resolve({});
});

test("oversized stale permission IDs never create an oversized response", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("nonce"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await core.receive({
    v: 1, cmd: "permission", connection_nonce: "old", epoch, id: "x".repeat(3957), decision: "once",
  });
  assert.equal(outbound.filter((message) => message.type === "permission_result").length, 0);
});
