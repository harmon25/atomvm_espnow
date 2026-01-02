# Copilot instructions (AtomVM ESP-IDF component)

## Big picture
- This repo builds an ESP-IDF component that provides AtomVM native functions (NIFs) implemented in C under `nifs/`.
- The goal is an AtomVM-facing ESPNOW driver (ESP-IDF `esp_now`) with an Erlang wrapper in `src/`.

## Where to look
- ESP-IDF component build integration: `CMakeLists.txt`, `component.mk`, `idf_component.yml`, `Kconfig`
- AtomVM NIF collection + term marshaling patterns: `nifs/atomvm_espnow.c`
- ESPNOW driver glue (WiFi init, esp_now init, callbacks): `nifs/espnow_driver.c` and `nifs/include/atomvm_espnow.h`
- Erlang wrapper API surface: `src/espnow.erl`
- Examples:
  - `examples/espnow_example` (Erlang)
  - `examples/espnow_elixir_example` (Elixir code sample)

## NIF/driver architecture (how changes should be made)
- The AtomVM-facing entrypoint is `atomvm_espnow_get_nif()` which dispatches by string name like `"espnow:nif_init/1"`.
  - If you add a NIF: implement `static term nif_xxx(...)`, add a `static const struct Nif xxx_nif`, and extend the `strcmp()` chain.
- Handle passing is done via a **binary containing a native pointer** (`ptr_to_binary` / `binary_to_ptr`). Keep this convention so Erlang can store an opaque handle.
- Active mode is implemented in Erlang by spawning a poller that calls `espnow:poll/1` and forwards messages to an owner pid; this avoids needing AtomVM-internal “send message from C callback” APIs.
- Error signaling convention in NIFs:
  - Return `OK_ATOM` on success.
  - Return `{error, Reason}` tuples for recoverable failures (`Reason` is an atom like `not_supported` or an ESP-IDF `esp_err_t` integer).
  - Use AtomVM validation/macros (`VALIDATE_VALUE`, `RAISE_ERROR`) and call `memory_ensure_free()` before allocating tuples/binaries.

## Build & workflow notes
- ESP-IDF v5+: the component is registered with `idf_component_register(... WHOLE_ARCHIVE)` in `CMakeLists.txt`. There is legacy support for IDF v4 link options, but the manifest currently declares `idf: ">=5.0"` in `idf_component.yml`.
- This component is intended to be built as part of an AtomVM ESP32 build; see `README.md` (points to AtomVM build instructions).
- Erlang build uses `rebar3` and `atomvm_rebar3_plugin` (see `rebar.config`).
- The example under `examples/` uses `rebar3 esp32_flash -p <PORT>` (see `examples/espnow_example/README.md`).

## Repo-specific gotchas (don’t get trapped)
- The NIF name strings include the module prefix `espnow:`; Erlang wrappers must call the same names or the NIF lookup will fail.

