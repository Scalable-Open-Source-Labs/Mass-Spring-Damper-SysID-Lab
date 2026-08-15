# Firmware

Arduino sketch for the RP2040 main board. Reads the linear encoder as the carriage moves, drives the 7-segment display, and exposes the captured motion data as a CSV over USB — the board enumerates as a mass-storage drive, so data can be pulled off without any host-side software.

For build/flash setup (Arduino IDE, board settings, library versions), see [Programming-Instructions.md](../Programming-Instructions.md).

## Files

| File | Role |
|---|---|
| `firmware-mass-spring-damper-sys-id-lab.ino` | Main sketch — state machine, encoder capture, CSV data buffer |
| `gpio.cpp` / `gpio.h` | Pin definitions and GPIO init for the encoder, button/LED, and display |
| `LedControlPatched.cpp` / `.h` | Fork of the [LedControl](https://wayoda.github.io/LedControl/) library, with local modifications, used to drive the 7-segment display |
| `ramdisk.h` | USB Mass Storage (virtual FAT drive) implementation used to export captured data as a CSV file |
