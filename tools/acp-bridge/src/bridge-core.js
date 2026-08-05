import { randomBytes } from "node:crypto";
import { AcpBridge, summarizeArguments, summarizeMessage } from "./bridge.js";
import { assertFits, assertObject, VERSION } from "./protocol.js";

function epoch() { return randomBytes(12).toString("base64url"); }

export class DeskMateBridgeCore {
  constructor({ adapter, publish, clock = () => Date.now(), permissionTimeoutMs = 30_000 }) {
    this.adapter = adapter;
    this.publish = publish;
    this.clock = clock;
    this.permissions = new AcpBridge();
    this.epoch = epoch();
    this.permissionTimeoutMs = permissionTimeoutMs;
    this.permissionTimer = null;
    this.textBuffer = "";
    this.textTimer = null;
    this.seq = 0;
    this.nonce = null;
    this.facts = { total: 1, running: 0, waiting: 0, msg: "Agent offline", prompt: null };
    this.adapter.onEvent((event) => this.#onAdapterEvent(event));
  }

  async connect(message) {
    assertObject(message);
    if (message.cmd !== "hello" || typeof message.connection_nonce !== "string") throw new TypeError("hello required");
    this.nonce = message.connection_nonce;
    this.seq = 0;
    await this.disconnect();
    this.facts.prompt = null;
    this.facts.waiting = 0;
    this.publish({ v: VERSION, type: "hello_ack", connection_nonce: this.nonce, epoch: this.epoch });
    this.snapshot();
  }

  snapshot(extra = {}) {
    if (!this.nonce) return;
    const message = {
      v: VERSION, type: "snapshot", connection_nonce: this.nonce, epoch: this.epoch, seq: ++this.seq,
      total: this.facts.total, running: this.facts.running, waiting: this.facts.waiting,
      msg: this.facts.msg, prompt: this.facts.prompt, ...extra,
    };
    message.msg = summarizeMessage(message.msg);
    this.publish(assertFits(message));
  }

  async receive(message) {
    assertObject(message);
    if (message.cmd === "hello") return this.connect(message);
    if (message.cmd !== "permission") throw new TypeError("unsupported command");
    if (message.connection_nonce !== this.nonce || message.epoch !== this.epoch) {
      return this.#result(message.id, false, "stale_connection");
    }
    const result = this.permissions.receiveDeviceDecision(message);
    if (result.type === "ignored") return;
    if (result.type !== "acp_response") return this.#result(message.id, false, result.error);
    try {
      clearTimeout(this.permissionTimer);
      this.permissionTimer = null;
      await this.adapter.respondPermission(result.id, result.result);
      this.facts.prompt = null;
      this.facts.waiting = 0;
      this.facts.msg = "Permission decision accepted by ACP";
      this.#result(result.deviceResult.id, true);
      this.snapshot();
    } catch {
      this.#result(result.deviceResult.id, false, "acp_rejected");
    }
  }

  async disconnect() {
    clearTimeout(this.textTimer);
    this.textTimer = null;
    this.textBuffer = "";
    clearTimeout(this.permissionTimer);
    this.permissionTimer = null;
    const rejected = this.permissions.rejectPending();
    if (rejected) {
      try { await this.adapter.respondPermission(rejected.id, rejected.result); } catch {}
    }
    this.permissions.disconnect();
    this.facts.prompt = null;
    this.facts.waiting = 0;
  }

  #result(id, accepted, error) {
    if (!this.nonce) return;
    this.publish({ v: VERSION, type: "permission_result", connection_nonce: this.nonce, epoch: this.epoch, id, accepted, ...(error ? { error } : {}) });
  }

  #onAdapterEvent(event) {
    if (event.type === "permission_request") {
      clearTimeout(this.permissionTimer);
      const snapshot = this.permissions.ptSubmit(event.params);
      this.facts = { ...this.facts, ...snapshot, msg: snapshot.msg };
      this.snapshot();
      this.permissionTimer = setTimeout(() => this.disconnect(), this.permissionTimeoutMs);
      return;
    }
    if (event.type === "tool") {
      clearTimeout(this.textTimer);
      this.textTimer = null;
      this.textBuffer = "";
      this.facts.running = event.status === "completed" || event.status === "failed" ? 0 : 1;
      this.facts.msg = summarizeMessage(event.title || "Tool activity");
      this.snapshot();
      if ((event.status === "completed" || event.status === "failed") && this.nonce) {
        this.publish({ v: VERSION, type: "tool_result", connection_nonce: this.nonce, epoch: this.epoch, toolCallId: event.toolCallId, status: event.status });
      }
      return;
    }
    if (event.type === "text") {
      this.textBuffer += event.text;
      if (this.textTimer) return;
      this.textTimer = setTimeout(() => {
        this.facts.msg = summarizeMessage(this.textBuffer);
        this.textBuffer = "";
        this.textTimer = null;
        this.snapshot();
      }, 120);
      this.textTimer.unref?.();
    }
  }
}
