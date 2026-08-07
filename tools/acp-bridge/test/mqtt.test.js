import test from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import net from "node:net";
import mqtt from "mqtt";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { MqttTransport } from "../src/mqtt-transport.js";
import { decision, hello, promptSubmit, usageQuery } from "../src/protocol.js";

async function freePort() {
  const server = net.createServer();
  await new Promise((resolve) => server.listen(0, "127.0.0.1", resolve));
  const { port } = server.address();
  await new Promise((resolve) => server.close(resolve));
  return port;
}

async function waitFor(client, event) {
  return new Promise((resolve, reject) => {
    client.once(event, resolve);
    client.once("error", reject);
  });
}

async function waitForBroker(port) {
  const deadline = Date.now() + 2_000;
  while (Date.now() < deadline) {
    const probe = mqtt.connect(`mqtt://127.0.0.1:${port}`, { reconnectPeriod: 0 });
    try {
      await waitFor(probe, "connect");
      probe.end(true);
      return;
    } catch {
      probe.end(true);
      await new Promise((resolve) => setTimeout(resolve, 20));
    }
  }
  throw new Error("broker did not start");
}

async function waitForMessage(messages, predicate) {
  const deadline = Date.now() + 2_000;
  while (Date.now() < deadline) {
    const message = messages.find(predicate);
    if (message) return message;
    await new Promise((resolve) => setTimeout(resolve, 10));
  }
  throw new Error("timed out waiting for MQTT message");
}

test("MQTT transport carries hello, snapshot, and exactly one decision", async () => {
  const port = await freePort();
  const broker = spawn("mosquitto", ["-c", "/dev/null", "-p", String(port)], { stdio: "ignore" });
  const brokerUrl = `mqtt://127.0.0.1:${port}`;
  const base = "deskmate/mqtt-test";
  await waitForBroker(port);
  const device = mqtt.connect(brokerUrl, { protocolVersion: 5, clean: true, properties: { sessionExpiryInterval: 0 } });
  try {
    await waitFor(device, "connect");
    const received = [];
    await new Promise((resolve, reject) => device.subscribe(`${base}/bridge-to-device`, { qos: 0 }, (error) => error ? reject(error) : resolve()));
    device.on("message", (_topic, payload) => received.push(JSON.parse(payload.toString("utf8"))));
    const adapter = new FakeAcpAdapter();
    let transport;
    const core = new DeskMateBridgeCore({ adapter, publish: (message) => transport.send(message) });
    transport = new MqttTransport({ brokerUrl, topicPrefix: "deskmate", deviceId: "mqtt-test", clientId: "bridge-test" });
    await transport.start({ onMessage: (message) => core.receive(message), onLinkState: (state) => { if (state === "down") core.disconnect(); } });
    await new Promise((resolve, reject) => device.publish(`${base}/device-to-bridge`, JSON.stringify(hello("mqtt-nonce")), { qos: 0, retain: false }, (error) => error ? reject(error) : resolve()));
    const snapshot = await waitForMessage(received, (message) => message.type === "snapshot");
    assert.equal(snapshot.connection_nonce, "mqtt-nonce");
    adapter.requestPermission({ requestId: 1, sessionId: "s", toolCall: { toolCallId: "t", title: "Bash", rawInput: { token: "secret" } }, options: [{ kind: "allow_once", optionId: "yes" }, { kind: "reject_once", optionId: "no" }] });
    const attention = await waitForMessage(received, (message) => message.type === "snapshot" && message.prompt);
    await new Promise((resolve, reject) => device.publish(`${base}/device-to-bridge`, JSON.stringify(decision("mqtt-nonce", attention.epoch, attention.prompt.id, "deny")), { qos: 0, retain: false }, (error) => error ? reject(error) : resolve()));
    await waitForMessage(received, (message) => message.type === "permission_result" && message.accepted);
    assert.deepEqual(adapter.responses, [{ id: 1, result: { outcome: { outcome: "selected", optionId: "no" } } }]);
    await transport.stop();
    await core.disconnect();
  } finally {
    device.end(true);
    broker.kill();
    await new Promise((resolve) => broker.once("exit", resolve));
  }
});

test("MQTT transport carries session-local usage and a query response", async () => {
  const port = await freePort();
  const broker = spawn("mosquitto", ["-c", "/dev/null", "-p", String(port)], { stdio: "ignore" });
  const brokerUrl = `mqtt://127.0.0.1:${port}`;
  const base = "deskmate/mqtt-usage";
  await waitForBroker(port);
  const device = mqtt.connect(brokerUrl, { protocolVersion: 5, clean: true, properties: { sessionExpiryInterval: 0 } });
  try {
    await waitFor(device, "connect");
    const received = [];
    await new Promise((resolve, reject) => device.subscribe(`${base}/bridge-to-device`, { qos: 0 }, (error) => error ? reject(error) : resolve()));
    device.on("message", (_topic, payload) => received.push(JSON.parse(payload.toString("utf8"))));
    const adapter = new FakeAcpAdapter();
    let transport;
    const core = new DeskMateBridgeCore({ adapter, publish: (message) => transport.send(message) });
    transport = new MqttTransport({ brokerUrl, topicPrefix: "deskmate", deviceId: "mqtt-usage", clientId: "bridge-usage-test" });
    await transport.start({ onMessage: (message) => core.receive(message), onLinkState: (state) => { if (state === "down") core.disconnect(); } });
    await adapter.start();
    await new Promise((resolve, reject) => device.publish(`${base}/device-to-bridge`, JSON.stringify(hello("usage-nonce")), { qos: 0, retain: false }, (error) => error ? reject(error) : resolve()));
    await waitForMessage(received, (message) => message.type === "usage_snapshot");
    adapter.usage({ charge: { kind: "credit", amount: 0.375, unit: "credits" } });
    const observed = await waitForMessage(received, (message) => message.type === "usage_snapshot" && message.usage.billing.charge?.amount === 0.375);
    await new Promise((resolve, reject) => device.publish(`${base}/device-to-bridge`, JSON.stringify(usageQuery("usage-nonce", observed.epoch)), { qos: 0, retain: false }, (error) => error ? reject(error) : resolve()));
    const queried = await waitForMessage(received, (message) => message.type === "usage_snapshot" && message.usage_seq > observed.usage_seq);
    assert.deepEqual(queried.usage, observed.usage);
    await transport.stop();
    await core.disconnect();
  } finally {
    device.end(true);
    broker.kill();
    await new Promise((resolve) => broker.once("exit", resolve));
  }
});

test("MQTT transport carries one device chat turn and streamed output", async () => {
  const port = await freePort();
  const broker = spawn("mosquitto", ["-c", "/dev/null", "-p", String(port)], { stdio: "ignore" });
  const brokerUrl = `mqtt://127.0.0.1:${port}`;
  const base = "deskmate/mqtt-chat";
  await waitForBroker(port);
  const device = mqtt.connect(brokerUrl, { protocolVersion: 5, clean: true, properties: { sessionExpiryInterval: 0 } });
  try {
    await waitFor(device, "connect");
    const received = [];
    await new Promise((resolve, reject) => device.subscribe(`${base}/bridge-to-device`, { qos: 0 }, (error) => error ? reject(error) : resolve()));
    device.on("message", (_topic, payload) => received.push(JSON.parse(payload.toString("utf8"))));
    const adapter = new FakeAcpAdapter();
    let complete;
    adapter.submitHandler = () => new Promise((resolve) => { complete = resolve; });
    let transport;
    const core = new DeskMateBridgeCore({ adapter, publish: (message) => transport.send(message), outputFlushMs: 1 });
    transport = new MqttTransport({ brokerUrl, topicPrefix: "deskmate", deviceId: "mqtt-chat", clientId: "bridge-chat-test" });
    await transport.start({ onMessage: (message) => core.receive(message), onLinkState: (state) => { if (state === "down") core.disconnect(); } });
    await new Promise((resolve, reject) => device.publish(`${base}/device-to-bridge`, JSON.stringify(hello("chat-nonce")), { qos: 0, retain: false }, (error) => error ? reject(error) : resolve()));
    const snapshot = await waitForMessage(received, (message) => message.type === "snapshot");
    await new Promise((resolve, reject) => device.publish(`${base}/device-to-bridge`, JSON.stringify(promptSubmit("chat-nonce", snapshot.epoch, "request-1", "Summarize the project")), { qos: 0, retain: false }, (error) => error ? reject(error) : resolve()));
    const ack = await waitForMessage(received, (message) => message.type === "prompt_ack" && message.accepted);
    assert.equal(adapter.submits[0], "Summarize the project");
    adapter.text("A safe streamed answer\n");
    const output = await waitForMessage(received, (message) => message.type === "agent_output");
    assert.equal(output.turn_id, ack.turn_id);
    assert.equal(output.output_seq, 1);
    complete({ stopReason: "end_turn" });
    const result = await waitForMessage(received, (message) => message.type === "turn_result");
    assert.equal(result.status, "completed");
    await transport.stop();
    await core.disconnect();
  } finally {
    device.end(true);
    broker.kill();
    await new Promise((resolve) => broker.once("exit", resolve));
  }
});
