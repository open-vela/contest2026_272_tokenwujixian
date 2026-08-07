function chargeText(charge) {
  if (!charge) return "";
  if (charge.kind === "credit") return `${charge.amount} ${charge.unit}`;
  if (charge.kind === "currency") return `${charge.amount} ${charge.currency}`;
  return "";
}

export function formatUsage(usage) {
  if (!usage) return ["Billing: unavailable (ACP session is not ready)"];
  const tokens = usage.billing?.tokens;
  const tokenText = tokens ? `${tokens.total} tokens (input=${tokens.input}, output=${tokens.output})` : "";
  const lines = [`Billing: ${chargeText(usage.billing?.charge)} / ${tokenText}`];
  if (usage.context) lines.push(`Context: ${usage.context.used}/${usage.context.limit} ${usage.context.unit}`);
  lines.push("Account: unavailable via ACP (session-local totals only)");
  return lines;
}

export function bridgeHelp() {
  return [
    "DeskMate bridge commands:",
    "  help         Show this command list",
    "  status       Show local bridge state",
    "  usage        Show the current session-local Billing snapshot",
    "  quit, exit   Stop the bridge",
    "  <text>       Send text to the configured ACP backend",
  ];
}
