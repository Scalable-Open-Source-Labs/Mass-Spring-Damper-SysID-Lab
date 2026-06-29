# Programming Instructions

Download the latest `.uf2` firmware file from the repository [Releases](https://github.com/Scalable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/releases/latest)

Connect to a host computer with the USB cable. Unprogrammed boards (fresh from factory) will automatically enter Firmware Update mode.
> Programmed Units:
> If the unit has been programmed before, the process is different. Hold down the button on the back of the device *and then* connect to the host computer. Once plugged in, release the button.

An external Drive `RPI-RP2` should appear

Copy the `.uf2` firmware file onto the `RPI-RP2` drive.

Once the file transfer is complete, the unit will automatically run the new firmware.

The programmed unit should now operate as described in the [User Instructions](https://monasheng.gitbook.io/scalable-labs/mass-spring-damper-sysid). Move the carriage and observe the displacement reading updates sensibly.
