import test from "node:test";
import assert from "node:assert/strict";
import { DeskMateBridgeCore } from "../src/bridge-core.js";
import { FakeAcpAdapter } from "../src/fake-adapter.js";
import { hello, usageQuery } from "../src/protocol.js";
import { SessionUsageLedger } from "../src/session-usage.js";

test("session ledger exposes separate charge and token billing slots", () => {
  const ledger = new SessionUsageLedger({ backend: "mimo", clock: () => 123 });
  ledger.record({ tokens: { input: 11, output: 3, total: 14 }, context: { unit: "token", used: 17, limit: 1000 } });
  ledger.record({ charge: { kind: "credit", amount: 0.375, unit: "credits" }, context: { unit: "percent", used: 2.5, limit: 100 } });

  assert.deepEqual(ledger.snapshot(), {
    scope: "bridge_session",
    backend: "mimo",
    started_at_ms: 123,
    billing: {
      charge: { kind: "credit", amount: 0.375, unit: "credits" },
      tokens: { input: 11, output: 3, total: 14 },
    },
    context: { unit: "percent", used: 2.5, limit: 100 },
    account: { status: "unavailable" },
  });
});

test("session ledger leaves partial token reports unavailable", () => {
  const ledger = new SessionUsageLedger({ backend: "mimo" });
  ledger.record({ tokens: { total: 14 } });
  ledger.record({ tokens: { input: 11, output: 3, total: 14 } });

  assert.deepEqual(ledger.snapshot().billing.tokens, { input: 11, output: 3, total: 14 });
});

test("bridge publishes and returns the same session-local usage snapshot", async () => {
  const adapter = new FakeAcpAdapter();
  const outbound = [];
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message), clock: () => 456 });
  await adapter.start();
  await core.receive(hello("nonce-a"));

  adapter.usage({ tokens: { input: 11, output: 3, total: 14 }, context: { unit: "token", used: 17, limit: 1000 } });
  adapter.usage({ charge: { kind: "credit", amount: 0.375, unit: "credits" } });
  const pushed = outbound.at(-1);
  assert.equal(pushed.type, "usage_snapshot");
  assert.deepEqual(pushed.usage.billing, {
    charge: { kind: "credit", amount: 0.375, unit: "credits" },
    tokens: { input: 11, output: 3, total: 14 },
  });
  assert.deepEqual(pushed.usage.account, { status: "unavailable" });

  await core.receive(usageQuery("nonce-a", pushed.epoch));
  const queried = outbound.at(-1);
  assert.equal(queried.type, "usage_snapshot");
  assert.ok(queried.usage_seq > pushed.usage_seq);
  assert.deepEqual(queried.usage, pushed.usage);

  const sequence = queried.usage_seq;
  await core.receive(usageQuery("wrong-nonce", queried.epoch));
  await core.receive(usageQuery("nonce-a", "wrong-epoch"));
  assert.equal(outbound.at(-1).usage_seq, sequence);
});

test("bridge suppresses usage snapshots until its ACP session is ready", async () => {
  const adapter = new FakeAcpAdapter();
  const outbound = [];
  const core = new DeskMateBridgeCore({ adapter, publish: (message) => outbound.push(message) });

  await core.receive(hello("nonce-a"));
  assert.equal(outbound.some((message) => message.type === "usage_snapshot"), false);
  await adapter.start();
  const usage = outbound.at(-1);
  assert.equal(usage.type, "usage_snapshot");
  assert.equal(usage.usage_seq, 1);
});
