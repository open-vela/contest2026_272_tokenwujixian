# DeskMate OpenVela Client

This is the team 272 OpenVela application client. It follows the frozen
DeskMate Link contract without depending on ACP, Node.js, Unix sockets, or a
board peripheral.

`deskmate_client.c` is the portable protocol state core.
`deskmate_controller.c` owns the active protocol session and routes an approved
or denied current prompt through its active transport. `deskmate_mqtt.c` is the
first transport: it uses NuttX MQTT-C over ordinary TCP, publishes complete JSON
messages, and subscribes to bridge messages. It does not read local input.
`deskmate_console.c` is a foreground-only input adapter that maps `once` and
`deny` to the controller and maps `usage` to a read-only `usage_query`. Future
GPIO input uses the same controller decision API; USB CDC will be a separate
newline-framed transport.

The controller keeps a local exact-prompt decision latch after a successful
send. Repeated local input for that `prompt.id` is rejected until a later
authoritative snapshot clears or replaces the prompt. The bridge remains the
final consumer and rejects all duplicate, stale, or mismatched decisions.

The app requires `CONFIG_NETUTILS_MQTTC=y` and `CONFIG_NETUTILS_CJSON=y`. For
the current LAN development broker, leave `CONFIG_NETUTILS_MQTTC_WITH_MBEDTLS`
disabled. Do not add the PC address, MQTT username, or password to source or
Kconfig. Supply them at launch:

```sh
deskmate -h <pc-lan-ip> -p 1883 -u deskmate -w <password> -d goldfish-1
```

The runtime command line exposes the password in an interactive process list;
this is acceptable only for the current simulator bring-up. Replace it with a
restricted deployment configuration before board delivery.

For the current Goldfish image, start the app after the virtual network is up:

```sh
deskmate -h <pc-lan-ip> -p 1883 -u deskmate -w <password> -d goldfish-1
```

It publishes to `deskmate/goldfish-1/device-to-bridge` and subscribes to
`deskmate/goldfish-1/bridge-to-device`. On every broker reconnect it creates a
new connection nonce, performs a new `hello` handshake, and remains Offline
until a matching snapshot arrives.

While the Goldfish app owns the NSH console, it displays a `DeskMate>` prompt.
Use `help` to print the command list. An empty line is a no-op; type `once` or
`deny` followed by Enter only for the current approval request. `usage` first
prints the currently cached session-local Billing value, then sends a
read-only query to refresh the cache. A changed query response is printed
automatically; an identical response only advances the telemetry sequence and
does not add duplicate console output. This is a simulator bring-up input only; it invokes
the same exact-prompt decision path that future GPIO buttons will use.
`deskmate` must remain the foreground command while this adapter is enabled,
so it is the sole reader of stdin; no background service may read the NSH
console concurrently.

Host verification:

```sh
gcc -std=c11 -Wall -Wextra -Werror -I app/deskmate \
  app/deskmate/deskmate_client.c app/deskmate/deskmate_controller.c \
  app/deskmate/test_deskmate_client.c \
  -o /tmp/deskmate-client-test && /tmp/deskmate-client-test
```

This is not USB, LCD/LED/key, or BK7258 bring-up evidence.

## Session-local Billing telemetry

The device accepts optional `usage_snapshot` telemetry only after the normal
nonce/epoch authority snapshot has locked the session. It maintains a separate,
strictly increasing `usage_seq`; a missing, stale, or malformed usage message
cannot alter a pending permission, clear the decision latch, or refresh the
30-second authority-snapshot liveness timer.

The display form is `Billing: <charge> / <tokens>`. The left slot is a native
provider charge (`credits` or a verified currency amount), while the right slot
is raw token usage. Either `billing` slot may be JSON `null` and is displayed
empty; the client never substitutes zero, derives a price from tokens, or
treats context-window use as billing. All values are bridge-session-local, not
an account balance or remaining quota.
# DeskMate device client

## Goldfish / NSH chat input

Once `deskmate` has connected and received a bridge `hello_ack` advertising
`chat_turns`, use the compact `@` prefix at its interactive prompt:

```text
DeskMate> @ 检查当前仓库有哪些未提交修改
DeskMate [agent] 我先检查工作区。
DeskMate [agent] 当前有以下修改……
DeskMate: Agent turn completed
DeskMate>
```

`@` sends one complete UTF-8 text prompt to the PC ACP Agent; it is not a
shell command and cannot grant permission. The input limit is 2048 UTF-8
bytes. While a turn or a real ACP permission request is active, a new `@`
prompt is rejected locally. The existing `once` and `deny` commands remain
the only permission decisions.

The device prints Agent-facing text as it arrives, preserving a bounded 8 KiB
transcript in memory for a future UI. It accepts increasing output sequence
numbers, notes a QoS 0 gap without changing authority state, and ignores stale
nonce, epoch, request, turn, or sequence values. DeskMate Link messages remain
capped at 4096 UTF-8 bytes.
