import { randomBytes } from "node:crypto";
import { AcpBridge, sanitizeAgentText, summarizeMessage } from "./bridge.js";
import { assertFits, assertObject, byteLength, MAX_CONNECTION_NONCE_BYTES, MAX_PROMPT_TEXT_BYTES, MAX_REQUEST_ID_BYTES, VERSION } from "./protocol.js";
import { SessionUsageLedger } from "./session-usage.js";

function epoch() { return randomBytes(12).toString("base64url"); }
function turnId() { return `t-${randomBytes(18).toString("base64url")}`; }

function validText(value) {
  return typeof value === "string" && value.trim().length > 0 &&
    Buffer.byteLength(value, "utf8") <= MAX_PROMPT_TEXT_BYTES;
}

function validRequestId(value) {
  return typeof value === "string" && value.length > 0 &&
    Buffer.byteLength(value, "utf8") <= MAX_REQUEST_ID_BYTES;
}

function splitOutput(message) {
  const pieces = [];
  let piece = "";
  for (const character of message) {
    const candidate = piece + character;
    /* JSON may double every source byte through escaping. Keeping source
     * chunks at 1600 bytes leaves space for all protocol fields under 4 KiB. */
    if (candidate !== piece && Buffer.byteLength(candidate, "utf8") <= 1600) {
      piece = candidate;
    } else {
      if (piece) pieces.push(piece);
      piece = character;
    }
  }
  if (piece) pieces.push(piece);
  return pieces;
}

export class DeskMateBridgeCore {
  constructor({ adapter, publish, clock = () => Date.now(), permissionTimeoutMs = 30_000, outputFlushMs = 40, onAdapterEvent = () => {} }) {
    this.adapter = adapter;
    this.publish = publish;
    this.clock = clock;
    this.permissions = new AcpBridge();
    this.epoch = epoch();
    this.permissionTimeoutMs = permissionTimeoutMs;
    this.permissionTimer = null;
    this.textBuffer = "";
    this.textTimer = null;
    this.outputBuffer = "";
    this.outputTimer = null;
    this.outputFlushMs = outputFlushMs;
    this.activeTurn = null;
    this.requestAcks = new Map();
    this.seq = 0;
    this.usageSeq = 0;
    this.usageReady = false;
    this.nonce = null;
    this.linkActive = false;
    this.facts = { total: 1, running: 0, waiting: 0, msg: "Agent offline", prompt: null };
    this.usage = new SessionUsageLedger({ backend: adapter.backend, clock });
    this.onAdapterEvent = onAdapterEvent;
    this.adapter.onEvent((event) => this.#onAdapterEvent(event));
  }

  currentUsage() { return this.usageReady ? this.usage.snapshot() : null; }

  async connect(message) {
    assertObject(message);
    if (message.cmd !== "hello" || typeof message.connection_nonce !== "string" ||
        !message.connection_nonce ||
        Buffer.byteLength(message.connection_nonce, "utf8") > MAX_CONNECTION_NONCE_BYTES) {
      throw new TypeError("hello required");
    }
    await this.disconnect();
    this.nonce = message.connection_nonce;
    this.linkActive = true;
    this.seq = 0;
    this.usageSeq = 0;
    this.requestAcks.clear();
    this.facts.prompt = null;
    this.facts.waiting = 0;
    this.publish({ v: VERSION, type: "hello_ack", connection_nonce: this.nonce, epoch: this.epoch, capabilities: ["chat_turns"] });
    this.snapshot();
    this.usageSnapshot();
  }

  snapshot(extra = {}) {
    if (!this.linkActive || !this.nonce) return;
    const message = {
      v: VERSION, type: "snapshot", connection_nonce: this.nonce, epoch: this.epoch, seq: ++this.seq,
      total: this.facts.total, running: this.facts.running, waiting: this.facts.waiting,
      msg: this.facts.msg, prompt: this.facts.prompt, ...extra,
    };
    message.msg = summarizeMessage(message.msg);
    this.publish(assertFits(message));
  }

  usageSnapshot() {
    if (!this.linkActive || !this.nonce || !this.usageReady) return;
    try {
      this.publish(assertFits({
        v: VERSION,
        type: "usage_snapshot",
        connection_nonce: this.nonce,
        epoch: this.epoch,
        usage_seq: ++this.usageSeq,
        usage: this.usage.snapshot(),
      }));
    } catch {
      // Telemetry is optional and must never interrupt permission/liveness.
    }
  }

  async receive(message) {
    assertObject(message);
    assertFits(message);
    if (message.cmd === "hello") return this.connect(message);
    if (!this.linkActive) return;
    if (message.cmd === "usage_query") {
      if (message.connection_nonce === this.nonce && message.epoch === this.epoch) this.usageSnapshot();
      return;
    }
    if (message.cmd === "prompt_submit") return this.#receivePrompt(message);
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
      this.facts.running = this.activeTurn ? 1 : 0;
      this.facts.msg = "Permission decision accepted by ACP";
      this.#result(result.deviceResult.id, true);
      this.snapshot();
    } catch {
      this.#result(result.deviceResult.id, false, "acp_rejected");
    }
  }

  async disconnect() {
    this.linkActive = false;
    clearTimeout(this.textTimer);
    this.textTimer = null;
    this.textBuffer = "";
    clearTimeout(this.outputTimer);
    this.outputTimer = null;
    this.outputBuffer = "";
    if (this.activeTurn?.device && this.activeTurn.nonce === this.nonce) {
      this.activeTurn.detached = true;
    }
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

  submitLocal(text) {
    if (!validText(text)) throw new TypeError("text required and limited to 2048 UTF-8 bytes");
    if (this.activeTurn || this.facts.waiting > 0) throw new Error("turn_busy");
    return this.#startTurn({ text, device: false }).completion;
  }

  #receivePrompt(message) {
    if (!this.linkActive) return;
    if (message.connection_nonce !== this.nonce || message.epoch !== this.epoch) {
      return this.#promptAck(message.request_id, false, "stale_connection");
    }
    if (!validRequestId(message.request_id)) return this.#promptAck(message.request_id, false, "invalid_request_id");
    if (!validText(message.text)) return this.#promptAck(message.request_id, false, "invalid_text");

    const previous = this.requestAcks.get(message.request_id);
    if (previous) {
      if (previous.text !== message.text) return this.#promptAck(message.request_id, false, "request_conflict");
      this.#publishPromptAck(previous);
      if (previous.result) this.#publishTurnResult(previous.result);
      return;
    }
    if (this.activeTurn || this.facts.waiting > 0) return this.#promptAck(message.request_id, false, "turn_busy");

    const started = this.#startTurn({
      text: message.text,
      device: true,
      requestId: message.request_id,
      nonce: message.connection_nonce,
      epoch: message.epoch,
    });
    const ack = { requestId: message.request_id, turnId: started.turn.id, accepted: true, text: message.text };
    this.requestAcks.set(message.request_id, ack);
    this.#publishPromptAck(ack);
  }

  #startTurn({ text, device, requestId = null, nonce = null, epoch: turnEpoch = null }) {
    const turn = {
      id: turnId(), device, requestId, nonce, epoch: turnEpoch, outputSeq: 0, detached: false,
    };
    this.activeTurn = turn;
    this.facts.running = 1;
    this.facts.msg = "Agent working";
    if (this.nonce) this.snapshot();
    const completion = Promise.resolve().then(() => this.adapter.submit(text));
    completion.then(
      (result) => this.#finishTurn(turn, "completed", result?.stopReason),
      () => this.#finishTurn(turn, "failed", "backend_unavailable"),
    );
    return { turn, completion };
  }

  #finishTurn(turn, status, error) {
    this.#flushOutput(turn);
    if (this.activeTurn !== turn) return;
    this.activeTurn = null;
    this.facts.running = 0;
    this.facts.msg = status === "completed" ? "Agent turn completed" : "Agent turn failed";
    if (this.nonce) this.snapshot();
    if (this.#turnConnected(turn)) {
      const result = {
        v: VERSION, type: "turn_result", connection_nonce: turn.nonce, epoch: turn.epoch,
        request_id: turn.requestId, turn_id: turn.id, status,
        ...(status === "failed" ? { error } : {}),
      };
      const request = this.requestAcks.get(turn.requestId);
      if (request) request.result = result;
      this.#publishTurnResult(result);
    }
  }

  #promptAck(requestId, accepted, error) {
    if (typeof requestId !== "string" ||
        Buffer.byteLength(requestId, "utf8") > MAX_REQUEST_ID_BYTES ||
        !this.linkActive || !this.nonce) return;
    this.publish(assertFits({
      v: VERSION, type: "prompt_ack", connection_nonce: this.nonce, epoch: this.epoch,
      request_id: requestId, accepted, ...(error ? { error } : {}),
    }));
  }

  #publishPromptAck(ack) {
    if (!this.linkActive || !this.nonce) return;
    this.publish(assertFits({
      v: VERSION, type: "prompt_ack", connection_nonce: this.nonce, epoch: this.epoch,
      request_id: ack.requestId, accepted: ack.accepted,
      ...(ack.accepted ? { turn_id: ack.turnId } : {}),
    }));
  }

  #publishTurnResult(result) {
    if (this.linkActive && result.connection_nonce === this.nonce && result.epoch === this.epoch) {
      this.publish(assertFits(result));
    }
  }

  #turnConnected(turn) {
    return turn.device && !turn.detached && turn.nonce === this.nonce && turn.epoch === this.epoch;
  }

  #queueOutput(text) {
    const turn = this.activeTurn;
    if (!turn || !this.#turnConnected(turn)) return;
    this.outputBuffer += sanitizeAgentText(text);
    if (!this.outputBuffer || this.outputTimer) return;
    this.outputTimer = setTimeout(() => this.#flushOutput(turn), this.outputFlushMs);
    this.outputTimer.unref?.();
  }

  #flushOutput(turn = this.activeTurn) {
    if (!this.outputBuffer || !turn || this.activeTurn !== turn) return;
    clearTimeout(this.outputTimer);
    this.outputTimer = null;
    const text = this.outputBuffer;
    this.outputBuffer = "";
    if (!this.#turnConnected(turn)) return;
    for (const chunk of splitOutput(text)) {
      const message = {
        v: VERSION, type: "agent_output", connection_nonce: turn.nonce, epoch: turn.epoch,
        request_id: turn.requestId, turn_id: turn.id, output_seq: ++turn.outputSeq, text: chunk,
      };
      if (byteLength(message) > 4096) throw new RangeError("agent output exceeds 4096 UTF-8 bytes");
      this.publish(message);
    }
  }

  #result(id, accepted, error) {
    if (!this.linkActive || !this.nonce || typeof id !== "string" ||
        Buffer.byteLength(id, "utf8") > MAX_REQUEST_ID_BYTES) return;
    try {
      this.publish(assertFits({
        v: VERSION, type: "permission_result", connection_nonce: this.nonce,
        epoch: this.epoch, id, accepted, ...(error ? { error } : {}),
      }));
    } catch {
      /* An invalid device ID must not make bridge output exceed the link cap. */
    }
  }

  #onAdapterEvent(event) {
    try {
      if (event.type === "text") {
        event = { ...event, text: sanitizeAgentText(event.text) };
      }
      if (event.type === "session_started") {
        this.usage.reset(event.backend ?? this.adapter.backend);
        this.usageReady = true;
        this.usageSnapshot();
        return;
      }
      if (event.type === "usage") {
        this.usage.record(event);
        this.usageSnapshot();
        return;
      }
      if (event.type === "permission_request") {
        clearTimeout(this.permissionTimer);
        if (this.activeTurn?.device && this.activeTurn.detached) {
          /* The ACP turn belongs to a disconnected device. Do not rebind its
           * permission request to a later nonce; reject through ACP instead. */
          const orphaned = this.permissions.ptSubmit(event.params);
          const rejected = this.permissions.rejectPending();
          if (rejected) {
            this.adapter.respondPermission(rejected.id, rejected.result).catch(() => {});
          }
          return orphaned;
        }
        const snapshot = this.permissions.ptSubmit(event.params);
        this.facts = { ...this.facts, ...snapshot, msg: snapshot.msg };
        this.snapshot();
        this.permissionTimer = setTimeout(() => this.disconnect(), this.permissionTimeoutMs);
        return;
      }
      if (event.type === "tool") {
        if (this.activeTurn?.device && this.activeTurn.detached) {
          /* This tool belongs to a turn started by the disconnected nonce. */
          return;
        }
        clearTimeout(this.textTimer);
        this.textBuffer = "";
        this.facts.running = this.activeTurn ? 1 :
          (event.status === "completed" || event.status === "failed" ? 0 : 1);
        this.facts.msg = summarizeMessage(event.title || "Tool activity");
        this.snapshot();
        if ((event.status === "completed" || event.status === "failed") && this.nonce) {
          this.publish({ v: VERSION, type: "tool_result", connection_nonce: this.nonce, epoch: this.epoch, toolCallId: event.toolCallId, status: event.status });
        }
        return;
      }
      if (event.type === "text") {
        this.#queueOutput(event.text);
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
    } finally {
      try { this.onAdapterEvent(event, this.currentUsage()); } catch {}
    }
  }
}
