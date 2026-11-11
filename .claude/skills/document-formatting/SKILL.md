---
name: document-formatting
description: >
  文档排版与美化。One skill covering three document typesetting/beautification capabilities —
  (1) safely formatting and updating Feishu/Lark docs through MCP (table widths/alignment, Markdown
  emphasis, section-scoped edits, punctuation-locale consistency, async write verification);
  (2) authoring diagram-first technical documentation from fragmented notes, diffs, logs, or
  measurements (converting flat prose/tables into Mermaid or ASCII text-sketches); (3) building
  beautiful, editable Feishu/Lark whiteboards, infographics, posters and visual explainers from SVG
  using a 35-style colour-palette library. Use when the user wants to format, restyle, or beautify a
  document, lay out a technical report, turn prose into diagrams, or create a polished Feishu
  whiteboard/poster/infographic.
---

# Document Formatting (文档排版美化)

One skill, three capabilities. Dispatch by what the user is actually asking for, then read only the
reference files that capability needs.

| The user wants… | Capability | Read first |
|---|---|---|
| Format / restyle / safely edit an existing Feishu doc (tables, bold, sections, punctuation) | **A. Feishu formatting & safe update** | [references/formatting-rules.md](references/formatting-rules.md) |
| Turn notes / diffs / logs / prose into readable technical documentation, with diagrams | **B. Diagram-first documentation** | [references/diagram-selection.md](references/diagram-selection.md) |
| A beautiful editable Feishu whiteboard / infographic / poster | **C. Beautiful whiteboard** | [whiteboard/RULES.md](whiteboard/RULES.md) + [whiteboard/CATALOG.md](whiteboard/CATALOG.md) |

A single request can cross capabilities (e.g. "write up this investigation as a Feishu doc with
diagrams and make it look good") — then apply **B** to author the canonical Markdown, **A** to adapt
and write it safely, and **C** only if a standalone whiteboard/poster is wanted.

---

## Capability A — Feishu formatting & safe update

**Core contract.** Format by visible content and semantic role, then mutate the **smallest complete
structural block** in document order. An MCP success response is not completion: every completed
mutation batch must survive read-back with tables, code, diagrams, media, and neighbors intact.
User-explicit styling overrides defaults. Content skills decide facts; this capability controls
serialization, layout, update safety, and verification.

**Workflow (condensed):**

1. **Fetch and classify.** Call `fetch_doc` without `limit`/`offset` to get the complete document,
   then locate boundaries locally. Never infer current headings/tokens from an old draft.
2. **Build a structural manifest** in document order: headings; table boundaries/shape; fenced code
   and Mermaid boundaries; callout/grid/container boundaries; whiteboard/image/file tokens, order,
   and neighbors; target/preserve status. Never split a table/fence/container or cross media.
3. **Allocate auditable table widths** to the document's *actual* text-column width (≈800px
   narrow, ≈930px wide). Width count = column count, sum = chosen total. Center headers/compact
   values; leave paths/prose left-aligned. Vertical centering is **UI-only** — never claim it from a write.
4. **Preflight serialization** (balanced fences, equal cells per row, same-block bold, punctuation
   locale). CJK context → full-width marks; pure-ASCII context → ASCII mark + space; never mix;
   code/ASCII fences exempt.
5. **Mutate in document order.** No `overwrite`/`replace_all` on existing docs. Use one unique
   complete-block `replace_range`, precise inserts, or true-end append. Escape literal `...` as `\.\.\.`.
6. **Resolve async completion.** A `task_id` is not completion — poll the same tool to terminal state.
7. **Read-back barrier** after every batch: re-fetch the affected block + neighbors and verify.

**Hard rules (non-negotiable):** no existing-doc `overwrite`/`replace_all`; no non-unique or
speculative selection; no split structural block or media crossing; no width-count/sum mismatch; no
blanket centering of paths/prose; no mixed punctuation locales; no next batch before async
completion + read-back; no rendering-success claim from MCP acceptance alone.

Full detail (operation table, width policy, lark-table structure, bold safety, selection safety,
ordered async batches, red flags) is in [references/formatting-rules.md](references/formatting-rules.md).

---

## Capability B — Diagram-first documentation

**Core principle.** Organize around the reader's questions, not the author's chronology. Use diagrams
only where they compress structure or causality better than prose, and keep every claim tied to
evidence. A diagram reads as authoritative, so precision matters more than in prose — never let a
visual encode a vague, guessed, or unverified condition/threshold/step.

**Two task shapes.** (a) Author a new document from raw notes/diffs/logs. (b) **Convert** existing
flat prose or tables into diagrams on request (e.g. "文字不生动，绘制图形表示原理和流程") — preserve
facts, exact labels, and neighbouring rich content; change only the targeted block; add at most one
takeaway; do not duplicate an adjacent diagram.

**Workflow (condensed):**

1. **Fix goal and scope.** Audience, review goal, supplied evidence, desired output, target.
2. **Inventory evidence** as Fact / Interpretation / Decision / Limit — never smooth uncertainty into fact.
3. **Map reader questions → evidence → best visual → exact-value table → limiting evidence.**
4. **Build the terminology ladder** before technical wording: `plain meaning → technical term → exact source label`.
5. **Choose one evidence-backed visual per question.** Mermaid for rendered flow/sequence/state; a
   fenced **ASCII text-sketch** for precise inline annotation, lanes/layout/funnel/compact state
   machines. A table stays a table only for exact parallel values/mappings.
6. **Compose to a proportional reading budget** (10s / 1min / deep review).
7. **Produce the canonical platform-neutral Markdown first**; adapt to Feishu only when requested
   (see [references/feishu-adapter.md](references/feishu-adapter.md)).

**Budgets.** Short or single-mechanism note: **1–3 diagrams**, ~900-word ceiling, no minimum. Long or
multi-stage handoff: count by distinct reader questions (**4–7 typical, not a quota**), no word
ceiling; diagrams ≥ tables for process-heavy content. For a Chinese long-form deep doc, title with
**“深度梳理”**, not **“交接”/“技术交接”**.

Full detail (visual selection table, complexity budgets, Mermaid + text-sketch patterns, evidence
classes, reading-budget shapes) is in [references/diagram-selection.md](references/diagram-selection.md).
Run every objective gate in [references/readability-gates.md](references/readability-gates.md) before
delivery. Feishu adaptation rules are in [references/feishu-adapter.md](references/feishu-adapter.md).

---

## Capability C — Beautiful Feishu whiteboard

A design-system skill: **35 colour-palette styles** plus the verified hard rules of the Feishu SVG
whiteboard medium. This is **not** an auto-layout chart generator — **you** compose the layout; a
template gives palette + mood, [`whiteboard/RULES.md`](whiteboard/RULES.md) gives the medium's hard
limits. The output is a real, editable Feishu whiteboard inside a doc, not a screenshot.

**How to run:**

1. **Check prerequisites** first — run `whiteboard/scripts/preflight.sh` (Node ≥20, `feishu` CLI
   `@mi/feishu` installed + authenticated, `@larksuite/whiteboard-cli` via npx). Stop with install
   instructions if missing.
2. **Understand the board** (content, purpose, audience), then **ask about the vibe** (playful vs
   professional, colour/mood, brand) unless already named.
3. **Pick a style** from [`whiteboard/CATALOG.md`](whiteboard/CATALOG.md) alone (palette signatures
   listed there — do not open several `design.md` to compare). Say which and why, in one line.
4. **Build** — read [`whiteboard/RULES.md`](whiteboard/RULES.md) and only the one chosen
   `whiteboard/templates/<slug>/design.md`, then compose the SVG (~1600–1700 logical width, **native
   shapes only**: rect/rounded-rect/circle/ellipse/line/polyline + `<text>`; arrows via `marker-end`,
   never hand-drawn `<polygon>` heads; no gradient/filter/pattern/opacity/shadows; never set
   `font-family`). **Only content goes on the board, never the instruction/meta behind it.** Render,
   **look at the image, fix what you see** (overflow, margins, overlaps, clipping, hand-drawn
   arrowheads) via small in-place SVG edits. Then write into Feishu with `feishu board create-notes`
   and view the live board too.
5. **Deliver both** the Feishu doc link **and** the rendered image, and offer to switch styles.

The hard medium rules (one font, native shapes, arrow markers, opacity ignored, no shadows, no
echoing the user's instructions, reflow-by-character) live in
[whiteboard/RULES.md](whiteboard/RULES.md). The full 35-style catalogue (level / formality / vibe /
palette hex) is [whiteboard/CATALOG.md](whiteboard/CATALOG.md); per-style palette notes are in
[whiteboard/templates/](whiteboard/templates/); preview images in
[whiteboard/assets/styles/](whiteboard/assets/styles/).

---

## Cross-capability guardrails

- **Source of truth is one.** Author canonical Markdown (B) first; Feishu (A/C) is a rendering and
  placement target, never a second source of truth.
- **Preserve rich content.** Never destroy media, whiteboards, comments, or collaborative structure
  to edit nearby text; keep selections narrow.
- **Never publish silently.** Default to draft + preview; verify after every write.
- **Facts vs layout.** The document's facts come from the evidence; this skill controls layout,
  serialization, update safety, and visual polish — not truth.
