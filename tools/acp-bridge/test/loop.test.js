import test from "node:test";
import assert from "node:assert/strict";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { UnixLinkClient, UnixLinkServer } from "../src/unix-link.js";
import { decision, hello, MAX_MESSAGE_BYTES, usageQuery } from "../src/protocol.js";

test("Unix loop carries nonce snapshot and exactly one permission decision", async () => {
  const directory = await mkdtemp(join(tmpdir(), "deskmate-loop-"));
  const path = join(directory, "link.sock");
  const received = [];
  const adapter = new FakeAcpAdapter();
  let server;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => server.send(message) });
  server = new UnixLinkServer(path, { onMessage: (message) => core.receive(message), onDisconnect: () => core.disconnect() });
  await server.listen();
  await adapter.start();
  const client = new UnixLinkClient(path, { onMessage: (message) => received.push(message) });
  await client.connect(); client.send(hello("nonce-a"));
  await new Promise((resolve) => setTimeout(resolve, 10));
  const snapshot = received.find((message) => message.type === "snapshot");
  assert.equal(snapshot.connection_nonce, "nonce-a");
  adapter.requestPermission({ requestId: 7, sessionId: "s", toolCall: { toolCallId: "t", title: "Bash", rawInput: { token: "secret" } }, options: [{ kind: "allow_once", optionId: "yes" }, { kind: "reject_once", optionId: "no" }] });
  await new Promise((resolve) => setTimeout(resolve, 10));
  const attention = received.at(-1);
  assert.equal(attention.prompt.hint.includes("secret"), false);
  client.send(decision("nonce-a", attention.epoch, attention.prompt.id, "deny"));
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.deepEqual(adapter.responses, [{ id: 7, result: { outcome: { outcome: "selected", optionId: "no" } } }]);
  client.send(decision("nonce-a", attention.epoch, attention.prompt.id, "deny"));
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(adapter.responses.length, 1);
  client.close(); await core.disconnect(); await server.close(); await rm(directory, { recursive: true, force: true });
});

test("Unix loop delivers session-local usage and answers a matching query", async () => {
  const directory = await mkdtemp(join(tmpdir(), "deskmate-usage-loop-"));
  const path = join(directory, "link.sock");
  const received = [];
  const adapter = new FakeAcpAdapter();
  let server;
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => server.send(message) });
  server = new UnixLinkServer(path, { onMessage: (message) => core.receive(message), onDisconnect: () => core.disconnect() });
  await server.listen();
  await adapter.start();
  const client = new UnixLinkClient(path, { onMessage: (message) => received.push(message) });
  await client.connect();
  client.send(hello("usage-nonce"));
  await new Promise((resolve) => setTimeout(resolve, 10));
  const first = received.find((message) => message.type === "usage_snapshot");
  assert.equal(first.usage.billing.charge, null);
  assert.equal(first.usage.billing.tokens, null);

  adapter.usage({ tokens: { input: 11, output: 3, total: 14 } });
  await new Promise((resolve) => setTimeout(resolve, 10));
  const observed = received.at(-1);
  assert.equal(observed.type, "usage_snapshot");
  assert.deepEqual(observed.usage.billing.tokens, { input: 11, output: 3, total: 14 });

  client.send(usageQuery("usage-nonce", observed.epoch));
  await new Promise((resolve) => setTimeout(resolve, 10));
  const queried = received.at(-1);
  assert.equal(queried.type, "usage_snapshot");
  assert.ok(queried.usage_seq > observed.usage_seq);
  assert.deepEqual(queried.usage, observed.usage);

  client.send(usageQuery("wrong-nonce", observed.epoch));
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.equal(received.at(-1).usage_seq, queried.usage_seq);
  client.close(); await core.disconnect(); await server.close();
  await rm(directory, { recursive: true, force: true });
});

test("stale epoch decisions never reach the ACP adapter", async () => {
  const adapter = new FakeAcpAdapter();
  const outbound = [];
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("nonce-a"));
  adapter.requestPermission({ requestId: 8, sessionId: "s", toolCall: { toolCallId: "t", title: "Bash" }, options: [{ kind: "allow_once", optionId: "yes" }, { kind: "reject_once", optionId: "no" }] });
  const attention = outbound.at(-1);
  await core.receive(decision("nonce-a", "old-epoch", attention.prompt.id, "once"));
  assert.equal(adapter.responses.length, 0);
  assert.deepEqual(outbound.at(-1).error, "stale_connection");
  await core.disconnect();
});

test("disconnect denies the pending ACP request with its original reject option", async () => {
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: () => {} });
  await core.receive(hello("nonce-a"));
  adapter.requestPermission({ requestId: "uuid-request", sessionId: "s", toolCall: { toolCallId: "t", title: "Bash" }, options: [{ kind: "allow_once", optionId: "once" }, { kind: "reject_once", optionId: "reject" }] });
  await core.disconnect();
  assert.deepEqual(adapter.responses, [{ id: "uuid-request", result: { outcome: { outcome: "selected", optionId: "reject" } } }]);
});

test("reconnect keeps the bridge epoch while resetting the nonce stream", async () => {
  const adapter = new FakeAcpAdapter();
  const outbound = [];
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });
  await core.receive(hello("first"));
  const firstEpoch = outbound.find((message) => message.type === "hello_ack").epoch;
  await core.receive(hello("second"));
  const secondEpoch = outbound.filter((message) => message.type === "hello_ack").at(-1).epoch;
  assert.equal(secondEpoch, firstEpoch);
  assert.equal(outbound.filter((message) => message.type === "snapshot").at(-1).seq, 1);
});

test("snapshot message is UTF-8 safe and capped at 128 bytes", async () => {
  const adapter = new FakeAcpAdapter();
  const outbound = [];
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });

  await core.receive(hello("nonce-a"));
  core.snapshot({ msg: "你".repeat(100) });

  const snapshot = outbound.at(-1);
  assert.equal(snapshot.type, "snapshot");
  assert.ok(Buffer.byteLength(snapshot.msg, "utf8") <= 128);
  assert.ok(snapshot.msg.endsWith("…"));
  assert.doesNotThrow(() => Buffer.from(snapshot.msg, "utf8").toString("utf8"));
});

test("tool snapshots use the title instead of raw input", async () => {
  const adapter = new FakeAcpAdapter();
  const outbound = [];
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });

  await core.receive(hello("nonce-a"));
  adapter.tool({
    toolCallId: "tool-1",
    title: "Running: pwd",
    status: "in_progress",
    rawInput: { command: "pwd", __tool_use_purpose: "secret internal purpose" },
  });

  const snapshot = outbound.at(-1);
  assert.equal(snapshot.type, "snapshot");
  assert.equal(snapshot.msg, "Running: pwd");
  assert.doesNotMatch(snapshot.msg, /command|purpose|secret/);
});

test("permission timeout rejects the original ACP request", async () => {
  const adapter = new FakeAcpAdapter();
  const core = new DeskMateBridgeCore({ adapter, publish: () => {}, permissionTimeoutMs: 10 });
  await core.receive(hello("nonce-a"));
  adapter.requestPermission({ requestId: 9, sessionId: "s", toolCall: { toolCallId: "t", title: "Bash" }, options: [{ kind: "reject_once", optionId: "reject" }] });
  await new Promise((resolve) => setTimeout(resolve, 25));
  assert.deepEqual(adapter.responses, [{ id: 9, result: { outcome: { outcome: "selected", optionId: "reject" } } }]);
});

test("Unix loop drops an overlong framed message and resynchronizes", async () => {
  const directory = await mkdtemp(join(tmpdir(), "deskmate-frame-"));
  const path = join(directory, "link.sock");
  const errors = [];
  const messages = [];
  const server = new UnixLinkServer(path, { onMessage: (message) => messages.push(message), onError: (error) => errors.push(error) });
  await server.listen();
  const client = new UnixLinkClient(path, {});
  await client.connect();
  client.socket.write("x".repeat(MAX_MESSAGE_BYTES + 1));
  client.socket.write(`still-overlong\n{"v":1,"cmd":"hello","connection_nonce":"again"}\n`);
  await new Promise((resolve) => setTimeout(resolve, 10));
  assert.deepEqual(errors, ["overlong_message"]);
  assert.deepEqual(messages, [{ v: 1, cmd: "hello", connection_nonce: "again" }]);
  client.close(); await server.close(); await rm(directory, { recursive: true, force: true });
});
