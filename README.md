[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

# Mass Spring Damper SysID Lab

A low-cost dynamics lab that allows students to perform System Identification on a real Mass-Spring-Damper system.

This is the source repo — hardware, firmware, and enclosure design files. If you're looking for usage instructions or lab notes, head to the [user guide](https://monasheng.gitbook.io/scalable-labs/mass-spring-damper-sysid).

*Designed by Michael Ruppe for Monash University in partnership with MathWorks®*

![Mass-spring-damper system diagram](PCB/main-board/images/image_2000_D_lineart.png)

## Repo map

| Path | What it is |
|---|---|
| [`PCB/`](PCB/README.md) | KiCad hardware source for the main board and carriage |
| [`Enclosure/`](Enclosure/README.md) | 3D-printable case, guide rail, and lasercut acrylic lid |
| [`firmware-mass-spring-damper-sys-id-lab/`](firmware-mass-spring-damper-sys-id-lab/README.md) | Arduino sketch for the RP2040 main board |
| [`Images/`](Images/) | Photos used in the assembly guide |
| [`Assembly-Instructions.md`](Assembly-Instructions.md) | Step-by-step mechanical build guide |
| [`Programming-Instructions.md`](Programming-Instructions.md) | Flashing and firmware dev setup |
| [`Ordering-Instructions.md`](Ordering-Instructions.md) | Bill of materials and where to order/fabricate each part |

## Getting started

- [Ordering Instructions](Ordering-Instructions.md)
- [Assembly Instructions](Assembly-Instructions.md)
- [Programming Instructions](Programming-Instructions.md)
- [Latest Project Release](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/releases/latest) - A downloadable bundle of all project files to complete this project: 3D Printing, PCBA, Lasercutting

## License

This project is licensed under the [MIT License](LICENSE).

Bundled third-party content — KiCad symbol/footprint libraries and vendor 3D step models under `PCB/main-board/packages3D/` and `PCB/carriage/CAD/`, and the patched `LedControl` fork used by the firmware (see [firmware README](firmware-mass-spring-damper-sys-id-lab/README.md)) — retains its original license terms.
