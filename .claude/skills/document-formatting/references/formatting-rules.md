# Feishu Formatting and Safe-Update Rules

## Priority

1. Preserve facts, links, structure, and unrelated rich content.
2. Follow explicit user styling.
3. Otherwise use the expert technical-report defaults.
4. Stop when uniqueness, completion, or structural integrity cannot be proven.

## Operation decision table

| Situation | Operation | Verification |
|---|---|---|
| New short document | `create_doc`; title must not repeat as first H1 | full fetch |
| New long document | create first segment, ordered append; location args remain mutually exclusive | poll task then fetch each segment |
| Replace one fetched media-free section | `replace_range` + `selection_by_title` | section + neighbors |
| Restyle one table/block | unique local `replace_range` | block + neighbors |
| Add adjacent/end content | precise insert / true-end append | boundary/tail |
| Existing broad rebuild | forbidden by this safety policy: `overwrite`, `replace_all` | n/a |
| Non-unique selection | stop and refine from fresh fetch | uniqueness first |

`wiki_node`, `wiki_space`, and `folder_token` are mutually exclusive on creation.

## Fetch and manifest

`fetch_doc` does not select by heading. By default, call it without `limit`/`offset` and fetch the complete document, then locate locally. Only when the user explicitly requests pagination may `limit`/`offset` be used; continue until the complete target block plus predecessor/successor is available. Record headings, tables, code/Mermaid fences, containers, media token/order/neighbors, and target/preserve status.

A fetched `<whiteboard token="..."/>` is read-only evidence, not creation syntax.

## Width policy

**Match the document's actual text-column width** so the table neither overflows nor looks narrow. Do not assume a fixed number: verify against a rendered reference — the table must not extend past the adjacent callout/heading right edge. Feishu wide-mode docs are ≈ **900–960px (930 typical)**; classic/narrow docs are ≈ **780–820px (800 typical)**. When a rendered table overflows, reduce the total (a one-number-per-table change) rather than assuming the default. Width count = columns, widths are positive integers, and sum = chosen total.

| Role | Typical px | Alignment |
|---|---:|---|
| index/icon/tiny enum | 70–100 | center |
| status/date/version | 90–140 | center |
| numeric/delta | 105–145 | center |
| symbol/API | 150–220 | usually left |
| repo/code path/revision | 210–300 | left |
| explanation/evidence/cause/procedure | 280–500 or remainder | left |

If 960px cannot remain readable, split by reader question. Exceed 960 only when the user explicitly overrides the default. A one-off compact/equal-width/all-left request also overrides defaults without changing future defaults.

Examples (930px):
- `类型/版本/说明`: `130,150,650`
- `状态/Change/路径/说明`: `110,140,270,410`
- `指标/前/后/变化/来源/解释`: `180,115,115,110,100,310`

### Uniform-centered style (common user request)

When the user asks for「表格总宽度 = 文本宽度、文本水平居中」or an equivalent uniform look: use **equal columns** summing to the text width and set `{align="center"}` on **every** cell (header and data). Equal-split examples — 800px: 5-col `160,160,160,160,160`, 4-col `200,200,200,200`, 3-col `266,267,267`, 2-col `400,400`; 930px: 5-col `186×5`, 4-col `232,233,232,233`, 3-col `310,310,310`.

### Alignment

Center headers and compact values with `{align="center"}`; center **all** cells for the uniform-centered style. Omit `align` for left-aligned paths/prose. Put attributes outside bold. Do not emit `{align="left"}`.

**Vertical centering (垂直居中) cannot be set through the MCP/markdown update tool** — `{align=...}` controls horizontal alignment only. Vertical alignment is a table-cell property applied in the Feishu UI (select the table → cell alignment → 垂直居中). Never claim vertical centering was applied by a write; report it as a one-click UI follow-up.

## lark-table structure

Hierarchy is `lark-table → lark-tr → lark-td`; each row has equal cells; cell content has surrounding blank lines; do not mix pipe-table syntax or nest lark-tables.

Enhanced cells may contain complex content such as a fenced code block when the complete nested structure is intentionally generated and validated. Keep ordinary technical-report code blocks outside tables by default for readability. Do not place Mermaid, media, or another lark-table inside a cell.

## Bold safety

Use paired same-block `**text**`, no inner whitespace/newline, no escaping when emphasis is intended, no split delimiters, and no changes inside code. Repair the smallest complete paragraph/list/callout/cell; never run a document-wide regex.

## Punctuation locale consistency

Match every punctuation mark to its surrounding language and never mix locales within one run. This applies to **all** punctuation (colon, comma, period, semicolon, enumeration comma `、`, brackets, quotes, exclamation/question marks, ellipsis, dash), not only colons.

- CJK context (both sides CJK, or at least one adjacent side is CJK): use full-width CJK punctuation — `：，。；、！？（）【】「」《》…——` — with no trailing ASCII space.
- Pure Latin/ASCII context (both sides are English words, numbers, code tokens, or identifiers): use the ASCII mark followed by a single space — `: ` `, ` `; ` `. ` — never a full-width mark.
- Never mix inside one clause: `实测结论:即` → `实测结论：即`; `codec-only,parser` → `codec-only, parser`; a Chinese sentence must not delimit clauses with ASCII `,`/`;`, and an all-English list must not use `，`/`；`.
- Do not alter punctuation inside fenced code blocks or ASCII diagrams — full-width marks are wider and break monospace alignment, and code content must stay byte-exact. Normalize prose, headings, callouts, blockquotes, and table cells only.
- When normalizing, edit the smallest unique block and reproduce the surrounding text (especially comment-anchored spans) exactly, so comment anchors are not orphaned.

## Fence and container safety

- Preserve matching fence and language labels.
- A selection contains the complete fence/container or none of it.
- Compare fence count, language, first/last content line, and order after each batch.
- Keep Mermaid/code outside callouts.
- Mermaid conversion warnings are failures to investigate.

## Selection safety

- `selection_by_title` replaces the complete heading section; use only after fetching and confirming that scope.
- `selection_with_ellipsis` boundaries should use 10–20 unique characters.
- Literal `...` in source must be escaped as `\.\.\.`.
- `insert_after` uses the matched range end; `insert_before` uses its start.
- Non-unique selection is a stop condition, not a reason to broaden across media.

## Ordered asynchronous batches

1. Fetch the complete document by default (or user-requested pages) and build the manifest.
2. Number targets by current document order.
3. Mutate the earliest complete block.
4. If a `task_id` returns, poll the same tool until terminal success/failure; pending is not completion.
5. Fetch the complete affected block and neighbors.
6. Validate content, table shape/width/alignment, bold, fences, containers, media, warnings, and unrelated content.
7. Only after PASS mutate the next block.
8. Finish with a full fetch and manifest comparison.

One section per batch is the long-report default. Split oversized sections only at heading/complete-block boundaries.

## Red flags

- existing-document overwrite/replace_all;
- pending task followed by another mutation or early fetch;
- non-unique or media-crossing selection;
- width count/sum mismatch, or an unverified fixed width that overflows/underfills the document text column;
- claiming vertical centering was set by a write (it is UI-only);
- blanket-centered path/prose;
- broken fence/container;
- next batch before read-back;
- changed media/fence/table count without intent;
- MCP acceptance treated as rendered success.
