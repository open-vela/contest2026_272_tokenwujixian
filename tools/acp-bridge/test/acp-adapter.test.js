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
