# Programming Instructions

## Flashing a Pre-compiled binary (Drag-and-drop programming)
Use this method for batch programming, or for applying a firmware to a deployed unit.

Download the latest `.uf2` firmware file from the repository [Releases](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/releases/latest)

> Use the firmware from the same release as your circuit boards. Firmware 2.0 and later only work on main board rev 2.0. Rev 1.2 boards (release-2026-08-17) need the firmware from that release.

Connect to a host computer with the USB cable. Unprogrammed boards (fresh from factory) will automatically enter Firmware Update mode.
> Programmed Units:
> If the unit has been programmed before, the process is different. Hold down the button on the back of the device *and then* connect to the host computer. Once plugged in, release the button.

An external Drive `RPI-RP2` should appear

Copy the `.uf2` firmware file onto the `RPI-RP2` drive.

Once the file transfer is complete, the unit will automatically run the new firmware.

The programmed unit should now operate as described in the [User Instructions](https://monasheng.gitbook.io/scalable-labs/mass-spring-damper-sysid). Move the carriage and observe the displacement reading updates sensibly.

A newly programmed unit must be calibrated next.


## Calibrating the optical encoder
If a freshly-programmed unit does not behave, it may need calibrating. Calibration tunes the unit to its own three optical sensors, which vary between production batches, so the displacement reading stays reliable. The calibration is stored on the unit (EEPROM) and kept through firmware updates. No computer is needed: the unit's display gives all the feedback.

Recalibrate if the circuit board or carriage is replaced, or if the displacement reading no longer returns to where it started.

1. Hold down the **Record** button, then connect the USB cable (or press and release **Reset**). Keep holding **Record** until the display shows one of these, then release it:
    - `CAL`: the unit is already calibrated. Recalibrating is fine.
    - `dEF`: the unit isn't calibrated yet and is running default settings.

   After a moment, each digit shows one sensor as a bar at the top or bottom of the digit, which jumps as the carriage moves.
2. Tap **Record**. All three decimal points light up: the unit is recording.
3. Slide the carriage very slowly from one end of its travel to the other, a few times.
4. Hold **Record** until the display changes, about 2 seconds:
    - `CAL`: calibration saved.
    - `Err`: nothing was saved, and any earlier calibration is kept. Tap **Record** and repeat from step 3, making sure the carriage reaches both ends. If `Err` keeps appearing, the unit has a faulty sensor.
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


