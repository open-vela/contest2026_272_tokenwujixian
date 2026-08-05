import { randomBytes } from "node:crypto";

export const VERSION = 1;
export const MAX_MESSAGE_BYTES = 4096;

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
  if (typeof connectionNonce !== "string" || !connectionNonce) throw new TypeError("connection_nonce required");
  return { v: VERSION, cmd: "hello", connection_nonce: connectionNonce };
}

export function decision(connectionNonce, epoch, id, choice) {
  return { v: VERSION, cmd: "permission", connection_nonce: connectionNonce, epoch, id, decision: choice };
}
