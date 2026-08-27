# PCB

## KiCad Projects

| Project | Purpose |
|---|---|
| [`main-board/`](main-board/) | RP2040 controller board. Reads the linear encoder, drives the 7-segment display, exposes captured motion data over USB as a virtual FAT12 drive |
| [`carriage/`](carriage/) | The moving carriage PCB that rides the guide rail, carrying the linear encoder strip |

## Datasheets

[`Datasheet/`](Datasheet/) Each schematic symbol's `Datasheet` field links to it with a path relative to its own project folder (e.g. `../Datasheet/SMTSO.pdf`), so this directory should only ever contain files that a `.kicad_sch` actually references. Call up a datasheet from within the schematic editor by hovering over a symbol and pressing the 'D' key.

Datasheets for parts that aren't placed as KiCad components (hand-assembly hardware sourced in [Ordering-Instructions.md](../Ordering-Instructions.md), like the carriage-capture bolt) belong in the root [`Datasheets/`](../Datasheets/) directory instead, not here.
