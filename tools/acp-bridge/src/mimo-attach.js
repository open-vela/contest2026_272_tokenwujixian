import { randomBytes } from "node:crypto";
import { existsSync } from "node:fs";
import { homedir } from "node:os";
import { dirname, isAbsolute, join } from "node:path";
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

function validateTarget(sessionId, url) {
  if (typeof sessionId !== "string" || !/^[A-Za-z0-9_-]{1,128}$/.test(sessionId)) {
    throw new TypeError("MiMo ACP session id is invalid");
  }
  if (typeof url !== "string" || !/^http:\/\/127\.0\.0\.1:[1-9][0-9]{0,4}$/.test(url)) {
    throw new TypeError("MiMo attach URL must use 127.0.0.1 and an explicit port");
  }
}

function validateReady(path, sessionId, url) {
  if (typeof path !== "string" || !isAbsolute(path)) {
    throw new TypeError("MiMo attach ready file must be an absolute path");
  }
  validateTarget(sessionId, url);
}

/* Resolve the mimo CLI the same way the demo script does, but without
 * relying on the bridge's PATH: an explicit override wins, then the
 * per-user install, then a bare name for PATH lookup. */
export function resolveMimoBinary({ env = process.env, home = homedir(), exists = existsSync } = {}) {
  const override = typeof env.DESKMATE_MIMO_BIN === "string" ? env.DESKMATE_MIMO_BIN.trim() : "";
  if (override !== "") return override;
  const bundled = join(home, ".mimocode", "bin", "mimo");
  if (exists(bundled)) return bundled;
  return "mimo";
}

/* Command used to pop up a terminal already attached to a MiMo session.
 * Kept pure so the bridge can spawn it and tests can assert the shape. */
export function mimoAttachWindowSpec({ url, sessionId, bin = "mimo", terminal = "gnome-terminal", title = "DeskMate MiMo" } = {}) {
  validateTarget(sessionId, url);
  if (typeof bin !== "string" || bin.trim() === "") {
    throw new TypeError("MiMo binary must be a non-empty string");
  }
  if (typeof terminal !== "string" || terminal.trim() === "") {
    throw new TypeError("terminal command must be a non-empty string");
  }
  return {
    command: terminal,
    args: [`--title=${title}`, "--", bin, "attach", url, "--session", sessionId],
  };
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
