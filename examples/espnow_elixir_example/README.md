# AtomVM ESPNOW Elixir Example (node discovery)

This folder contains an Elixir example showing how to use the `:espnow` module from Elixir, including automatic node discovery using broadcast packets.

## What it does

- Calls `:espnow.init(channel)` (defaults to channel `1` in the example)
- Enables active-mode delivery with `:espnow.active(handle, self())`
- Periodically broadcasts a discovery request
- Responds to discovery requests via unicast and prints discovered peers
- Receives messages of the form:
  - `{:espnow, :rx, from_mac_bin, data_bin}`
  - `{:espnow, :tx, :broadcast | mac_bin, status_int}`

## Notes

- All devices must share the same WiFi channel for discovery to work.
- Build/flash is expected to be done via your AtomVM Elixir workflow/tooling (this folder includes a `mix.exs` configured for AtomVM).
