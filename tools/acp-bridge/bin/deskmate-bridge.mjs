#!/usr/bin/env node
import process from "node:process";
import readline from "node:readline";
import { join } from "node:path";
import { rmSync } from "node:fs";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { adapterFor } from "../src/acp-adapter.js";
import { LoopTransportServer } from "../src/transport.js";
import { MqttTransport } from "../src/mqtt-transport.js";
import { bridgeHelp, formatUsage } from "../src/terminal-format.js";
import { mimoAcpPort, mimoAttachUrl, publishMimoAttachReady, removeMimoAttachReady } from "../src/mimo-attach.js";

const args = process.argv.slice(2);
const index = args.indexOf("--socket");
const backendIndex = args.indexOf("--backend");
const transportIndex = args.indexOf("--transport");
const brokerIndex = args.indexOf("--broker");
const topicPrefixIndex = args.indexOf("--topic-prefix");
const deviceIdIndex = args.indexOf("--device-id");
const mimoAcpPortIndex = args.indexOf("--mimo-acp-port");
const mimoAttachReadyFileIndex = args.indexOf("--mimo-attach-ready-file");
const backendName = backendIndex >= 0 ? args[backendIndex + 1] : "fake";
const transportName = transportIndex >= 0 ? args[transportIndex + 1] : "loop";
const runtime = process.env.XDG_RUNTIME_DIR;
const socket = index >= 0 ? args[index + 1] : runtime ? join(runtime, "vela-deskmate", "deskmate-link.sock") : null;
const brokerUrl = brokerIndex >= 0 ? args[brokerIndex + 1] : process.env.DESKMATE_MQTT_BROKER;
const topicPrefix = topicPrefixIndex >= 0 ? args[topicPrefixIndex + 1] : process.env.DESKMATE_MQTT_TOPIC_PREFIX;
const deviceId = deviceIdIndex >= 0 ? args[deviceIdIndex + 1] : process.env.DESKMATE_DEVICE_ID;
const attachPort = mimoAcpPortIndex >= 0 ? mimoAcpPort(args[mimoAcpPortIndex + 1]) : null;
const attachReadyFile = mimoAttachReadyFileIndex >= 0 ? args[mimoAttachReadyFileIndex + 1] : null;
if (transportName === "loop" && (!socket || !socket.startsWith("/"))) throw new Error("provide an absolute --socket path when XDG_RUNTIME_DIR is unavailable");
if (transportName === "mqtt" && (!brokerUrl || !topicPrefix || !deviceId)) throw new Error("MQTT requires --broker, --topic-prefix, and --device-id");
if (transportName !== "loop" && transportName !== "mqtt") throw new Error("--transport must be loop or mqtt");
if ((attachPort !== null || attachReadyFile !== null) && backendName !== "mimo") {
  throw new Error("MiMo attach options require --backend mimo");
}
if (attachReadyFile !== null && attachPort === null) {
  throw new Error("--mimo-attach-ready-file requires --mimo-acp-port");
}
process.once("exit", () => {
  if (attachReadyFile !== null) {
    try { rmSync(attachReadyFile, { force: true }); } catch {}
  }
});

const adapter = backendName === "fake"
  ? new FakeAcpAdapter()
  : adapterFor(backendName, {
    cwd: process.cwd(),
    ...(attachPort === null ? {} : { args: ["acp", "--port", String(attachPort)] }),
  });
let transport;
let terminal;
let closing = false;
let submitting = false;
let agentLineOpen = false;

function endAgentLine() {
  if (!agentLineOpen) return;
  process.stdout.write("\n");
  agentLineOpen = false;
}

function logBlock(lines) {
  endAgentLine();
  process.stdout.write(`${lines.join("\n")}\n`);
}

function logAsync(text) {
  endAgentLine();
  process.stdout.write(`\n${text}\n`);
}

function showPrompt() {
  if (!closing && !submitting) terminal.prompt();
}

function renderAdapterEvent(event, usage) {
  if (event.type === "text") {
    if (event.text === `${backendName} session ready`) return;
    if (!agentLineOpen) {
      process.stdout.write("\nagent> ");
      agentLineOpen = true;
    }
    process.stdout.write(event.text);
    return;
  }
  if (event.type === "session_started") logAsync(`[bridge] ${event.backend} ACP session ready`);
  else if (event.type === "permission_request") logAsync(`[permission] waiting for DeskMate device: ${event.params?.toolCall?.title ?? "tool call"}`);
  else if (event.type === "usage") logAsync(formatUsage(usage).map((line) => `[billing] ${line}`).join("\n"));
  showPrompt();
}

const core = new DeskMateBridgeCore({
  adapter,
  publish: (message) => transport?.send(message),
  onAdapterEvent: renderAdapterEvent,
});
transport = transportName === "loop"
  ? new LoopTransportServer(socket)
  : new MqttTransport({ brokerUrl, topicPrefix, deviceId, username: process.env.DESKMATE_MQTT_USERNAME, password: process.env.DESKMATE_MQTT_PASSWORD });
try {
  await adapter.start();
  if (attachReadyFile !== null) {
    const sessionId = adapter.attachSession?.();
    if (!sessionId) throw new Error("MiMo ACP session was not created");
    await publishMimoAttachReady(attachReadyFile, {
      url: mimoAttachUrl(attachPort), sessionId,
    });
  }
  await transport.start({
    onMessage: (message) => core.receive(message).catch((error) => transport.send({ v: 1, type: "error", error: error.message })),
    onLinkState: (state) => { if (state === "down") core.disconnect(); },
    onError: (error) => transport.send({ v: 1, type: "error", error }),
  });
} catch (error) {
  await adapter.stop().catch(() => {});
  await transport.stop().catch(() => {});
  await removeMimoAttachReady(attachReadyFile).catch(() => {});
  throw error;
}
process.stderr.write(`[deskmate-bridge] ${backendName} backend using ${transportName}${transportName === "loop" ? ` on ${socket}` : ` via ${brokerUrl}`}\n`);

const keepAlive = setInterval(() => core.snapshot(), 10_000);
const usageKeepAlive = setInterval(() => core.usageSnapshot(), 30_000);
const shutdown = async () => {
  if (closing) return;
  closing = true;
  clearInterval(keepAlive); clearInterval(usageKeepAlive);
  endAgentLine();
  await core.disconnect(); await adapter.stop(); await transport.stop();
  await removeMimoAttachReady(attachReadyFile).catch(() => {});
  process.exit(0);
};
process.on("SIGINT", shutdown); process.on("SIGTERM", shutdown);

terminal = readline.createInterface({ input: process.stdin, output: process.stdout, prompt: "deskmate> " });
endAgentLine();
logBlock(["Type help for bridge commands. Any other text is sent to the ACP backend."]);
terminal.prompt();
terminal.on("line", async (line) => {
  const input = line.trim();
  if (!input) return showPrompt();
  if (input === "help") {
    logBlock(bridgeHelp());
    return showPrompt();
  }
  if (input === "status") {
    logBlock([`[bridge] backend=${backendName} transport=${transportName} request=${submitting ? "running" : "idle"}`]);
    return showPrompt();
  }
  if (input === "usage") {
    logBlock(formatUsage(core.currentUsage()).map((line) => `[billing] ${line}`));
    return showPrompt();
  }
  if (input === "quit" || input === "exit") return shutdown();
  if (submitting) {
    logBlock(["[bridge] a request is already running; wait for its turn result"]);
    return showPrompt();
  }
  if (backendName === "fake") {
    if (input !== "prompt") {
      logBlock(["[bridge] fake backend accepts only: prompt"]);
      return showPrompt();
    }
    adapter.requestPermission({ requestId: "fake-1", sessionId: "fake-session", toolCall: { toolCallId: "fake-tool", title: "Run demo", rawInput: { command: "echo safe", token: "hidden" } }, options: [{ kind: "allow_once", optionId: "allow-1" }, { kind: "reject_once", optionId: "deny-1" }] });
    return showPrompt();
  }
  submitting = true;
  try {
    const result = await core.submitLocal(input);
    endAgentLine();
    logBlock([`[turn] ${result.stopReason ?? "completed"}`]);
  } catch (error) {
    endAgentLine();
    logBlock([`[error] ${error.message ?? "ACP request failed"}`]);
  } finally {
    submitting = false;
    showPrompt();
  }
});
