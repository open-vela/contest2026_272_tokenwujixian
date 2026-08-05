export class FakeAcpAdapter {
  constructor() { this.listener = () => {}; this.responses = []; }
  onEvent(listener) { this.listener = listener; }
  async start() { this.listener({ type: "text", text: "Agent ready" }); }
  async respondPermission(id, result) { this.responses.push({ id, result }); }
  requestPermission(params) { this.listener({ type: "permission_request", params }); }
  tool(event) { this.listener({ type: "tool", ...event }); }
  async stop() {}
}
