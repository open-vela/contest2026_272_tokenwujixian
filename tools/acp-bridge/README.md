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

Use a selected backend in the bridge terminal and type a prompt as bridge stdin:

```sh
node bin/deskmate-bridge.mjs --backend mimo --socket /tmp/deskmate-link.sock
node bin/deskmate-bridge.mjs --backend kiro --socket /tmp/deskmate-link.sock
```

The adapter runs `initialize`, `session/new`, and `session/prompt`, then turns `session/update` tool/text activity into DeskMate facts. A real `session/request_permission` is held until the terminal client sends `once` or `deny`; only the Agent-provided `allow_once` or `reject_once` option ID can be returned.

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

For the existing loop transport, replace the two final commands with `bridge <backend> loop` and `client loop`. Run `bin/deskmate-demo.sh --help` for all parameters. The launcher reads MQTT credentials only from `DESKMATE_MQTT_USERNAME` and `DESKMATE_MQTT_PASSWORD`.
