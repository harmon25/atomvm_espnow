# AtomVM ESPNOW Example Program

The `espnow_example` program is a minimal example that initializes ESPNOW on an ESP32 device.

> Note.  Building and flashing the `espnow_example` program requires installation of the [`rebar3`](https://www.rebar3.org) Erlang build tool.

To run this example program, you will need two devices (or one device and a peer) to exchange ESPNOW frames.

Build the example program and flash to your device:

    shell$ rebar3 esp32_flash -p /dev/ttyUSB0

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
