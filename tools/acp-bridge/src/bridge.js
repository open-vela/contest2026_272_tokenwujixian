import { createHash } from "node:crypto";

const HINT_MAX_BYTES = 96;
const MESSAGE_MAX_BYTES = 128;
const PROMPT_ID_MAX_BYTES = 40;

const SENSITIVE_KEY = /(?:api[_-]?key|authorization|cookie|credential|password|secret|token)/i;
const SENSITIVE_TEXT = /\b(?:api[_ -]?key|authorization|cookie|credential|password|secret|token)\b\s*([:=])\s*(?:Bearer\s+)?[^\s,;]+/gi;
const SENSITIVE_JSON_DOUBLE = /("(?:api[_-]?key|authorization|cookie|credential|password|secret|token)"\s*:\s*)"(?:\\.|[^"\\])*"/gi;
const SENSITIVE_JSON_SINGLE = /('(?:api[_-]?key|authorization|cookie|credential|password|secret|token)'\s*:\s*)'(?:\\.|[^'\\])*'/gi;

function utf8Prefix(value, maxBytes) {
  const input = String(value ?? "");
  if (Buffer.byteLength(input, "utf8") <= maxBytes) {
    return input;
  }

  let output = "";
  for (const character of input) {
    if (Buffer.byteLength(output + character, "utf8") > maxBytes) {
      break;
    }
    output += character;
  }
  return output;
}

function utf8Summary(value, maxBytes) {
  const input = String(value ?? "");
  const marker = "…";

  if (Buffer.byteLength(input, "utf8") <= maxBytes) {
    return input;
  }

  return utf8Prefix(input, maxBytes - Buffer.byteLength(marker, "utf8")) + marker;
}

function redact(value, key = "") {
  if (SENSITIVE_KEY.test(key)) {
    return "[redacted]";
  }
  if (Array.isArray(value)) {
    return value.map((item) => redact(item));
  }
  if (value && typeof value === "object") {
    return Object.fromEntries(
      Object.entries(value).map(([name, item]) => [name, redact(item, name)]),
    );
  }
  return value;
}

function summarizeArguments(argumentsValue) {
  if (argumentsValue === undefined || argumentsValue === null) {
    return "";
  }
  const redacted = redact(argumentsValue);
  const rendered = typeof redacted === "string" ? redacted : JSON.stringify(redacted);
  return utf8Summary(rendered, HINT_MAX_BYTES);
}

function summarizeMessage(value) {
  const redacted = redact(value);
  const rendered = typeof redacted === "string" ? redacted : JSON.stringify(redacted);
  return utf8Summary(rendered, MESSAGE_MAX_BYTES);
}

/* Agent text is user-visible rather than a security boundary. This removes
 * terminal control bytes and obvious inline secrets before it crosses the
 * device link; it deliberately does not attempt to interpret ACP payloads. */
function sanitizeAgentText(value) {
  return String(value ?? "")
    .replace(/[\u0000-\u0009\u000b-\u001f\u007f-\u009f]/g, "")
    .replace(SENSITIVE_JSON_DOUBLE, '$1"[redacted]"')
    .replace(SENSITIVE_JSON_SINGLE, "$1'[redacted]'")
    .replace(SENSITIVE_TEXT, (_match, separator) => `secret${separator}[redacted]`);
}

function choiceOptionId(options, kind) {
  return options.find((option) => option?.kind === kind)?.optionId;
}

function promptIdFor(sessionId, requestId, toolCallId) {
  const source = `${sessionId}\u0000${String(requestId)}\u0000${toolCallId}`;
  const digest = createHash("sha256").update(source, "utf8").digest("base64url");
  return `p-${digest.slice(0, PROMPT_ID_MAX_BYTES - 2)}`;
}

function normalizePermission(params) {
  const toolCall = params?.toolCall ?? params?.tool_call ?? {};
  const options = Array.isArray(params?.options) ? params.options : [];
  const requestId = params?.requestId ?? params?.request_id;
  const sessionId = params?.sessionId ?? params?.session_id;
  const toolCallId = toolCall.toolCallId ?? toolCall.id ?? params?.toolCallId;

  if (typeof requestId !== "string" && typeof requestId !== "number") {
    throw new TypeError("request_permission requires a JSON-RPC request id");
  }
  if (typeof sessionId !== "string" || typeof toolCallId !== "string") {
    throw new TypeError("request_permission requires sessionId and toolCallId");
  }

  const allowOnce = choiceOptionId(options, "allow_once");
  const rejectOnce = choiceOptionId(options, "reject_once");
  const promptId = promptIdFor(sessionId, requestId, toolCallId);
  return {
    acpRequestId: requestId,
    prompt: {
      id: promptId,
      tool: utf8Prefix(typeof toolCall.title === "string" ? toolCall.title : "Tool call", 24),
      hint: summarizeArguments(toolCall.rawInput ?? toolCall.input ?? toolCall.arguments),
      canOnce: typeof allowOnce === "string",
      canDeny: typeof rejectOnce === "string",
    },
    optionIds: { allowOnce, rejectOnce },
  };
}

export class AcpBridge {
  #pending = null;
  #consumed = new Set();

  ptSubmit(params) {
    const pending = normalizePermission(params);
    this.#pending = pending;
    return {
      type: "snapshot",
      total: 1,
      running: 0,
      waiting: 1,
      msg: `approval required: ${pending.prompt.tool}`,
      prompt: pending.prompt,
    };
  }

  receiveDeviceDecision(decision) {
    const id = decision?.id;
    const choice = decision?.decision;
    if (choice !== "once" && choice !== "deny") {
      return { type: "permission_result", id, accepted: false, error: "invalid_decision" };
    }
    if (this.#consumed.has(id)) {
      return { type: "ignored", reason: "duplicate_decision" };
    }
    if (!this.#pending || id !== this.#pending.prompt.id) {
      return { type: "permission_result", id, accepted: false, error: "unknown_or_stale_id" };
    }

    const optionId = choice === "once" ? this.#pending.optionIds.allowOnce : this.#pending.optionIds.rejectOnce;
    if (!optionId) {
      return { type: "permission_result", id, accepted: false, error: "unsupported_decision" };
    }

    const pending = this.#pending;
    this.#consumed.add(id);
    this.#pending = null;
    return {
      type: "acp_response",
      id: pending.acpRequestId,
      result: { outcome: { outcome: "selected", optionId } },
      deviceResult: { type: "permission_result", id, accepted: true },
    };
  }

  rejectPending() {
    if (!this.#pending || this.#consumed.has(this.#pending.prompt.id) || !this.#pending.optionIds.rejectOnce) return null;
    const pending = this.#pending;
    this.#consumed.add(pending.prompt.id);
    this.#pending = null;
    return {
      type: "acp_response",
      id: pending.acpRequestId,
      result: { outcome: { outcome: "selected", optionId: pending.optionIds.rejectOnce } },
      deviceResult: { type: "permission_result", id: pending.prompt.id, accepted: false, error: "device_disconnected" },
    };
  }

  disconnect() {
    this.#pending = null;
  }
}

export { promptIdFor, sanitizeAgentText, summarizeArguments, summarizeMessage, utf8Prefix, utf8Summary };
