import test from "node:test";
import assert from "node:assert/strict";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { hello, promptSubmit } from "../src/protocol.js";

test("snapshot reports the coding agent's state", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });

  await adapter.start();
  await core.receive(hello("nonce"));
  assert.equal(outbound.find((message) => message.type === "snapshot").backend, "up");

  await adapter.stop();
  assert.equal(outbound.at(-1).type, "snapshot");
  assert.equal(outbound.at(-1).backend, "down");
  assert.match(outbound.at(-1).msg, /exited/i);
});

test("a down backend refuses the prompt locally instead of starting a doomed turn", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await adapter.start();

  await core.receive(hello("nonce"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  await adapter.stop();

  await core.receive(promptSubmit("nonce", epoch, "request-1", "Build the firmware"));
  const ack = outbound.find((message) => message.type === "prompt_ack");
  assert.equal(ack.accepted, false);
  assert.equal(ack.error, "backend_unavailable");
  assert.deepEqual(adapter.submits, []);
  assert.equal(outbound.some((message) => message.type === "turn_result"), false);
});

test("an unknown backend state leaves the device free to submit", async () => {
  const outbound = [];
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });

  await core.receive(hello("nonce"));
  const epoch = outbound.find((message) => message.type === "snapshot").epoch;
  assert.equal(outbound.find((message) => message.type === "snapshot").backend, "unknown");

  await core.receive(promptSubmit("nonce", epoch, "request-1", "Explain the tree"));
  const ack = outbound.find((message) => message.type === "prompt_ack");
  assert.equal(ack.accepted, true);
  assert.deepEqual(adapter.submits, ["Explain the tree"]);
});
