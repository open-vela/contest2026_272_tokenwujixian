# DeskMate ACP bridge — PC loop milestone

The bridge is the PC-only ACP Client. It starts one backend (`mimo acp` or `kiro-cli acp`), normalizes its state, and exposes only DeskMate Link through a local Unix-domain socket or MQTT. The terminal client is an ACP-agnostic stand-in for the future OpenVela client.

The frozen link contract is [../../docs/protocol.md](../../docs/protocol.md). The client never receives ACP session IDs, raw tool arguments, credentials, or Agent-specific extension messages.

## Deterministic loop verification

```sh
cd tools/acp-bridge
npm test
npm run demo:loop
```

The fake adapter integration test proves nonce handshake, Unix socket delivery, host-side argument redaction, `ptSubmit`, exact option ID mapping, and duplicate decision suppression. It does not claim hardware or real-Agent permission control.

## Interactive PC client

In one terminal, start the bridge. `fake` is the deterministic loop backend:

```sh
node bin/deskmate-bridge.mjs --backend fake --socket /tmp/deskmate-link.sock
```

In another terminal, start the client:

```sh
node bin/deskmate-client.mjs --socket /tmp/deskmate-link.sock
```

The client sends hello with a fresh nonce and then displays `Offline`, `Idle`, `Busy`, `Attention`, or `Result` from DeskMate snapshots. Its commands are `status`, `once`, `deny`, `disconnect`, and `quit`. It has no `ptSubmit` command: only the bridge's ACP capture path can create a prompt.

For the fake demo, type `prompt` into the bridge terminal. This creates a synthetic request solely for PC-loop testing; it never runs a command. Then choose `once` or `deny` in the client terminal.

## Real ACP state-flow probe

Use a selected backend in the bridge terminal. It provides a small shell-style
REPL: `deskmate>` accepts ordinary text as an ACP prompt and streams agent text,
tool state, permission waits, and Billing events locally while continuing to
publish the same facts to the DeskMate Link.

```sh
node bin/deskmate-bridge.mjs --backend mimo --socket /tmp/deskmate-link.sock
node bin/deskmate-bridge.mjs --backend kiro --socket /tmp/deskmate-link.sock
```

The local commands are `help`, `status`, `usage`, and `quit`/`exit`; all other
input is sent as an ACP prompt. The adapter runs `initialize`, `session/new`,
and `session/prompt`, then turns `session/update` tool/text activity into
DeskMate facts. A real `session/request_permission` is held until the terminal
client sends `once` or `deny`; only the Agent-provided `allow_once` or
`reject_once` option ID can be returned.

## Session-local usage

The bridge maintains an in-memory ledger for the one ACP session it creates. It publishes a `usage_snapshot` after the nonce handshake, on each observed usage increment, and every 30 seconds. The terminal client's `usage` command asks for the same snapshot without issuing a model prompt.

- The snapshot has two display-ready `billing` slots: `charge` (left) and raw `tokens` (right). A device can render them as `Billing: <charge> / <total tokens>` and leaves any `null` slot blank.
- MiMoCode contributes raw `input`/`output`/`total` tokens to the right slot from a completed `session/prompt`, plus separate token-based context-window use from `usage_update`.
- Kiro contributes incremental `credits` to the left slot from `_kiro.dev/metadata.meteringUsage`; it does not expose raw token counts through ACP. Its context value is a percentage, not a token count.
- A verified provider currency amount can use the same left slot with `kind: "currency"`; the bridge does not treat MiMoCode's local `$0` estimate as a real price.
- The snapshot is only the bridge-process lifetime total for its current ACP session. It is never an account balance, remaining quota, rate-limit reading, or cross-machine aggregate. Units are deliberately never converted or summed together.

Known limitation: the current MiMoCode profile has been observed to complete a bash tool call without emitting `session/request_permission`. The bridge will show its state/tool result but will not invent an Attention prompt; this is not a permission-control success.

## Local MQTT transport

MQTT is the wireless DeskMate Link shared by the simulator and the real board. Run Mosquitto on the PC, then start the bridge with an explicit broker, topic prefix, and device ID:

```sh
mosquitto -p 18883 -v
node bin/deskmate-bridge.mjs --backend fake --transport mqtt \
  --broker mqtt://127.0.0.1:18883 --topic-prefix deskmate --device-id demo-1
node bin/deskmate-client.mjs --transport mqtt \
  --broker mqtt://127.0.0.1:18883 --topic-prefix deskmate --device-id demo-1
```

The bridge subscribes to `deskmate/demo-1/device-to-bridge`, publishes to `deskmate/demo-1/bridge-to-device`, and publishes non-retained `online`/LWT `offline` status to `deskmate/demo-1/status`. MQTT uses version 5, clean start, session expiry `0`, QoS `0`, and non-retained DeskMate messages. Set `DESKMATE_MQTT_USERNAME` and `DESKMATE_MQTT_PASSWORD` only through the environment when a broker requires authentication; never put credentials in commands, fixtures, or logs.

## Unified launcher

`bin/deskmate-demo.sh` shortens PC validation while keeping broker, bridge, and device/client in separate terminals. It accepts the real ACP backend and link transport as positional arguments:

```sh
# Terminal 1: local-only broker for PC validation
bin/deskmate-demo.sh broker --port 18883

# Terminal 2: use a real ACP backend over MQTT
bin/deskmate-demo.sh bridge mimo mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1
# Or: bin/deskmate-demo.sh bridge kiro mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1

# Terminal 3: PC device stand-in; later replace with the simulator or board
bin/deskmate-demo.sh client mqtt --broker mqtt://127.0.0.1:18883 --device-id goldfish-1
```

### Attach the native MiMo TUI to the bridge session

MiMo ACP exposes an attachable loopback server as well as its stdio ACP stream.
For a MiMo bridge, `--mimo-attach` gives the ACP server an explicit port,
waits until the bridge has created its actual ACP session, then opens a tmux
window attached to that exact session. It never guesses a session ID from logs:

```sh
bin/deskmate-demo.sh bridge mimo mqtt \
  --broker mqtt://10.189.140.244:1883 --device-id goldfish-1 \
  --mimo-attach --mimo-attach-port 4096
```

The chosen port must be unused before launch. If an earlier `mimo acp` still
owns `4096`, stop that bridge first or choose another explicit port, such as
`--mimo-attach-port 4097`.

The bridge writes an atomic, mode-`0600` runtime ready file containing only the
loopback URL and session ID, then deletes it when the bridge exits. Inside tmux,
the launcher creates a `deskmate-mimo` window running `mimo attach`. Outside
tmux, it opens a GNOME Terminal by default. It prints the exact attach command
only when no graphical session is available, `gnome-terminal` is unavailable,
or launching it fails. The native TUI and DeskMate share one ACP session: do
not submit concurrent prompts from the TUI, `deskmate>`, and device `@` input.

For the existing loop transport, replace the two final commands with `bridge <backend> loop` and `client loop`. Run `bin/deskmate-demo.sh --help` for all parameters. The launcher reads MQTT credentials only from `DESKMATE_MQTT_USERNAME` and `DESKMATE_MQTT_PASSWORD`.
# DeskMate ACP bridge

## Device chat turns

When a DeskMate device sends `prompt_submit`, the bridge validates its current
nonce/epoch, gives the request a single active ACP turn, and calls
`session/prompt` with the complete text. It responds with `prompt_ack`, streams
sanitized `agent_output` chunks, and ends with `turn_result`. The bridge, its
local `deskmate>` REPL, and the device share one turn lock; neither side may
interleave prompts in the same ACP session.

The bridge does not pass ACP chunks through verbatim. It removes terminal
control bytes and obvious inline secrets, batches output for up to 40 ms,
splits it at UTF-8 boundaries, and verifies every produced message stays within
the 4096-byte link limit. It never waits for an entire answer before streaming.
The 2048-byte input limit and 4096-byte wire cap are intentional safety bounds;
large document upload is not part of this protocol.
