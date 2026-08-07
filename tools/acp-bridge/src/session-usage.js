function finiteNonNegative(value) {
  return typeof value === "number" && Number.isFinite(value) && value >= 0 ? value : null;
}

/**
 * An in-memory, session-local ledger for data observed on one ACP connection.
 * It deliberately keeps backend-native units separate: Kiro credits are not
 * convertible to MiMoCode tokens, and context use is not billable usage.
 */
export class SessionUsageLedger {
  constructor({ backend = "unknown", clock = () => Date.now() } = {}) {
    this.clock = clock;
    this.reset(backend);
  }

  reset(backend = "unknown") {
    this.backend = backend;
    this.startedAtMs = this.clock();
    this.tokensObserved = false;
    this.tokens = { input: 0, output: 0, total: 0 };
    this.charge = null;
    this.context = null;
  }

  record(event = {}) {
    const tokens = event.tokens;
    if (tokens && typeof tokens === "object") {
      const input = finiteNonNegative(tokens.input);
      const output = finiteNonNegative(tokens.output);
      const reportedTotal = finiteNonNegative(tokens.total);
      if (input !== null && output !== null && reportedTotal !== null) {
        this.tokensObserved = true;
        this.tokens.input += input;
        this.tokens.output += output;
        this.tokens.total += reportedTotal;
      }
    }

    this.#recordCharge(event.charge);

    const context = event.context;
    if (context && typeof context === "object") {
      const used = finiteNonNegative(context.used);
      const limit = finiteNonNegative(context.limit);
      if ((context.unit === "token" || context.unit === "percent") &&
          used !== null && limit !== null) {
        this.context = { unit: context.unit, used, limit };
      }
    }
  }

  snapshot() {
    return {
      scope: "bridge_session",
      backend: this.backend,
      started_at_ms: this.startedAtMs,
      billing: {
        // Left slot: provider-native credits or an independently verified price.
        charge: this.charge ? { ...this.charge } : null,
        // Right slot: raw, non-converted token counts.
        tokens: this.tokensObserved ? { ...this.tokens } : null,
      },
      context: this.context ? { ...this.context } : null,
      // ACP carries per-turn/session deltas, not account billing snapshots.
      account: { status: "unavailable" },
    };
  }

  #recordCharge(charge) {
    if (!charge || typeof charge !== "object") return;
    const amount = finiteNonNegative(charge.amount);
    const kind = charge.kind;
    const unit = charge.unit;
    const currency = charge.currency;
    const validCredit = kind === "credit" && unit === "credits";
    const validCurrency = kind === "currency" &&
      typeof currency === "string" && /^[A-Z]{3}$/.test(currency);
    if (amount === null || (!validCredit && !validCurrency)) return;

    const normalized = validCredit
      ? { kind: "credit", amount, unit }
      : { kind: "currency", amount, currency };
    if (!this.charge) {
      this.charge = normalized;
      return;
    }

    // A session normally has one backend and one billing unit. Refuse to mix
    // units instead of inventing a conversion ratio.
    const sameCredit = this.charge.kind === "credit" && normalized.kind === "credit" && this.charge.unit === normalized.unit;
    const sameCurrency = this.charge.kind === "currency" && normalized.kind === "currency" && this.charge.currency === normalized.currency;
    if (sameCredit || sameCurrency) this.charge.amount += normalized.amount;
  }
}
