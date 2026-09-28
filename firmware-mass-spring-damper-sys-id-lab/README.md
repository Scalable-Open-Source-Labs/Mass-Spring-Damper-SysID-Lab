# Firmware

Arduino sketch for the RP2040 main board. It reads the linear encoder as the carriage moves, drives the 7-segment display, and exposes the captured motion data as a CSV over USB. The board enumerates as a mass-storage drive, so data can be pulled off without any host-side software.

For build/flash setup (Arduino IDE, board settings, library versions) and the calibration every new unit needs, see [Programming-Instructions.md](../Programming-Instructions.md).

## Files

| File | Role |
|---|---|
| `firmware-mass-spring-damper-sys-id-lab.ino` | Main sketch: mode state machine, encoder capture, CSV data buffer, test mode |
| `gpio.cpp` / `gpio.h` | Pin definitions and GPIO init for the encoder, button, and display. Per-channel comparator thresholds (PWM through an RC filter) and sensor voltage readings (ADC) |
| `calibration.cpp` / `.h` | Per-unit threshold calibration, saved to emulated EEPROM |
| `LedControlPatched.cpp` / `.h` | Fork of the [LedControl](https://wayoda.github.io/LedControl/) library, with local modifications, used to drive the 7-segment display |
| `ramdisk.h` | USB Mass Storage (virtual FAT drive) implementation used to export captured data as a CSV file. The 32 KB image holds about 2,500 rows; `generateCSV()` stops at the last whole row that fits and reports it on serial |

## Encoder thresholds

Each optical channel's comparator switches at its own threshold, set by PWM through a 10k/1µF filter. An uncalibrated unit uses 780 mV on every channel (`VREF_DEFAULT_MV` in `gpio.h`).

Calibration measures each channel's light and dark sensor levels during a sweep and centres the comparator's hysteresis band between them, so each unit adapts to its own sensors. A channel whose levels are closer than 500 mV is refused (`MIN_SPAN_MV`). The thresholds, and the levels they came from, are saved with a CRC in emulated EEPROM (the last 4 KB of flash), which UF2 firmware updates leave alone.

If you change the `Calibration` struct in `calibration.cpp`, or the meaning of its fields, bump `CAL_VERSION`. Every unit then falls back to 780 mV until it is recalibrated.

## Calibration Mode

Hold **Record** at power-up or reset.

**Display**
- `CAL` (calibrated) or `dEF` (default thresholds) on entry.
- Then one digit per channel, with A on the right: the top segment when the comparator output is high, the bottom segment when it's low.
- Tap **Record** to start recording a sweep (all decimal points lit), and tap again to stop.
- Hold **Record** for 2 seconds to calibrate from the recorded sweep: `CAL` when saved, `Err` when refused.

**Serial** (USB, any baud rate)
- Streams the sensor voltages and thresholds in mV at 20 Hz, in the Arduino IDE 2 Serial Plotter format: `A:812,B:790,C:1020,VrefA:780,VrefB:780,VrefC:780`.
- Ending a recording prints a `#` summary per channel. It covers the light (min) and dark (max) levels, the midpoint, and the switching points and margins at the current threshold. It also gives the suggested threshold and the number of invalid encoder transitions.
- Commands, one per line:
  - `A 820` (or `B`, `C`) sets that channel's threshold in mV until reset, without saving it.
  - `S` toggles the stream.
  - `X` clears the saved calibration.
- Lines starting with `#` use `key=value` tokens so the Serial Plotter ignores them.

In normal operation, each capture ends with `Done, N invalid transitions` on serial. Anything above 0 means counts were lost.
