# Firmware host tests

These tests run on a PC, not the board. They compile the sketch's own `sweep.cpp`, `calibration.cpp` and `gpio.cpp`, plus the decoder copied out of the `.ino`, against small stand-ins for the Arduino API (`Arduino.h`, `EEPROM.h`).

Run them with bash and g++ (C++17):

```sh
./firmware-tests/run.sh
```

- **Output:** each test's summary and any failures. The script exits non-zero if anything fails.
- **Where files go:** the builds and the calibration test's full log go to `firmware-tests/build/`, which git ignores.
- **When to run them:** after any change to `sweep.cpp`, `calibration.cpp`, `gpio.cpp` or `processEncoderChange()`. If that function is renamed, `run.sh` stops with an error until the name in its awk line is updated.

## Calibration test (`calibration_test.cpp`)

A synthetic hand sweeps a synthetic encoder, and a copy of the test-mode loop reads it. The expected answers come from the physical model, never from the firmware's own maths.

- **The model:**
  - 3 mm bars at a 6 mm pitch, and a blurred light spot;
  - a phototransistor on a 10k pull-up that clips when it saturates;
  - comparators whose hysteresis and offsets differ from nominal.
- **The hand and loop:**
  - smooth passes with a wobble, and pauses;
  - loop jitter, with ADC reads trailing the pin read;
  - timestamps near the 32-bit wrap.
- **Checks:**
  - measured switching points and state widths against the model;
  - the half-duty threshold giving 3 mm of light in every 6 mm;
  - suggested thresholds against a grid search for the true best spacing;
  - the switching-point fallbacks;
  - `calibrateFromSweep()` saving, and refusing short sweeps, weak sensors and misplaced sensors.
- **Scenarios:**
  - the old midpoint thresholds;
  - deep clipping, and unclipped sensors;
  - slow, jerky and fast sweeps, and a long one;
  - mixed sensor strengths;
  - board 1, fitted to its sweeps of 2026-09-28.
- **First-boot calibration (`autocal.cpp`):** a technician slides until the unit decides for itself. Each case checks what it saves, or the fault it names:
  - steady slides, short rests, no rests at all, a rushed and sloppy hand, and a wobbly one, each with each analysis stalling the loop for 350 ms while the hand keeps moving;
  - deep clipping, unclipped sensors, and a channel that never switches at the default threshold;
  - a weak sensor, and misplaced sensors;
  - two plucks (fast swings don't count, and anything saved must still be right);
  - a carriage jiggled on the bench, which must never save or fault.

## Decoder test (`decoder_test.cpp`)

Feeds pin states to the sketch's real `processEncoderChange()`:
- single steps;
- skipped states and crossed edges, which should count as ±2;
- jumps to the opposite state, whose counts should be lost;
- 2000 random walks, where the count must follow the true position.
