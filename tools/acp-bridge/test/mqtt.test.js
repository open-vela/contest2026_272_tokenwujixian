import test from "node:test";
import assert from "node:assert/strict";
import { spawn } from "node:child_process";
import net from "node:net";
import mqtt from "mqtt";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { MqttTransport } from "../src/mqtt-transport.js";
import { decision, hello } from "../src/protocol.js";

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
