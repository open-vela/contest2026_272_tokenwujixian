import { randomBytes } from "node:crypto";
import { dirname, isAbsolute } from "node:path";
import { mkdir, rename, rm, writeFile } from "node:fs/promises";

export function mimoAcpPort(value) {
  const port = typeof value === "number" ? value : Number(value);
  if (!Number.isInteger(port) || port < 1 || port > 65_535) {
    throw new TypeError("MiMo ACP port must be an integer from 1 through 65535");
  }
  return port;
}

export function mimoAttachUrl(port) {
  return `http://127.0.0.1:${mimoAcpPort(port)}`;
}

function validateReady(path, sessionId, url) {
  if (typeof path !== "string" || !isAbsolute(path)) {
    throw new TypeError("MiMo attach ready file must be an absolute path");
  }
  if (typeof sessionId !== "string" || !/^[A-Za-z0-9_-]{1,128}$/.test(sessionId)) {
    throw new TypeError("MiMo ACP session id is invalid");
  }
  if (typeof url !== "string" || !/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}$/.test(url)) {
    throw new TypeError("MiMo attach URL must use 127.0.0.1 and an explicit port");
  }
}

export async function publishMimoAttachReady(path, { sessionId, url }) {
  validateReady(path, sessionId, url);
  await mkdir(dirname(path), { recursive: true, mode: 0o700 });
  const temporary = `${path}.${process.pid}.${randomBytes(8).toString("hex")}.tmp`;
  const document = `${JSON.stringify({ v: 1, url, session_id: sessionId })}\n`;
  try {
    await writeFile(temporary, document, { encoding: "utf8", mode: 0o600, flag: "wx" });
    await rename(temporary, path);
  } catch (error) {
    await rm(temporary, { force: true }).catch(() => {});
    throw error;
  }
}

export async function removeMimoAttachReady(path) {
  if (path) await rm(path, { force: true });
}
