# Firmware

Arduino sketch for the RP2040 main board. It reads the linear encoder as the carriage moves, drives the 7-segment display, and exposes the captured motion data as a CSV over USB. The board enumerates as a mass-storage drive, so data can be pulled off without any host-side software.

For build/flash setup (Arduino IDE, board settings, library versions) and calibrating a unit whose encoder misbehaves, see [Programming-Instructions.md](../Programming-Instructions.md).

The calibration maths and the encoder decoder have host tests that run on a PC. See [firmware-tests](../firmware-tests/README.md).

## Files

| File | Role |
|---|---|
| `firmware-mass-spring-damper-sys-id-lab.ino` | Main sketch: mode state machine, encoder capture, CSV data buffer, test mode |
| `gpio.cpp` / `gpio.h` | Pin definitions and GPIO init for the encoder, button, and display. Per-channel comparator thresholds (PWM through an RC filter) and sensor voltage readings (ADC) |
| `calibration.cpp` / `.h` | Per-unit threshold calibration, saved to emulated EEPROM |
| `sweep.cpp` / `.h` | Calibration-mode sweep recording and analysis: where each comparator actually switches, the width of each encoder state, and the thresholds that space the edges best |
| `LedControlPatched.cpp` / `.h` | Fork of the [LedControl](https://wayoda.github.io/LedControl/) library, with local modifications, used to drive the 7-segment display |
| `ramdisk.h` | USB Mass Storage (virtual FAT drive) implementation used to export captured data as a CSV file. The 32 KB image holds about 2,500 rows; `generateCSV()` stops at the last whole row that fits and reports it on serial |

## Encoder thresholds

Each optical channel's comparator switches at its own threshold, set by PWM through a 10k/1µF filter. An uncalibrated unit uses 780 mV on every channel (`VREF_DEFAULT_MV` in `gpio.h`).

The three channels form a 6-state Johnson code, and each state should span 1 mm of the 6 mm strip period. Robustness is the width of the narrowest state. When it reaches 0, two channels switch at the same place and a count can be lost.

A threshold moves its channel's two edges in opposite directions, so thresholds can only trade width between opposite states (100/011, 110/001, 111/000). The narrowest state is widest when every channel reads light for exactly half of each period (50% duty).

Calibration therefore sets each channel's threshold for 50% duty, measured from a sweep. This is not the midpoint of the channel's light and dark levels:
- The light level clips, because the phototransistor saturates near 220 mV. That lifts the midpoint, so a midpoint threshold makes every channel light for more than half the period.
- Calibration version 1 used the midpoint, and on a clipping sensor it pushed two channels' edges together. Units calibrated that way revert to the defaults (`CAL_VERSION` 2).

The rules:
- Each switching point stays at least 200 mV inside the sweep's light and dark levels (`MIN_MARGIN_MV` in `sweep.h`). If 50% duty lies outside that window, the threshold stops at the margin.
- When a margin stops a channel short of 50%, the others give way. Pushing them to 50% regardless would squeeze a state between them and the stuck channel.
  - Calibration starts from 50% duty, or from the thresholds already running if those space the edges better.
  - A search then moves each threshold within its margins while the simulated narrowest state keeps widening.
  - On board 1, A and B clip so hard that 50% is out of reach, so C ends up a little under 50%. That takes the narrowest state from about 0.87 to 0.92 mm.
- A channel whose light and dark levels are closer than 500 mV is refused (`MIN_SPAN_MV`).
- Before saving, the comparators are simulated over the recorded sweep at the new thresholds. Calibration is refused unless every period decodes cleanly and every state is at least 0.5 mm wide (`MIN_STATE_WIDTH_MM`).
- The comparators' hysteresis isn't assumed. Every sweep measures where each comparator actually switched, and the simulation and the margins use those points. The nominal 100 mV band (330k feedback, `COMPARATOR_HYST_MV` in `gpio.h`) is only the fallback for a channel that didn't switch.

The thresholds, and the light and dark levels, are saved with a CRC in emulated EEPROM (the last 4 KB of flash), which UF2 firmware updates leave alone.

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
- Ending a recording prints a `#` summary. Each channel's line has, in mV:
  - the light (`min`) and dark (`max`) levels;
  - where the comparator switched at the current threshold (`rise`, `fall` and the band between them, `hyst`), with `hyst_src=meas` when measured from the sweep;
  - the margins those leave to the light and dark levels.

  It also gives:
  - `duty`, the fraction of each period the channel read light (`none` if the sweep was too short to measure it);
  - `half`, the threshold that makes it 0.5 (`clamped=light|dark` means a margin stopped it);
  - `suggest`, the threshold calibration would save.
- Then the width of each encoder state in mm, ideally 1 each:
  - `# hw` is measured from the comparators during the sweep.
  - `# sim` is simulated at the current thresholds, and should match `hw`.
  - `# half` and `# suggest` are simulated at the half-duty and suggested thresholds.
  - `min` is the narrowest state, the robustness figure.
  - `best` is what thresholds alone could reach, given where the sensors sit. `min` well below `best` points at the thresholds; both low points at the mechanics.
  - States are written with A as the left bit, so `w100` has A on a light stripe.
- The last line gives the position and the decoder's `recovered` and `lost` counts.
- The measurements are time-based, so sweep at a steady pace: about 2 seconds end to end, several times. Very slow sweeps are less accurate, because a hand's wobble is then a large fraction of the speed.
- Commands, one per line:
  - `A 820` (or `B`, `C`) sets that channel's threshold in mV until reset, without saving it.
  - `S` toggles the stream.
  - `X` clears the saved calibration.
- Lines starting with `#` use `key=value` tokens so the Serial Plotter ignores them.

In normal operation, each capture ends with `Done, N skipped states recovered, M counts lost` on serial.
- In the Johnson code, a jump of two states is unambiguous. It can come from a state the loop missed, or from two channels' edges crossing. The decoder counts it as ±2 instead of losing it, so a recovered skip costs a momentary 1 mm, seen as a 2 mm step in one CSV row.
- Only a jump to the opposite state, whose direction is unknown, loses counts. `lost` above 0 means the position is wrong.
- A steady nonzero `recovered` still means two edges sit close together. Check the unit's state widths in calibration mode.
