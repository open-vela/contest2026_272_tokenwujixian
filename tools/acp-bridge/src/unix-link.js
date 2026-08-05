import net from "node:net";
import { chmod, mkdir, rm, lstat } from "node:fs/promises";
import { dirname } from "node:path";
import { MAX_MESSAGE_BYTES, encode } from "./protocol.js";

function parser(socket, onMessage, onError) {
  let buffer = Buffer.alloc(0);
  let dropping = false;
  socket.on("data", (chunk) => {
    buffer = Buffer.concat([buffer, chunk]);
    while (true) {
      const newline = buffer.indexOf(10);
      if (newline < 0) {
        if (buffer.length > MAX_MESSAGE_BYTES) {
          dropping = true;
          buffer = Buffer.alloc(0);
        }
        return;
      }
      const line = buffer.subarray(0, newline);
      buffer = buffer.subarray(newline + 1);
      if (dropping) {
        dropping = false;
        onError("overlong_message");
        continue;
      }
      if (line.length > MAX_MESSAGE_BYTES) {
        onError("overlong_message");
        continue;
      }
      if (!line.length) continue;
      try {
        const message = JSON.parse(line.toString("utf8"));
        if (!message || typeof message !== "object" || Array.isArray(message)) throw new Error("not_object");
        onMessage(message);
      } catch {
        onError("invalid_json");
      }
    }
  });
}

export class UnixLinkServer {
  constructor(path, handlers) {
    this.path = path;
    this.handlers = handlers;
    this.server = null;
    this.socket = null;
    this.ownsPath = false;
  }

  async listen() {
    await mkdir(dirname(this.path), { recursive: true, mode: 0o700 });
    try {
      await lstat(this.path);
      throw new Error(`socket path already exists: ${this.path}`);
    } catch (error) {
      if (error.code !== "ENOENT") throw error;
    }
    this.server = net.createServer((socket) => {
      if (this.socket) {
        socket.end(encode({ v: 1, type: "error", error: "client_busy" }));
        return;
      }
      this.socket = socket;
      parser(socket, (message) => this.handlers.onMessage(message), (error) => this.handlers.onError?.(error));
      socket.on("close", () => {
        if (this.socket === socket) {
          this.socket = null;
          this.handlers.onDisconnect?.();
        }
      });
      this.handlers.onConnect?.();
    });
    await new Promise((resolve, reject) => this.server.listen(this.path, resolve).once("error", reject));
    this.ownsPath = true;
    await chmod(this.path, 0o600);
  }

  send(message) {
    if (this.socket && !this.socket.destroyed) this.socket.write(encode(message));
  }

  async close() {
    this.socket?.destroy();
    if (this.server) await new Promise((resolve) => this.server.close(resolve));
    if (this.ownsPath) await rm(this.path, { force: true });
  }
}

export class UnixLinkClient {
  constructor(path, handlers) {
    this.path = path;
    this.handlers = handlers;
    this.socket = null;
  }

  async connect() {
    this.socket = net.createConnection(this.path);
    parser(this.socket, (message) => this.handlers.onMessage(message), (error) => this.handlers.onError?.(error));
    this.socket.on("close", () => this.handlers.onDisconnect?.());
    await new Promise((resolve, reject) => this.socket.once("connect", resolve).once("error", reject));
  }

  send(message) {
    this.socket.write(encode(message));
  }

  close() {
    this.socket?.end();
  }
}
