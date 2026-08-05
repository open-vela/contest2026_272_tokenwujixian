#!/usr/bin/env node
import process from "node:process";
import { join } from "node:path";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { adapterFor } from "../src/acp-adapter.js";
import { LoopTransportServer } from "../src/transport.js";
import { MqttTransport } from "../src/mqtt-transport.js";

const args = process.argv.slice(2);
const index = args.indexOf("--socket");
const backendIndex = args.indexOf("--backend");
const transportIndex = args.indexOf("--transport");
const brokerIndex = args.indexOf("--broker");
const topicPrefixIndex = args.indexOf("--topic-prefix");
const deviceIdIndex = args.indexOf("--device-id");
const backendName = backendIndex >= 0 ? args[backendIndex + 1] : "fake";
const transportName = transportIndex >= 0 ? args[transportIndex + 1] : "loop";
const runtime = process.env.XDG_RUNTIME_DIR;
const socket = index >= 0 ? args[index + 1] : runtime ? join(runtime, "vela-deskmate", "deskmate-link.sock") : null;
const brokerUrl = brokerIndex >= 0 ? args[brokerIndex + 1] : process.env.DESKMATE_MQTT_BROKER;
const topicPrefix = topicPrefixIndex >= 0 ? args[topicPrefixIndex + 1] : process.env.DESKMATE_MQTT_TOPIC_PREFIX;
const deviceId = deviceIdIndex >= 0 ? args[deviceIdIndex + 1] : process.env.DESKMATE_DEVICE_ID;
if (transportName === "loop" && (!socket || !socket.startsWith("/"))) throw new Error("provide an absolute --socket path when XDG_RUNTIME_DIR is unavailable");
if (transportName === "mqtt" && (!brokerUrl || !topicPrefix || !deviceId)) throw new Error("MQTT requires --broker, --topic-prefix, and --device-id");
if (transportName !== "loop" && transportName !== "mqtt") throw new Error("--transport must be loop or mqtt");

const adapter = backendName === "fake" ? new FakeAcpAdapter() : adapterFor(backendName, { cwd: process.cwd() });
let transport;
const core = new DeskMateBridgeCore({ adapter, publish: (message) => transport.send(message) });
transport = transportName === "loop"
  ? new LoopTransportServer(socket)
  : new MqttTransport({ brokerUrl, topicPrefix, deviceId, username: process.env.DESKMATE_MQTT_USERNAME, password: process.env.DESKMATE_MQTT_PASSWORD });
await transport.start({
  onMessage: (message) => core.receive(message).catch((error) => transport.send({ v: 1, type: "error", error: error.message })),
  onLinkState: (state) => { if (state === "down") core.disconnect(); },
  onError: (error) => transport.send({ v: 1, type: "error", error }),
});
await adapter.start();
process.stderr.write(`[deskmate-bridge] ${backendName} backend using ${transportName}${transportName === "loop" ? ` on ${socket}` : ` via ${brokerUrl}`}\n`);

const keepAlive = setInterval(() => core.snapshot(), 10_000);
const shutdown = async () => { clearInterval(keepAlive); await core.disconnect(); await adapter.stop(); await transport.stop(); process.exit(0); };
process.on("SIGINT", shutdown); process.on("SIGTERM", shutdown);

// Demo-only hook: deterministic fake permission, never present in a real ACP adapter.
process.stdin.on("data", (chunk) => {
  if (backendName !== "fake" && chunk.toString("utf8").trim()) adapter.submit(chunk.toString("utf8").trim()).catch(() => {});
  if (backendName !== "fake") return;
  if (chunk.toString("utf8").trim() !== "prompt") return;
  adapter.requestPermission({ requestId: "fake-1", sessionId: "fake-session", toolCall: { toolCallId: "fake-tool", title: "Run demo", rawInput: { command: "echo safe", token: "hidden" } }, options: [{ kind: "allow_once", optionId: "allow-1" }, { kind: "reject_once", optionId: "deny-1" }] });
});
