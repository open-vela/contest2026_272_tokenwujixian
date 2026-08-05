import readline from "node:readline";

const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
for await (const line of input) {
  const message = JSON.parse(line);
  if (message.method === "initialize") {
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { protocolVersion: 1 } })}\n`);
  } else if (message.method === "session/new") {
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { sessionId: "fake-session" } })}\n`);
  } else if (message.method === "session/prompt") {
    const update = { sessionUpdate: "tool_call", toolCallId: "tool-1", title: "Run test", status: "pending", rawInput: { token: "hidden" } };
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", method: "session/update", params: { update } })}\n`);
    const params = { sessionId: "fake-session", toolCall: { toolCallId: "tool-1", title: "Run test" }, options: [{ kind: "allow_once", optionId: "allow-1" }, { kind: "reject_once", optionId: "deny-1" }] };
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: "permission-1", method: "session/request_permission", params })}\n`);
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { stopReason: "waiting" } })}\n`);
  } else if (message.id === "permission-1" && message.result) {
    const update = { sessionUpdate: "tool_call_update", toolCallId: "tool-1", title: "Run test", status: "completed", rawInput: { token: "hidden" } };
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", method: "session/update", params: { update } })}\n`);
  }
}
