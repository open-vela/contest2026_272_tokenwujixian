import readline from "node:readline";

const input = readline.createInterface({ input: process.stdin, crlfDelay: Infinity });
for await (const line of input) {
  const message = JSON.parse(line);
  if (message.method === "initialize") {
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { protocolVersion: 1 } })}\n`);
  } else if (message.method === "session/new") {
    process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { sessionId: "fake-session" } })}\n`);
  } else if (message.method === "session/prompt") {
    const text = message.params?.prompt?.[0]?.text;
    if (text === "mimo-usage-probe") {
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", method: "session/update", params: { sessionId: "fake-session", update: { sessionUpdate: "usage_update", used: 17, size: 1000, cost: { amount: 0, currency: "USD" } } } })}\n`);
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { stopReason: "end_turn", usage: { inputTokens: 11, outputTokens: 3, totalTokens: 14 } } })}\n`);
      continue;
    }
    if (text === "mimo-partial-usage-probe") {
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { stopReason: "end_turn", usage: { totalTokens: 14 } } })}\n`);
      continue;
    }
    if (text === "mimo-foreign-context-probe") {
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", method: "session/update", params: { sessionId: "other-session", update: { sessionUpdate: "usage_update", used: 99, size: 1000 } } })}\n`);
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { stopReason: "end_turn" } })}\n`);
      continue;
    }
    if (text === "kiro-usage-probe") {
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", method: "_kiro.dev/metadata", params: { sessionId: "fake-session", contextUsagePercentage: 2.5, meteringUsage: [{ value: 0.125, unit: "credit", unitPlural: "credits" }, { value: 0.25, unit: "credit", unitPlural: "credits" }] } })}\n`);
      process.stdout.write(`${JSON.stringify({ jsonrpc: "2.0", id: message.id, result: { stopReason: "end_turn" } })}\n`);
      continue;
    }
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
