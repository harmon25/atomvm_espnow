# Copilot instructions (AtomVM ESP-IDF component)

## Big picture
- This repo builds an ESP-IDF component that provides an AtomVM **port driver** for ESP-NOW.
- The driver is implemented in C under `nifs/` and provides an Erlang/Elixir API via `src/espnow.erl`.
- ESP-NOW enables connectionless peer-to-peer wireless communication between ESP32 devices.

## Where to look
- ESP-IDF component build integration: `CMakeLists.txt`, `component.mk`, `idf_component.yml`, `Kconfig`
- Port driver registration: `nifs/atomvm_espnow.c` (just registers the port driver)
- Port driver implementation: `nifs/espnow_driver.c` (main logic: WiFi init, esp_now callbacks, command handling)
- Header: `nifs/include/atomvm_espnow.h`
- Erlang wrapper API: `src/espnow.erl`
- Examples:
  - `examples/espnow_example` (Erlang)
  - `examples/espnow_elixir_example` (Elixir)

## Port driver architecture (NOT NIFs)

**Important**: This uses the AtomVM **port driver** pattern, not NIFs.

### Key components:
1. **Registration** (`atomvm_espnow.c`):
   - Uses `REGISTER_PORT_DRIVER(espnow, ...)` macro
   - Entry points: `atomvm_espnow_init`, `atomvm_espnow_create_port`, `atomvm_espnow_destroy`

2. **Port creation** (`espnow_driver.c`):
   - `atomvm_espnow_create_port()` is called when Erlang does `open_port({spawn, "espnow"}, Opts)`
   - Initializes WiFi (if needed), ESP-NOW, creates event queue
   - Returns a `Context*` with `native_handler` set to `espnow_consume_mailbox`

3. **Command handling**:
   - Commands sent via `Port ! {'$gen_call', {Pid, Ref}, Command}`
   - Erlang wrapper uses `gen_server_call/2` pattern (not actual gen_server)
   - Commands: `{send, To, Data}`, `{add_peer, Mac, Chan}`, `{mod_peer, Mac, Chan}`, `{del_peer, Mac}`, `{peer_exists, Mac}`, `get_channel`

4. **Async events**:
   - ESP-NOW callbacks (`recv_cb`, `send_cb`) run in WiFi task context
   - Events queued via FreeRTOS queue, delivered to owner via `globalcontext_send_message()`
   - Owner receives: `{espnow, rx, FromMac, Data}`, `{espnow, tx, To, Status}`

### Singleton pattern:
- Only one ESPNOW port can exist at a time (hardware limitation)
- Global state: `s_port_data`, `s_global`, `s_event_queue`

## WiFi coexistence

ESP-NOW and WiFi networking share the same radio:
- Driver detects if WiFi is already initialized (by AtomVM network module)
- If WiFi running: reuses existing config, won't reinitialize
- **Channel constraint**: When connected to AP, channel is locked to AP's channel
- Use `espnow:get_channel/1` to query current channel
- Use peer channel `0` to auto-use current channel

## AtomVM-specific gotchas

1. **No `erlang:phash2/1`** in AtomVM - use `atomvm:random()` instead
2. **Binary operations**: AtomVM supports standard binary syntax
3. **Memory constraints**: Use 16-bit values where 32-bit isn't needed (saves heap)
4. **Port message format**: Uses `{'$gen_call', {Pid, Ref}, Request}` - must parse manually since `port_parse_gen_message` doesn't recognize this format
5. **Atom strings in C**: Length-prefixed format `"\x6" "espnow"` (hex length byte + string)

## Build & workflow notes

- **ESP-IDF**: Requires v5.0+ (ESP-NOW v2.0 API)
- **Component registration**: Uses `WHOLE_ARCHIVE` linker option in `CMakeLists.txt`
- **Erlang build**: `rebar3` with `atomvm_rebar3_plugin`
- **Elixir build**: `mix` with `exatomvm` plugin, use `mix atomvm.esp32.flash --port <PORT>`
- **Flashing examples**: Flash to AtomVM firmware that includes this component

## API reference

```erlang
%% Open/close
{ok, Port} = espnow:open([{channel, 1}, {owner, self()}]).
ok = espnow:close(Port).

%% Send (broadcast or unicast)
ok = espnow:send(Port, broadcast, Data).
ok = espnow:send(Port, Mac, Data).

%% Peer management
ok = espnow:add_peer(Port, Mac, Channel).
ok = espnow:mod_peer(Port, Mac, Channel).
ok = espnow:del_peer(Port, Mac).
true | false = espnow:peer_exists(Port, Mac).

%% Channel query
Channel = espnow:get_channel(Port).
```

## Error handling conventions

- Success: `ok` or `true`/`false` for queries
- Failure: `{error, Reason}` where Reason is an atom or ESP-IDF error code integer
- Use `port_create_error_tuple(ctx, reason_term)` in C to create error tuples
