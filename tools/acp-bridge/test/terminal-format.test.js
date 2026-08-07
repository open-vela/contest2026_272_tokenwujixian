import test from "node:test";
import assert from "node:assert/strict";
import { bridgeHelp, formatUsage } from "../src/terminal-format.js";

test("terminal formatter keeps empty Billing slots visibly empty", () => {
  assert.deepEqual(formatUsage({ billing: { charge: { kind: "credit", amount: 0.5, unit: "credits" }, tokens: null }, context: null }), [
    "Billing: 0.5 credits / ",
    "Account: unavailable via ACP (session-local totals only)",
  ]);
  assert.deepEqual(formatUsage({ billing: { charge: null, tokens: { input: 11, output: 3, total: 14 } }, context: { unit: "token", used: 17, limit: 1000 } }), [
    "Billing:  / 14 tokens (input=11, output=3)",
    "Context: 17/1000 token",
    "Account: unavailable via ACP (session-local totals only)",
  ]);
});

test("bridge help documents local commands and ACP prompt input", () => {
  const help = bridgeHelp().join("\n");
  assert.match(help, /help/);
  assert.match(help, /usage/);
  assert.match(help, /<text>/);
});
