#!/usr/bin/env node
import { spawn } from "node:child_process";
import { mkdtemp, rm } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, resolve } from "node:path";
import readline from "node:readline";
import { AcpBridge, summarizeArguments } from "./bridge.js";

const WORKSPACE = process.cwd();
const ACP_PROCESS_CWD = resolve(WORKSPACE, "../..");
const TIMEOUT_MS = 30_000;
const AGENT_READY_DELAY_MS = 1_000;
const USE_CURRENT_PROFILE = process.env.DESKMATE_USE_CURRENT_PROFILE === "1";
const PROBE_EXTERNAL_TEMP = process.env.DESKMATE_PROBE_EXTERNAL_TEMP === "1";
const bridge = new AcpBridge();
const events = [];
let permissionRequests = 0;
let nextId = 1;
const pending = new Map();
let child;

function record(type, detail = {}) {
  events.push({ type, ...detail });
}

function send(message) {
  child.stdin.write(`${JSON.stringify(message)}\n`);
}

function request(method, params) {
  const id = nextId++;
  send({ jsonrpc: "2.0", id, method, params });
  return new Promise((resolve, reject) => {
    const timer = setTimeout(() => {
      pending.delete(id);
      reject(new Error(`${method} timed out after ${TIMEOUT_MS}ms`));
    }, TIMEOUT_MS);
    pending.set(id, { resolve, reject, timer, method });
  });
}

function respond(id, result) {
  send({ jsonrpc: "2.0", id, result });
}

function respondError(id, code, message) {
  send({ jsonrpc: "2.0", id, error: { code, message } });
}

function delay(milliseconds) {
  return new Promise((resolve) => setTimeout(resolve, milliseconds));
}

function permissionDetails(params) {
  const toolCall = params?.toolCall ?? params?.tool_call ?? {};
  return {
    sessionId: params?.sessionId ?? params?.session_id,
    toolCallId: toolCall.toolCallId ?? toolCall.id ?? params?.toolCallId,
    title: toolCall.title ?? toolCall.name ?? "Tool call",
    hint: summarizeArguments(toolCall.rawInput ?? toolCall.input ?? toolCall.arguments),
    optionKinds: Array.isArray(params?.options) ? params.options.map((option) => option?.kind) : [],
  };
}

function chooseDenyOption(params) {
  return params?.options?.find((option) => option?.kind === "reject_once")?.optionId;
}

function updateSummary(update) {
  if (!update || typeof update !== "object") return { kind: typeof update };
  const type = update.sessionUpdate ?? update.type ?? update.kind ?? "unknown";
  const summary = { type, fields: Object.keys(update).sort() };
  for (const field of ["toolCallId", "title", "kind", "status"]) {
    if (typeof update[field] === "string") summary[field] = update[field];
  }
  if (update.rawInput !== undefined) summary.rawInput = summarizeArguments(update.rawInput);
  if (typeof update.content === "string") summary.content = summarizeArguments(update.content);
  if (typeof update.message === "string") summary.message = summarizeArguments(update.message);
  if (update.error && typeof update.error === "object") {
    summary.error = {
      code: update.error.code,
      message: summarizeArguments(update.error.message),
    };
  }
  return summary;
}

async function handleServerRequest(message) {
  if (message.method === "session/request_permission") {
    permissionRequests += 1;
    const details = permissionDetails(message.params);
    record("permission_request", details);
    const snapshot = bridge.ptSubmit({ ...message.params, requestId: message.id });
    record("deskmate_snapshot", { prompt: snapshot.prompt });

    const optionId = chooseDenyOption(message.params);
    if (!optionId) {
      respondError(message.id, -32602, "request_permission did not offer reject_once");
      record("permission_response", { accepted: false, error: "missing_reject_once" });
      return;
    }
    respond(message.id, { outcome: { outcome: "selected", optionId } });
    record("permission_response", { accepted: true, decision: "deny", optionId });
    return;
  }

  respondError(message.id, -32601, `unsupported server request: ${message.method}`);
  record("unsupported_server_request", { method: message.method });
}

async function run() {
  const isolatedHome = USE_CURRENT_PROFILE ? null : await mkdtemp(join(tmpdir(), "deskmate-mimo-acp-"));
  const externalTemp = PROBE_EXTERNAL_TEMP ? await mkdtemp(join(tmpdir(), "deskmate-acp-external-")) : null;
  const acpArguments = ["acp", "--log-level", "ERROR", "--cwd", ACP_PROCESS_CWD];
  if (!USE_CURRENT_PROFILE) acpArguments.splice(1, 0, "--pure");
  child = spawn("mimo", acpArguments, {
    cwd: ACP_PROCESS_CWD,
    env: {
      ...process.env,
      ...(USE_CURRENT_PROFILE ? {} : { MIMOCODE_PURE: "true" }),
      ...(isolatedHome ? { MIMOCODE_HOME: isolatedHome } : {}),
    },
    stdio: ["pipe", "pipe", "pipe"],
  });

  const stderr = [];
  child.stderr.setEncoding("utf8");
  child.stderr.on("data", (chunk) => stderr.push(chunk));
  child.on("error", (error) => record("agent_process_error", { message: error.message }));
  child.on("exit", (code, signal) => record("agent_process_exit", { code, signal }));
  const lines = readline.createInterface({ input: child.stdout, crlfDelay: Infinity });
  lines.on("line", (line) => {
    let message;
    try {
      message = JSON.parse(line);
    } catch {
      record("invalid_agent_json", { line });
      return;
    }
    if (Object.hasOwn(message, "id") && (Object.hasOwn(message, "result") || Object.hasOwn(message, "error"))) {
      const waiter = pending.get(message.id);
      if (!waiter) {
        record("unexpected_response", { id: message.id });
        return;
      }
      clearTimeout(waiter.timer);
      pending.delete(message.id);
      if (message.error) {
        const detail = message.error.data === undefined ? "" : ` (${JSON.stringify(message.error.data)})`;
        waiter.reject(new Error(`${waiter.method}: ${message.error.message ?? "unknown error"}${detail}`));
      }
      else waiter.resolve(message.result);
      return;
    }
    if (Object.hasOwn(message, "id") && message.method) {
      void handleServerRequest(message).catch((error) => {
        respondError(message.id, -32603, error.message);
        record("server_request_error", { method: message.method, error: error.message });
      });
      return;
    }
    if (message.method === "session/update") {
      record("session_update", { update: updateSummary(message.params?.update) });
      return;
    }
    record("notification", { method: message.method ?? "unknown" });
  });

  try {
    record("profile_mode", { currentProfile: USE_CURRENT_PROFILE, externalTempProbe: Boolean(externalTemp) });
    const initialized = await request("initialize", {
      protocolVersion: 1,
      clientCapabilities: {},
      clientInfo: { name: "vela-deskmate-acp-verifier", version: "0.1.0" },
    });
    record("initialized", { protocolVersion: initialized.protocolVersion, agent: initialized.agentInfo?.name });
    await delay(AGENT_READY_DELAY_MS);

    const created = await request("session/new", { cwd: WORKSPACE, mcpServers: [] });
    const sessionId = created.sessionId;
    if (typeof sessionId !== "string") throw new Error("session/new did not return sessionId");
    record("session_created", { sessionId });

    const probeText = externalTemp
      ? `Use the terminal tool to run exactly: ls -- ${externalTemp}`
      : "Use the terminal tool to run exactly: echo deskmate-acp-permission-probe";
    await request("session/prompt", {
      sessionId,
      prompt: [{ type: "text", text: probeText }],
    });
    record("prompt_finished", { sessionId });
    if (permissionRequests === 0) {
      throw new Error("agent completed the probe without session/request_permission");
    }
  } finally {
    child.stdin.end();
    child.kill();
    if (isolatedHome) await rm(isolatedHome, { recursive: true, force: true });
    if (externalTemp) await rm(externalTemp, { recursive: true, force: true });
  }

  process.stdout.write(`${JSON.stringify({ ok: true, events }, null, 2)}\n`);
  if (stderr.length) process.stderr.write(stderr.join(""));
}

run().catch((error) => {
  process.stderr.write(`ACP verifier failed: ${error.message}\n`);
  process.stdout.write(`${JSON.stringify({ ok: false, events }, null, 2)}\n`);
  process.exitCode = 1;
});
