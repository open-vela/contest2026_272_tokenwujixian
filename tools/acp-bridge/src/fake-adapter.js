export class FakeAcpAdapter {
  constructor() {
    this.backend = "fake";
    this.listener = () => {};
    this.responses = [];
    this.submits = [];
    this.submitHandler = null;
    this.backendState = "unknown";
    this.sessionSeq = 0;
    this.sessionId = null;
  }
  onEvent(listener) { this.listener = listener; }
  alive() { return this.backendState === "unknown" ? null : this.backendState === "up"; }
  #noteBackend(state) {
    if (this.backendState === state) return;
    this.backendState = state;
    this.listener({ type: "backend_state", state });
  }
  #newSessionId() {
    this.sessionSeq += 1;
    this.sessionId = `fake-session-${this.sessionSeq}`;
    return this.sessionId;
  }
  async start() { this.#newSessionId(); this.listener({ type: "session_started", backend: this.backend, sessionId: this.sessionId }); this.listener({ type: "text", text: "Agent ready" }); this.#noteBackend("up"); }
  async newSession() {
    this.#newSessionId();
    this.listener({ type: "session_started", backend: this.backend, sessionId: this.sessionId });
    return this.sessionId;
  }
  selectSession(sessionId) { this.sessionId = sessionId; return this.sessionId; }
  async submit(text) {
    this.submits.push(text);
    if (this.submitHandler) return this.submitHandler(text);
    /* Mirror the real backend: stream a transcript so device-side output
     * handling (agent_output -> turn transcript) is exercised without mimo. */
    this.text(`(fake) done: ${text}\nhello world script written to hello.sh`);
    return { stopReason: "completed" };
  }
  async respondPermission(id, result) { this.responses.push({ id, result }); }
  requestPermission(params) { this.listener({ type: "permission_request", params }); }
  text(text) { this.listener({ type: "text", text }); }
  tool(event) { this.listener({ type: "tool", ...event }); }
  usage(event) { this.listener({ type: "usage", ...event }); }
  async stop() { this.#noteBackend("down"); }
}
