# Feishu Adapter

Adapt only after the canonical Markdown is complete. Feishu is a rendering and placement target, not a second source of truth.

## Apply the canonical Feishu formatting skill

For table widths and alignment, Markdown bold safety, local-update mode selection, and post-write media verification, apply the document-formatting rules and read its [canonical rules](formatting-rules.md). User-explicit styling overrides those defaults. Keep this adapter focused on adapting diagram-first content rather than copying those rules here.

## Resolve the real target

Before previewing or writing:

1. Fetch the document and confirm its actual title.
2. Resolve the real section title from document content; treat URL anchors and user-supplied numbering as **hints**, never proof.
3. Identify the target block type: text, table, image, file, whiteboard, generated diagram, or another supported block.
4. If a whiteboard or media block is relevant, inspect it through the appropriate read/fetch operation rather than describing it from surrounding text.
5. Stop and ask when the intended location remains ambiguous. Do not guess a section, anchor, or block type.

## Adapt the canonical Markdown

- Keep heading depth to four levels or fewer.
- Prefer Mermaid for flow, sequence, state, and architecture views; use PlantUML only when it expresses a supported complex view more clearly. Preview either format to confirm Feishu renders it correctly.
- Use one callout for a key conclusion, risk, or limit; avoid turning every paragraph into a callout.
- Use tables for exact comparisons, mappings, and evidence—not narrative.
- Use grids only when side-by-side comparison improves comprehension.
- Preserve the canonical terminology, evidence classes, changed/unchanged boundary, and limits.
- Do not repeat a diagram as long prose; retain only its short reading guide or takeaway.
- When the user asks only for a **brief Feishu adaptation recommendation**, return at most 2–3 bullets: map canonical elements to candidate Feishu components, require real target resolution before any write, and recommend preview plus post-write verification. Keep diagram advice format-neutral unless rendering or conversion support was actually checked; say “preview the diagram in a supported component” rather than “render the Mermaid diagram.” Phrase actions as checks, not guarantees that an edit will preserve content.

## Preview before write

Prepare a preview that shows:

- resolved document title and section title;
- actual target block type;
- intended operation: append, insert before/after, or local range replacement;
- exact content or section boundary affected;
- adjacent content and media that must remain unchanged.

Do not publish silently. Obtain confirmation when the user requested preview, when placement is ambiguous, or when an existing section will be replaced.

## Write locally and preserve rich content

Prefer the smallest local operation:

1. Re-fetch the target section immediately before writing to detect drift.
2. Use local append, insert, or uniquely scoped range replacement.
3. Avoid document-wide overwrite. It can destroy media, whiteboards, comments, and collaborative structure.
4. Preserve existing image, file, and whiteboard references/tokens. Do not reconstruct or replace them merely to edit nearby text.
5. Keep the selection narrow enough that unrelated blocks cannot be captured.
6. If the selection is not unique, refine it or stop; never broaden it speculatively.

## Verify after write

Re-read the written section and its immediate neighbors. Confirm:

- the title, section, and block type still match the intended target;
- headings, diagrams, tables, and callouts rendered in the expected order;
- existing media and whiteboards remain present;
- no adjacent or unrelated content changed;
- the final text preserves facts, interpretations, decisions, limits, and source labels from the canonical Markdown.

If rendering produced a warning or converted a diagram incorrectly, repair only that block and re-read again. Report the final location and any unresolved rendering limitation.
