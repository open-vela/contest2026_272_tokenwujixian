import { spawn } from "node:child_process";
import readline from "node:readline";

const READY_DELAY_MS = 1_000;

function delay(milliseconds) { return new Promise((resolve) => setTimeout(resolve, milliseconds)); }

export class StdioAcpAdapter {
  constructor({ backend, command, args = ["acp"], cwd = process.cwd(), env = process.env }) {
    this.backend = backend;
    this.command = command;
    this.args = args;
    this.cwd = cwd;
    this.env = env;
    this.listener = () => {};
    this.pending = new Map();
    this.tools = new Map();
    this.nextId = 1;
    this.child = null;
  }

  onEvent(listener) { this.listener = listener; }

  async start() {
    this.child = spawn(this.command, this.args, { cwd: this.cwd, env: this.env, stdio: ["pipe", "pipe", "pipe"], detached: true });
    this.child.stderr.on("data", () => {}); // stderr is deliberately never forwarded to DeskMate Link.
    this.child.on("error", (error) => {
      for (const entry of this.pending.values()) entry.reject(error);
      this.pending.clear(); this.listener({ type: "text", text: "ACP backend unavailable" });
    });
    this.child.on("exit", () => {
      for (const entry of this.pending.values()) entry.reject(new Error("acp_process_exited"));
      this.pending.clear(); this.listener({ type: "text", text: "ACP backend disconnected" });
    });
    readline.createInterface({ input: this.child.stdout, crlfDelay: Infinity }).on("line", (line) => this.#receive(line));
    const initialized = await this.request("initialize", { protocolVersion: 1, clientCapabilities: {}, clientInfo: { name: "vela-deskmate", version: "0.1.0" } });
    await delay(READY_DELAY_MS);
    const created = await this.request("session/new", { cwd: this.cwd, mcpServers: [] });
    this.sessionId = created.sessionId;
    this.listener({ type: "session_started", backend: this.backend });
    this.listener({ type: "text", text: `${this.backend} session ready` });
    return initialized;
  }

  attachSession() {
    return typeof this.sessionId === "string" && this.sessionId
      ? this.sessionId
      : null;
  }

  async submit(text) {
    return this.request("session/prompt", { sessionId: this.sessionId, prompt: [{ type: "text", text }] });
  }

  async respondPermission(id, result) { this.send({ jsonrpc: "2.0", id, result }); }

  async stop() {
    if (!this.child?.pid) return;
    try { process.kill(-this.child.pid, "SIGTERM"); } catch { this.child.kill("SIGTERM"); }
  }

  request(method, params) {
    const id = this.nextId++;
    this.send({ jsonrpc: "2.0", id, method, params });
    return new Promise((resolve, reject) => {
      const timer = setTimeout(() => { this.pending.delete(id); reject(new Error(`${method}_timeout`)); }, 30_000);
      this.pending.set(id, { resolve, reject, timer, method, params });
    });
  }

  send(message) { this.child.stdin.write(`${JSON.stringify(message)}\n`); }

  #receive(line) {
    let message;
    try { message = JSON.parse(line); } catch { return; }
    if (message.id !== undefined && (message.result !== undefined || message.error !== undefined)) {
      const entry = this.pending.get(message.id); if (!entry) return;
      clearTimeout(entry.timer); this.pending.delete(message.id);
      if (message.error) entry.reject(new Error(message.error.message ?? "acp_error")); else {
        this.#recordPromptUsage(entry, message.result);
        entry.resolve(message.result);
      }
      return;
    }
    if (message.id !== undefined && message.method === "session/request_permission") {
      const toolCall = message.params?.toolCall ?? {};
      const cached = this.tools.get(toolCall.toolCallId);
      this.listener({ type: "permission_request", params: { ...message.params, requestId: message.id, toolCall: { ...cached, ...toolCall, rawInput: toolCall.rawInput ?? cached?.rawInput } } });
      return;
    }
    if (message.id !== undefined && message.method) {
      this.send({ jsonrpc: "2.0", id: message.id, error: { code: -32601, message: "DeskMate bridge does not implement this ACP client request" } });
      return;
    }
    if (message.method === "_kiro.dev/metadata") {
      this.#normalizeKiroMetadata(message.params);
      return;
    }
    if (message.method === "session/update" || message.method === "_kiro.dev/session/update") {
      this.#normalize(message.params?.update, message.params?.sessionId);
    }
  }

  #recordPromptUsage(entry, result) {
    if (entry.method !== "session/prompt") return;
    const usage = result?.usage;
    if (!usage || typeof usage !== "object") return;
    const input = usage.inputTokens;
    const output = usage.outputTokens;
    const total = usage.totalTokens;
    if (![input, output, total].every((value) => typeof value === "number" && Number.isFinite(value) && value >= 0)) return;
    this.listener({ type: "usage", tokens: { input, output, total } });
  }

  #normalizeKiroMetadata(params = {}) {
    if (params.sessionId && this.sessionId && params.sessionId !== this.sessionId) return;
    const values = Array.isArray(params.meteringUsage) ? params.meteringUsage : [];
    const credits = values.reduce((sum, entry) => {
      if (entry?.unit !== "credit" || typeof entry.value !== "number" || !Number.isFinite(entry.value) || entry.value < 0) return sum;
      return sum + entry.value;
    }, 0);
    const hasCredits = values.some((entry) => entry?.unit === "credit" && typeof entry.value === "number" && Number.isFinite(entry.value) && entry.value >= 0);
    const percentage = params.contextUsagePercentage;
    const context = typeof percentage === "number" && Number.isFinite(percentage) && percentage >= 0
      ? { unit: "percent", used: percentage, limit: 100 }
      : undefined;
    if (hasCredits || context) {
      this.listener({
        type: "usage",
        ...(hasCredits ? { charge: { kind: "credit", amount: credits, unit: "credits" } } : {}),
        ...(context ? { context } : {}),
      });
    }
  }

  #normalize(update = {}, sessionId) {
    const type = update.sessionUpdate;
    if (type === "agent_message_chunk") {
      const text = update.content?.text;
      if (typeof text === "string" && text) this.listener({ type: "text", text });
      return;
    }
    if (type === "usage_update") {
      const used = update.used;
      const limit = update.size;
      if (sessionId === this.sessionId && typeof used === "number" && Number.isFinite(used) && used >= 0 && typeof limit === "number" && Number.isFinite(limit) && limit >= 0) {
        this.listener({ type: "usage", context: { unit: "token", used, limit } });
      }
      return;
    }
    if (type === "tool_call" || type === "tool_call_update") {
      const previous = this.tools.get(update.toolCallId) ?? {};
      this.tools.set(update.toolCallId, { ...previous, ...update, rawInput: update.rawInput ?? previous.rawInput });
      const status = update.status;
      this.listener({ type: "tool", toolCallId: update.toolCallId, title: update.title ?? previous.title ?? "Tool activity", status: status === "completed" || status === "failed" ? status : "in_progress" });
    } else if (type === "tool_call_chunk") {
      const previous = this.tools.get(update.toolCallId) ?? {};
      this.tools.set(update.toolCallId, { ...previous, ...update });
      this.listener({ type: "tool", toolCallId: update.toolCallId, title: update.title ?? previous.title ?? "Tool activity", status: "in_progress" });
    }
  }
}

export function adapterFor(backend, options = {}) {
  if (backend === "mimo") return new StdioAcpAdapter({ backend, command: process.env.DESKMATE_MIMO_BIN || "mimo", ...options });
  if (backend === "kiro") return new StdioAcpAdapter({ backend, command: process.env.DESKMATE_KIRO_BIN || "kiro-cli", ...options });
  throw new TypeError("backend must be fake, mimo, or kiro");
}
