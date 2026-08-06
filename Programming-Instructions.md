# Programming Instructions

## Flashing a Pre-compiled binary (Drag-and-drop programming)
Use this method for batch programming, or for applying a firmware to a deployed unit.

Download the latest `.uf2` firmware file from the repository [Releases](https://github.com/Scalable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/releases/latest)

Connect to a host computer with the USB cable. Unprogrammed boards (fresh from factory) will automatically enter Firmware Update mode.
> Programmed Units:
> If the unit has been programmed before, the process is different. Hold down the button on the back of the device *and then* connect to the host computer. Once plugged in, release the button.

An external Drive `RPI-RP2` should appear

Copy the `.uf2` firmware file onto the `RPI-RP2` drive.

Once the file transfer is complete, the unit will automatically run the new firmware.

The programmed unit should now operate as described in the [User Instructions](https://monasheng.gitbook.io/scalable-labs/mass-spring-damper-sysid). Move the carriage and observe the displacement reading updates sensibly.


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
- Debug Port: Disabled
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


