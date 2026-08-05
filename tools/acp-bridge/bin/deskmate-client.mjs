#!/usr/bin/env node
import process from "node:process";
import readline from "node:readline";
import { join } from "node:path";
import { nonce, hello, decision } from "../src/protocol.js";
import { LoopTransportClient } from "../src/transport.js";
import { MqttTransport } from "../src/mqtt-transport.js";

const args = process.argv.slice(2);
const index = args.indexOf("--socket");
const transportIndex = args.indexOf("--transport");
const brokerIndex = args.indexOf("--broker");
const topicPrefixIndex = args.indexOf("--topic-prefix");
const deviceIdIndex = args.indexOf("--device-id");
const transportName = transportIndex >= 0 ? args[transportIndex + 1] : "loop";
const socket = index >= 0 ? args[index + 1] : process.env.XDG_RUNTIME_DIR ? join(process.env.XDG_RUNTIME_DIR, "vela-deskmate", "deskmate-link.sock") : null;
const brokerUrl = brokerIndex >= 0 ? args[brokerIndex + 1] : process.env.DESKMATE_MQTT_BROKER;
const topicPrefix = topicPrefixIndex >= 0 ? args[topicPrefixIndex + 1] : process.env.DESKMATE_MQTT_TOPIC_PREFIX;
const deviceId = deviceIdIndex >= 0 ? args[deviceIdIndex + 1] : process.env.DESKMATE_DEVICE_ID;
if (transportName === "loop" && (!socket || !socket.startsWith("/"))) throw new Error("provide an absolute --socket path when XDG_RUNTIME_DIR is unavailable");
if (transportName === "mqtt" && (!brokerUrl || !topicPrefix || !deviceId)) throw new Error("MQTT requires --broker, --topic-prefix, and --device-id");
if (transportName !== "loop" && transportName !== "mqtt") throw new Error("--transport must be loop or mqtt");
const state = { nonce: nonce(), epoch: null, seq: 0, prompt: null, status: "Offline", msg: "" };
let lastSnapshotAt = 0;
const show = () => {
  console.log(`${state.status}: ${state.msg || "no current activity"}`);
  if (state.prompt) console.log(`  ATTENTION ${state.prompt.tool}: ${state.prompt.hint || "(no summary)"} [once=${state.prompt.canOnce}, deny=${state.prompt.canDeny}]`);
};
const client = transportName === "loop"
  ? new LoopTransportClient(socket)
  : new MqttTransport({ brokerUrl, topicPrefix, deviceId, clientId: "deskmate-terminal", role: "device", username: process.env.DESKMATE_MQTT_USERNAME, password: process.env.DESKMATE_MQTT_PASSWORD });
await client.start({
  onMessage(message) {
    if (message.type === "hello_ack" && message.connection_nonce === state.nonce) { state.epoch = message.epoch; state.seq = 0; return; }
    if (message.connection_nonce !== state.nonce || message.epoch !== state.epoch) return;
    if (message.type === "snapshot") {
      if (message.seq <= state.seq) return;
      if (state.seq && message.seq !== state.seq + 1) { state.status = "Offline"; state.prompt = null; state.epoch = null; client.send(hello(state.nonce)); return; }
      state.seq = message.seq; state.prompt = message.prompt; state.msg = message.msg;
      lastSnapshotAt = Date.now();
      state.status = message.prompt ? "Attention" : message.running > 0 ? "Busy" : "Idle"; show();
    } else if (message.type === "permission_result") console.log(`Permission decision ${message.accepted ? "accepted by ACP" : `failed: ${message.error}`}`);
    else if (message.type === "tool_result") { state.status = "Result"; state.msg = `${message.toolCallId}: ${message.status}`; show(); }
    else if (message.type === "error") console.log(`Link error: ${message.error}`);
  },
  onLinkState(status) { if (status === "down") { state.status = "Offline"; state.prompt = null; console.log("Disconnected"); } },
});
client.send(hello(state.nonce));
const liveness = setInterval(() => {
  if (state.epoch && lastSnapshotAt && Date.now() - lastSnapshotAt > 30_000) {
    state.status = "Offline"; state.prompt = null; state.msg = "snapshot timeout"; lastSnapshotAt = 0; show();
  }
}, 1_000);
const terminal = readline.createInterface({ input: process.stdin, output: process.stdout, prompt: "deskmate> " });
console.log("Commands: status, once, deny, disconnect, quit"); terminal.prompt();
terminal.on("line", (line) => {
  const command = line.trim();
  if (command === "status") show();
  else if (command === "once" || command === "deny") {
    if (!state.prompt || !state.epoch) console.log("No current permission request.");
    else if (command === "once" && !state.prompt.canOnce) console.log("Approve once is unavailable.");
    else if (command === "deny" && !state.prompt.canDeny) console.log("Deny is unavailable.");
    else client.send(decision(state.nonce, state.epoch, state.prompt.id, command));
  } else if (command === "disconnect") client.stop();
  else if (command === "quit" || command === "exit") { clearInterval(liveness); client.stop(); terminal.close(); }
  else if (command) console.log("Commands: status, once, deny, disconnect, quit");
  terminal.prompt();
});
