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
    return this.submitHandler ? this.submitHandler(text) : { stopReason: "completed" };
  }
  async respondPermission(id, result) { this.responses.push({ id, result }); }
  requestPermission(params) { this.listener({ type: "permission_request", params }); }
  text(text) { this.listener({ type: "text", text }); }
  tool(event) { this.listener({ type: "tool", ...event }); }
  usage(event) { this.listener({ type: "usage", ...event }); }
  async stop() {}
}
