import { createHash } from "node:crypto";

/* The device renders the prompt on a 240x296 round panel, so a hint has to be
 * short enough to read but long enough to decide on: the approval card scrolls,
 * so 240 bytes (~8 lines of 12 px CJK text) buys a whole shell command line
 * without making the common case scroll.  REMOTE_HINT_MAX in the firmware's
 * remote_link.h and async_approval_t.hint must stay >= this value. */
const HINT_MAX_BYTES = 240;
const TOOL_TITLE_MAX_BYTES = 40;
const MESSAGE_MAX_BYTES = 128;
const PROMPT_ID_MAX_BYTES = 40;

/* Fields a human needs in order to decide on a permission prompt, most
 * operative first.  ACP tools put the payload under different names depending
 * on the tool (shell / read / write / search / fetch), and the raw JSON dump we
 * used to show ("external filetory{...}") told the user nothing.
 *
 * MiMoCode's permission requests carry the permission's `metadata` here
 * (lowercase `filepath` / `parentDir`), which is a different spelling from the
 * toolCall shapes below — keep both. */
const HINT_FIELDS = [
  "command", "cmd", "script",
  "path", "filePath", "file_path", "filepath", "filename", "target", "files",
  "parentDir", "dir", "directory", "cwd",
  "pattern", "query", "q", "url", "uri",
  "name", "description", "prompt", "text", "title",
];

/* MiMoCode raises each permission kind as its own "tool" name, so without a
 * mapping the device would show `external_directory` / `bash_delete`.  The
 * panel is Chinese and its 12 px font is a 3990-codepoint subset, so these
 * labels are ASCII-free and every glyph was checked against
 * packages/ai_agent/src/ui/lv_font_misans_12_cjk.c.  Unknown kinds fall back to
 * the backend's own title. */
const PERMISSION_LABELS = {
  external_directory: "访问项目外目录",
  bash: "执行命令",
  bash_delete: "删除文件",
  edit: "修改文件",
  write: "写入文件",
  read: "读取文件",
  webfetch: "网络请求",
  fetch: "网络请求",
  websearch: "网络搜索",
  doom_loop: "工具循环保护",
};

/* Shown when the backend sends no arguments at all (the tool was still being
 * prepared when the permission fired), so the page explains WHAT is being
 * asked instead of rendering an empty body. */
const PERMISSION_HINTS = {
  external_directory: "命令将读取或写入项目目录之外的路径",
  bash: "即将在终端执行命令",
  bash_delete: "即将删除文件",
  edit: "即将修改文件",
  write: "即将写入文件",
  read: "即将读取文件",
  webfetch: "即将访问网络",
  fetch: "即将访问网络",
  websearch: "即将搜索网络",
  doom_loop: "工具调用出现循环",
};

/* Separator for several hint fragments.  The panel font has no U+00B7 middle
 * dot and no U+2026 ellipsis (they rendered as tofu boxes), so the bridge only
 * emits glyphs the device can draw: "、" and "..". */
const HINT_SEPARATOR = "、";
const ELLIPSIS = "..";

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

  if (Buffer.byteLength(input, "utf8") <= maxBytes) {
    return input;
  }

  return utf8Prefix(input, maxBytes - Buffer.byteLength(ELLIPSIS, "utf8")) + ELLIPSIS;
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
    const joined = redacted.map((item) => (typeof item === "string" ? item : "")).filter(Boolean).join(HINT_SEPARATOR);
    return utf8Summary(clean(joined || JSON.stringify(redacted)), maxBytes);
  }
  if (typeof redacted !== "object" || Object.keys(redacted).length === 0) {
    /* An empty payload has nothing to say.  Returning "{}" here is what put a
     * literal "{}" on the approval page: the caller now falls through to the
     * permission explanation instead. */
    return "";
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
    if (Buffer.byteLength(parts.join(HINT_SEPARATOR), "utf8") >= maxBytes) {
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

  const rendered = parts.length > 0 ? parts.join(HINT_SEPARATOR) : JSON.stringify(redacted) ?? "";
  return utf8Summary(clean(rendered), maxBytes);
}

/* ACP also reports where the tool will operate (the permission request carries
 * `locations`).  For paths that is often the only detail a backend sends, so
 * use it when the arguments themselves are empty. */
function summarizeLocations(locations, maxBytes = HINT_MAX_BYTES) {
  if (!Array.isArray(locations)) {
    return "";
  }
  const clean = (value) => sanitizeAgentText(String(value)).replace(/\s+/g, " ").trim();
  const paths = locations
    .map((item) => (typeof item === "string" ? item : item?.path ?? item?.uri ?? ""))
    .filter((item) => typeof item === "string" && item.trim() !== "")
    .map(clean);
  return paths.length > 0 ? utf8Summary(paths.join(HINT_SEPARATOR), maxBytes) : "";
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
  const title = toolLabel(toolCall);
  const kind = typeof toolCall.kind === "string" ? toolCall.kind.trim() : "";
  const label = PERMISSION_LABELS[title] ?? PERMISSION_LABELS[kind] ?? title;
  /* Detail order: what the tool will actually run / touch, then where it will
   * run, then (if the backend sent neither) what the permission itself means.
   * Never the raw JSON dump that produced "{}" on the panel. */
  const detail =
    summarizeArguments(toolCall.rawInput ?? toolCall.input ?? toolCall.arguments) ||
    summarizeLocations(toolCall.locations) ||
    PERMISSION_HINTS[title] ||
    PERMISSION_HINTS[kind] ||
    "";
  return {
    acpRequestId: requestId,
    prompt: {
      id: promptId,
      tool: utf8Prefix(label, TOOL_TITLE_MAX_BYTES),
      hint: detail,
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
