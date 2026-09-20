export class FakeAcpAdapter {
  constructor() {
    this.backend = "fake";
    this.listener = () => {};
    this.responses = [];
    this.submits = [];
    this.submitHandler = null;
  }
  onEvent(listener) { this.listener = listener; }
  async start() { this.listener({ type: "session_started", backend: this.backend }); this.listener({ type: "text", text: "Agent ready" }); }
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
  async stop() {}
}
