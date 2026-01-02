# AtomVM ESPNOW Driver/Library

This repo is being converted into an AtomVM Erlang library + ESP-IDF component implementing ESPNOW.

## Features

Work in progress.

Current skeleton supports:
- `espnow:init/1` / `espnow:deinit/1`
- `espnow:add_peer/3`
- `espnow:send/3`
- Singleton driver model: only one active handle at a time (`espnow:init/1` returns `{error, busy}` if already initialized)
- Active mode via `espnow:active/1,2` which forwards async messages to an owner pid:
	- `{espnow, rx, FromMacBin, DataBin}`
	- `{espnow, tx, broadcast | MacBin, StatusInt}`

## Quick Start

### Basic Usage (Erlang)

TODO

### Basic Usage (Elixir)

TODO

ESP-IDF ESPNOW docs:
https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/network/esp_now.html

## Building

For instructions on building AtomVM with this component, see the [AtomVM Build Instructions](https://doc.atomvm.net/build-instructions.html).

## License

Apache License 2.0
