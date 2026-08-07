import test from "node:test";
import assert from "node:assert/strict";
import { fileURLToPath } from "node:url";
import { dirname, join } from "node:path";
import { StdioAcpAdapter } from "../src/acp-adapter.js";

const fixture = join(dirname(fileURLToPath(import.meta.url)), "..", "fixtures", "fake-acp-agent.mjs");

test("stdio adapter normalizes update and preserves a real reverse permission request", async () => {
  const events = [];
  const adapter = new StdioAcpAdapter({ backend: "fixture", command: process.execPath, args: [fixture], cwd: process.cwd() });
  adapter.onEvent((event) => events.push(event));
  await adapter.start();
  assert.equal(adapter.attachSession(), "fake-session");
  await adapter.submit("test");
  await new Promise((resolve) => setTimeout(resolve, 20));
  const permission = events.find((event) => event.type === "permission_request");
  assert.equal(permission.params.requestId, "permission-1");
  assert.deepEqual(permission.params.toolCall.rawInput, { token: "hidden" });
  assert.equal("summary" in events.find((event) => event.type === "tool"), false);
  await adapter.respondPermission(permission.params.requestId, { outcome: { outcome: "selected", optionId: "deny-1" } });
  await new Promise((resolve) => setTimeout(resolve, 20));
  assert.equal(events.at(-1).status, "completed");
  await adapter.stop();
});

test("stdio adapter keeps MiMo tokens and Kiro credits as distinct usage events", async () => {
  const events = [];
  const adapter = new StdioAcpAdapter({ backend: "fixture", command: process.execPath, args: [fixture], cwd: process.cwd() });
  adapter.onEvent((event) => events.push(event));
  await adapter.start();

  await adapter.submit("mimo-usage-probe");
  await adapter.submit("mimo-partial-usage-probe");
  await adapter.submit("mimo-foreign-context-probe");
  await adapter.submit("kiro-usage-probe");
  await new Promise((resolve) => setTimeout(resolve, 20));

  assert.deepEqual(events.find((event) => event.tokens), { type: "usage", tokens: { input: 11, output: 3, total: 14 } });
  assert.deepEqual(events.find((event) => event.context?.unit === "token"), { type: "usage", context: { unit: "token", used: 17, limit: 1000 } });
  assert.deepEqual(events.find((event) => event.charge), { type: "usage", charge: { kind: "credit", amount: 0.375, unit: "credits" }, context: { unit: "percent", used: 2.5, limit: 100 } });
  assert.equal(events.filter((event) => event.tokens).length, 1);
  assert.equal(events.filter((event) => event.context?.used === 99).length, 0);
  await adapter.stop();
});
