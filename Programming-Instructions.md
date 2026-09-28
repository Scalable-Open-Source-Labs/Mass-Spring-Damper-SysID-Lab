# Programming Instructions

## Flashing a Pre-compiled binary (Drag-and-drop programming)
Use this method for batch programming, or for applying a firmware to a deployed unit.

Download the latest `.uf2` firmware file from the repository [Releases](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/releases/latest)

> Use the firmware from the same release as your circuit boards. Firmware 2.0 and later only work on main board rev 2.0. Earlier boards (main board rev 1.x, including the pilot run in release-2026-08-17) need the firmware from that release.

Connect to a host computer with the USB cable. Unprogrammed boards (fresh from factory) will automatically enter Firmware Update mode.
> Programmed Units:
> If the unit has been programmed before, the process is different. Hold down the button on the back of the device *and then* connect to the host computer. Once plugged in, release the button.

An external Drive `RPI-RP2` should appear

Copy the `.uf2` firmware file onto the `RPI-RP2` drive.

Once the file transfer is complete, the unit will automatically run the new firmware.

## First start: automatic calibration
The first time a newly programmed unit starts, it calibrates its optical encoder to its own three sensors. This keeps the displacement reading reliable, since sensors vary between production batches. The display shows `SLd` (slide).

1. Slide the carriage from one end of its travel to the other and back, at a steady pace of about 2 seconds per slide. A brief pause at each end helps, but isn't needed. The three dots on the display fill from left to right as the unit collects enough good data.
    - `SLo` means you are sliding too fast. Slow down.
    - `Err` means the unit is starting its recording again. Keep sliding.
2. Keep going until the display shows `CAL`. It shows `CAL` by itself, usually within about ten seconds, then switches to the displacement reading. There is no button to press.
3. Let the carriage come to rest and press **Reset** to zero the reading. Pluck the carriage: the reading should return to 0 ±1 mm.

If the display instead shows `E-A`, `E-b` or `E-C`, that optical sensor is faulty or misaligned. If it shows `E-S`, the sensors are misplaced relative to each other. Either way the unit fails. These codes stay on the display.

The calibration is stored on the unit and kept through later firmware updates, so a unit calibrates like this only once. A unit that was calibrated with earlier firmware starts normally.

The programmed unit should now operate as described in the [User Instructions](https://monasheng.gitbook.io/scalable-labs/mass-spring-damper-sysid).

> Do not calibrate a 'naked PCB', where you might touch the circuit board under the sensors or the amplifier circuitry. Touching this area will invalidate the calibration.


## Recalibrating the optical encoder
Recalibrate if the circuit board or carriage is replaced, or if the displacement reading no longer returns to where it started. No computer is needed: the unit's display gives all the feedback.

1. Hold down the **Record** button, then connect the USB cable (or press and release **Reset**). Keep holding **Record** until the display shows one of these, then release it:
    - `CAL`: the unit is already calibrated. Recalibrating is fine.
    - `dEF`: the unit isn't calibrated yet and is running default settings.

   After a moment, each digit shows one sensor as a bar at the top or bottom of the digit, which jumps as the carriage moves.
2. Tap **Record**. All three decimal points light up: the unit is recording.
3. Slide the carriage from one end of its travel to the other and back, at a steady pace of about 2 seconds per slide. Do this at least four times (two round trips). Steady matters more than slow.
4. Hold **Record** until the display changes, about 2 seconds:
    - `CAL`: calibration saved.
    - `Err`: nothing was saved, and any earlier calibration is kept. Tap **Record** and repeat from step 3, making sure the carriage reaches both ends at a steady pace. If `Err` keeps appearing, the unit has a faulty or misaligned sensor.
5. Slide the carriage end to end again. All three digits should now jump between top and bottom. If one doesn't, the unit has a faulty sensor.
6. Press **Reset** to return to normal operation. From a zeroed position, pluck the carriage and it should return to zero or +- 1mm

> Do not perform the procedure on a 'naked PCB' where you might be able to touch the circuit board under the sensors or amplifier circuitry. Touching this area will invalidate the calibration.


## IDE Programming
This method is for actively developing new code.

Requires:
- Arduino IDE
- Board profile: Raspberry Pi Pico/RP2040/RP2350 by Earle F. Philhower, III - v5.5.0 or newer. Refer to the [repo](https://github.com/earlephilhower/arduino-pico) for installation instructions.
- Libraries:
    - [Adafruit TinyUSB Library](https://github.com/adafruit/Adafruit_TinyUSB_Arduino) by Adafruit 3.7.4 or newer
    - [LedControl](https://wayoda.github.io/LedControl/) by Eberhard Fahle. 1.0.6 or newer


Use the following board settings (Tools > Board). Most settings are default. Settings that need attention are in **bold**

- **Board: Generic RP2040**
- **Port: UF2_Board** (this option may only be available once the device is connected, and in BOOT mode)
- **Boot Stage 2: W25Q64JV QSPI/4**
- Debug Level: None
- Debug Port: Disabled (never Serial1, whose pins carry encoder signals on rev 2.0 boards)
- C++ Exceptions: None
- Flash Size: 2MB (No FS)
- CPU Speed: 200MHz
- IP/Bluetooth Stack: IPV4 only
- Optimise: Small
- Operating System: None
- Profiling: Disabled
- RTTI: Disabled
- Stack Protector: Disabled
- Upload Method: Default (UF2)
- **USB Stack: Adafruit TinyUSB**


