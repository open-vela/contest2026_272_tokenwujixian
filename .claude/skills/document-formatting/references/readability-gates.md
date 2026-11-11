# Readability Gates

Run these gates against the canonical Markdown before delivery. A failed gate requires revision or an explicit limit; do not compensate with more prose.

## 1. One-minute scanability

For every source, pass only when:

- The first screen contains a plain-language title, a one-sentence purpose or conclusion, terminology needed by the first visual, and the first useful visual or compact summary.
- For a Chinese long-form deep technical document, the reader-facing title uses **“深度梳理”**, not **“交接”** or **“技术交接”**, unless the user supplied an explicit title that must be preserved.
- Up to the first **2–3 core diagrams** form an independently scannable path through system position or symptom, main mechanism or cause, change/result boundary, and validation status. A one-question source may pass with one diagram.
- Each diagram answers a different reader question.
- Optional operations, hardening, and future tests occupy at most a short, clearly labeled follow-up section unless they are part of the requested handoff scope.

Apply the numeric budget only after classifying source complexity:

- **Short or single-mechanism note:** impose no word-count or diagram minimum. Use **1–3 diagrams** according to distinct supported questions and keep a **900-word ceiling** unless requested depth or evidence preservation requires otherwise. Evidence-rich short notes commonly land around **600–850 words**; tiny one-question inputs should remain substantially shorter.
- **Long or multi-stage handoff:** do not apply the short-note word or diagram ceiling. Keep the one-minute path compact and place additional supported mechanism, transformation, attribution, decision, and proof visuals in deep review.

## Visual coverage and technical depth

For long or multi-stage inputs, pass only when:

- the document has an explicit coverage map in the drafting process: `reader question → evidence → visual → exact-value table, if needed → limit`;
- every independent layer needed for handoff—such as system position, transformation, before/after structure, resource or performance causality, experiment isolation, and validation closure—has a visual or an explicit reason why prose/table is clearer;
- diagrams preserve the evidence-backed intermediate states required to support their conclusions rather than collapsing a multi-stage chain into start→end;
- processes, causal chains, state changes, and validation loops are not moved into tables merely because 2–3 diagrams already exist;
- tables remain focused on exact comparisons, mappings, or compact evidence lookup;
- the first 2–3 diagrams serve the one-minute path while additional diagrams answer distinct deep-review questions;
- added length comes from new information layers, exact evidence, boundaries, or decisions—not repeated conclusions, arrow narration, oversized text nodes, or prose padding;
- a process-heavy handoff that contains more tables than diagrams has been re-checked for flattened processes; a justified specification or lookup-heavy exception is stated by the information shape itself.

## 2. Terminology progression

Pass only when:

- Each specialized concept first appears as `plain-language meaning → technical term → exact source label`, either inline or in a compact key.
- Every acronym is expanded at its first appearance, including titles, conclusions, table cells, and diagram labels.
- The terminology key appears before the first sentence or diagram that depends on unfamiliar protocol, request-method, metric, queue, component, or platform terms.
- A final top-to-bottom scan finds no definition that occurs after first use.
- Every meaningful color, line style, or symbol has a legend, and the diagram remains understandable in grayscale.

## 3. Claim and diagram provenance

For every causal, quantitative, equivalence, or status claim, record or link its provenance.

| Claim class | Required provenance or wording |
|---|---|
| Fact | Source file/diff, log line, measurement, command result, or document block |
| Interpretation | Supporting facts plus language such as “indicates” or “therefore” |
| Decision | Decision owner or supplied conclusion, rationale, and trade-off when known |
| Limit | Untested path, unknown, unavailable evidence, or explicit scope boundary |

Pass only when:

- rejected hypotheses are not presented as root causes and recommendations are not presented as implemented behavior;
- every diagram node, edge, destination, and responsibility maps to supplied evidence or is explicitly labeled as interpretation;
- no visual encodes a vague, guessed, or unverified condition, threshold, gate, or step; every branch label is precise and evidence-backed or shown as unknown, and converting prose into a diagram introduced no invented gate;
- missing destinations, owners, authorization behavior, deployment state, and lifecycle behavior remain unknown rather than being completed from a typical architecture;
- ambiguous source wording is preserved instead of silently choosing one interpretation.

## 4. Changed-versus-unchanged boundary

Pass only when the document explicitly states:

- the smallest behavior or implementation region that changed;
- upstream and downstream behavior that stayed unchanged, when supported by the input;
- preserved contracts, outputs, failure semantics, or numerical semantics relevant to the conclusion;
- configuration or rollout boundaries when supplied.

A changed-file list alone does not pass this gate. If unchanged behavior is not evidenced, label it unknown rather than infer it from silence.

## 5. Product, measurement, and validation separation

Pass only when each artifact is classified by its supplied role:

- **Product:** shipped behavior, implementation, configuration, or runtime path;
- **Measurement:** profiling, tracing, logging, counters, or benchmark instrumentation;
- **Validation:** tests, fixtures, comparison tools, commands, or expected outputs.

Do not infer shipping or deployment status from a path. Do not imply that measurement or validation assets alter product behavior. If one file has multiple roles, identify the relevant section or change rather than assigning one misleading label.

## 6. Evidence closure

Pass only when:

- each conclusion is attributed to the evidence that directly proves it; stronger but indirect evidence is only corroboration;
- a negative observation about one branch is not turned into a positive claim that another branch ran unless the alternatives are exhaustive and evidenced;
- a tested platform, value, duration, or input is called **tested**, not **supported**;
- before/after measurements use comparable conditions, or the mismatch is stated;
- a root-cause-removing fix is distinguished from mitigation, containment, or an independent defense;
- validation lists completed checks separately from planned or untested checks;
- bit-exact, causal, performance, reliability, and security conclusions are not generalized beyond the tested dimensions;
- commands and exact values live in deep-review material, not the one-minute path.

## 7. Unknowns and limits

Pass only when unknowns are visible near the conclusion they constrain. Include untested platforms, configurations, durations, failure modes, dependencies, or operational paths supplied by the evidence. Use **Unknown**, **Untested**, or **Limit** labels; do not hide them in a generic “Notes” section.

## 8. Non-duplication and scope

Pass only when:

- one mechanism or causal chain has one canonical visual;
- an overview/定位 diagram and a detailed diagram of the same chain answer different reader questions (e.g. ownership vs data-flow-with-fallback), or are merged into one;
- the next section does not recreate that visual as an “at a glance” chronology;
- an evidence table maps claims to sources without walking through the same sequence again or repeating every timing value;
- prose after a diagram adds evidence, exception, boundary, or decision rather than narrating every arrow;
- each key conclusion and quantitative value has one primary home, with at most one short cross-reference when needed;
- every section traces to an input fact, supported interpretation, requested decision, or explicitly labeled follow-up;
- removing any H2 would lose information needed for the stated goal.

## Final self-review

- Scan from the title downward for acronyms and specialized nouns whose expansion or explanation appears later; if the conclusion needs them, move the key before the conclusion.
- Count whitespace-delimited words for short inputs and trim any result above 900 unless the user requested depth or omission would lose supplied evidence.
- Compare every quantitative conclusion with the exact source metric name and scope; reject broadened wording and unsupported superlatives or minimality claims.
- For interaction/handoff tasks, confirm one primary visual spans every participant requested by the user without adding unsupplied intermediaries or request payloads.
- Search for `TBD`, `TODO`, placeholder labels, invented examples, and unresolved references.
- Check each diagram edge against the source notes; remove inferred destinations and responsibilities.
- Check Markdown structure and diagram syntax. Both Mermaid diagrams and fenced plaintext text-sketches count as visuals for the one-minute-path and coverage gates; for a text-sketch, confirm it is inside a `plaintext` fence, its width does not overflow the text column, and its nodes/edges still trace to evidence. State a rendering limitation only when rendering was actually attempted and the result matters to the user; do not add unverifiable environment/process notes.
- Check that every document link and source reference resolves to the intended evidence or section.
- Check terminology and evidence labels for contradictions across summary, diagrams, tables, and limits.
- Confirm the canonical Markdown stands alone even when no Feishu adaptation is produced.