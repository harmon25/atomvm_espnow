# AtomVM ESPNOW Elixir Example (minimal)

This folder contains a minimal Elixir module showing how to use the `:espnow` module from Elixir.

## What it does

- Calls `:espnow.init(0)`
- Enables active-mode delivery with `:espnow.active(handle, self())`
- Receives messages of the form:
  - `{:espnow, :rx, from_mac_bin, data_bin}`
  - `{:espnow, :tx, :broadcast | mac_bin, status_int}`

## Notes

- This is a code sample (no Mix project is included yet).
- Build/flash is expected to be done via your AtomVM Elixir workflow/tooling.
