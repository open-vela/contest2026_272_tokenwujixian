import { createHash } from "node:crypto";

/* The device renders the prompt on a 240x296 round panel, so a hint has to be
 * short enough to read but long enough to decide on: 160 bytes is ~6-7 lines of
 * 16 px CJK text inside the approval card.  REMOTE_HINT_MAX in the firmware's
 * remote_link.h and async_approval_t.hint must stay >= this value. */
const HINT_MAX_BYTES = 160;
const TOOL_TITLE_MAX_BYTES = 40;
const MESSAGE_MAX_BYTES = 128;
const PROMPT_ID_MAX_BYTES = 40;

/* Fields a human needs in order to decide on a permission prompt, most
 * operative first.  ACP tools put the payload under different names depending
 * on the tool (shell / read / write / search / fetch), and the raw JSON dump we
 * used to show ("external filetory{...}") told the user nothing. */
const HINT_FIELDS = [
  "command", "cmd", "script",
  "path", "filePath", "file_path", "filename", "target", "files",
  "pattern", "query", "q", "url", "uri",
  "name", "description", "prompt", "text", "title",
];

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

/* Human-readable one-glance summary of a tool call's input.
 *
 * The device shows this under the tool title so a person can decide "approve or
 * deny" without opening a terminal.  A raw JSON dump was unreadable on a round
 * 240 px panel, so pick the operative fields (the shell command, the file path,
 * the search query ...) and join them with a middle dot.  Unknown shapes fall
 * back to `key=value` pairs, and only then to JSON. */
/* ACP's `title` is what the agent itself would show in its own UI, so prefer
 * it; `kind` (e.g. "execute", "edit", "fetch") is the fallback when a backend
 * sends no title, which is how we used to end up with a generic label. */
function toolLabel(toolCall) {
  const title = typeof toolCall?.title === "string" ? toolCall.title.trim() : "";
  if (title) {
    return title;
  }
  const kind = typeof toolCall?.kind === "string" ? toolCall.kind.trim() : "";
  if (kind) {
    return kind;
  }
  return "Tool call";
}

function summarizeArguments(argumentsValue, maxBytes = HINT_MAX_BYTES) {
  if (argumentsValue === undefined || argumentsValue === null) {
    return "";
  }

  const redacted = redact(argumentsValue);
  const clean = (value) => sanitizeAgentText(String(value)).replace(/\s+/g, " ").trim();

  if (typeof redacted === "string") {
    return utf8Summary(clean(redacted), maxBytes);
  }
  if (Array.isArray(redacted)) {
    const joined = redacted.map((item) => (typeof item === "string" ? item : "")).filter(Boolean).join(" · ");
    return utf8Summary(clean(joined || JSON.stringify(redacted)), maxBytes);
  }

  const parts = [];
  for (const field of HINT_FIELDS) {
    const value = redacted[field];
    let text = "";
    if (typeof value === "string") {
      text = value;
    } else if (Array.isArray(value)) {
      text = value.filter((item) => typeof item === "string").join(" ");
    }
    if (!text.trim()) {
      continue;
    }
    parts.push(clean(text));
    if (Buffer.byteLength(parts.join(" · "), "utf8") >= maxBytes) {
      break;
    }
  }

  if (parts.length === 0) {
    for (const [key, value] of Object.entries(redacted)) {
      if (["string", "number", "boolean"].includes(typeof value) && String(value).trim() !== "") {
        parts.push(`${key}=${value}`);
        if (parts.length >= 4) {
          break;
        }
      }
    }
  }

  const rendered = parts.length > 0 ? parts.join(" · ") : JSON.stringify(redacted) ?? "";
  return utf8Summary(clean(rendered), maxBytes);
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
      tool: utf8Prefix(toolLabel(toolCall), TOOL_TITLE_MAX_BYTES),
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
