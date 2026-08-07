import { randomBytes } from "node:crypto";

export const VERSION = 1;
export const MAX_MESSAGE_BYTES = 4096;
export const MAX_PROMPT_TEXT_BYTES = 2048;
export const MAX_REQUEST_ID_BYTES = 40;
export const MAX_CONNECTION_NONCE_BYTES = 48;

export function byteLength(value) {
  return Buffer.byteLength(JSON.stringify(value), "utf8");
}

export function nonce() {
  return randomBytes(18).toString("base64url");
}

export function assertObject(message) {
  if (!message || typeof message !== "object" || Array.isArray(message)) {
    throw new TypeError("message must be a JSON object");
  }
  if (message.v !== undefined && message.v !== VERSION) {
    throw new TypeError("unsupported protocol version");
  }
  return message;
}

export function assertFits(message) {
  if (byteLength(message) > MAX_MESSAGE_BYTES) {
    throw new RangeError("message exceeds 4096 UTF-8 bytes");
  }
  return message;
}

export function encode(message) {
  return `${JSON.stringify(assertFits(message))}\n`;
}

export function hello(connectionNonce) {
  if (typeof connectionNonce !== "string" || !connectionNonce ||
      Buffer.byteLength(connectionNonce, "utf8") > MAX_CONNECTION_NONCE_BYTES) {
    throw new TypeError("connection_nonce required and limited to 48 UTF-8 bytes");
  }
  return { v: VERSION, cmd: "hello", connection_nonce: connectionNonce };
}

export function decision(connectionNonce, epoch, id, choice) {
  return { v: VERSION, cmd: "permission", connection_nonce: connectionNonce, epoch, id, decision: choice };
}

export function usageQuery(connectionNonce, epoch) {
  if (typeof connectionNonce !== "string" || !connectionNonce) throw new TypeError("connection_nonce required");
  if (typeof epoch !== "string" || !epoch) throw new TypeError("epoch required");
  return { v: VERSION, cmd: "usage_query", connection_nonce: connectionNonce, epoch };
}

export function promptSubmit(connectionNonce, epoch, requestId, text) {
  if (typeof connectionNonce !== "string" || !connectionNonce ||
      Buffer.byteLength(connectionNonce, "utf8") > MAX_CONNECTION_NONCE_BYTES) {
    throw new TypeError("connection_nonce required and limited to 48 UTF-8 bytes");
  }
  if (typeof epoch !== "string" || !epoch) throw new TypeError("epoch required");
  if (typeof requestId !== "string" || !requestId || Buffer.byteLength(requestId, "utf8") > MAX_REQUEST_ID_BYTES) {
    throw new TypeError("request_id required and limited to 40 UTF-8 bytes");
  }
  if (typeof text !== "string" || !text.trim() || Buffer.byteLength(text, "utf8") > MAX_PROMPT_TEXT_BYTES) {
    throw new TypeError("text required and limited to 2048 UTF-8 bytes");
  }
  return assertFits({ v: VERSION, cmd: "prompt_submit", connection_nonce: connectionNonce, epoch, request_id: requestId, text });
}
