import { UnixLinkClient, UnixLinkServer } from "./unix-link.js";

export const LOOP_CAPABILITIES = Object.freeze({ ordered: true, reliable: true, encrypted: false, proximate: true, maxMessageBytes: 4096 });

export class LoopTransportServer {
  constructor(path) { this.path = path; this.link = null; }
  async start(handlers) {
    this.link = new UnixLinkServer(this.path, {
      onConnect: () => handlers.onLinkState?.("up"),
      onDisconnect: () => handlers.onLinkState?.("down"),
      onMessage: (message) => handlers.onMessage(message),
      onError: (error) => handlers.onError?.(error),
    });
    await this.link.listen();
  }
  send(message) { this.link.send(message); }
  async stop() { await this.link?.close(); }
  capabilities() { return LOOP_CAPABILITIES; }
}

export class LoopTransportClient {
  constructor(path) { this.path = path; this.link = null; }
  async start(handlers) {
    this.link = new UnixLinkClient(this.path, {
      onMessage: (message) => handlers.onMessage(message),
      onDisconnect: () => handlers.onLinkState?.("down"),
      onError: (error) => handlers.onError?.(error),
    });
    await this.link.connect();
    handlers.onLinkState?.("up");
  }
  send(message) { this.link.send(message); }
  async stop() { this.link?.close(); }
  capabilities() { return LOOP_CAPABILITIES; }
}
