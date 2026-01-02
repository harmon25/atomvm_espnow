# AtomVM ESPNOW Example Program

The `espnow_example` program demonstrates automatic node discovery using broadcast packets.

Each node periodically broadcasts a discovery request and responds to discovery requests with a unicast reply. When a peer is discovered, the example prints the peer's MAC address and a small peer id.

> Note.  Building and flashing the `espnow_example` program requires installation of the [`rebar3`](https://www.rebar3.org) Erlang build tool.

To run this example program, you will need 2+ devices on the same WiFi channel.

Build the example program and flash to your device:

    shell$ REBAR_BASE_DIR=/tmp/atomvm_espnow_example_build rebar3 esp32_flash -p /dev/ttyUSB0

> Note.  Because this example lives inside the `atomvm_espnow` component repo and depends on it via a `{path, "../.."}` dependency, you may need to set `REBAR_BASE_DIR` so `rebar3` builds outside the component directory (avoids copying the dependency into a subdirectory of itself).

> Note.  This build step makes use of the [`atomvm_rebar3_plugin`](https://github.com/atomvm/atomvm_rebar3_plugin).  See the `README.md` for information about parameters for setting the serial port and baud rate for your platform.

Attach to the console using the `monitor` Make target in the AtomVM ESP32 build:

    shell$ cd .../AtomVM/src/platform/esp32
    shell$ make monitor
    Toolchain path: /work/opt/xtensa-esp32-elf/bin/xtensa-esp32-elf-gcc
    WARNING: Toolchain version is not supported: crosstool-ng-1.22.0-95-ge082013a
    ...
    Found AVM partition: size: 1048576, address: 0x210000
    Booting file mapped at: 0x3f420000, size: 1048576
    I (...) atomvm_espnow: ...
    Found AVM partition: size: 1048576, address: 0x110000
    Starting: espnow_example.beam...
    ---
    I (...) atomvm_espnow: ...

This example currently just initializes ESPNOW and relies on C-side logging for RX/TX callbacks.

## Notes

- For discovery to work reliably, all devices must be on the same channel. The example currently sets the channel to `1` in `src/espnow_example.erl`.
- ESPNOW broadcast does not identify “self”, so if you see no discoveries, double-check that all devices are using the same channel and are in range.
