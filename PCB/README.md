# PCB

KiCad hardware source for the lab. Two independent projects, opened separately (not sub-sheets of one project). Built with KiCad 9 or newer.

| Project | Purpose |
|---|---|
| [`main-board/`](main-board/) | RP2040 controller board ("DynaLab") — reads the linear encoder, drives the 7-segment display, exposes captured motion data over USB as a virtual FAT drive |
| [`carriage/`](carriage/) | The moving carriage PCB that rides the guide rail, carrying the linear encoder strip |

## Datasheets

[`Datasheet/`](Datasheet/) holds datasheets for every part placed on a board here, main board or carriage alike — there's no per-project split. Each symbol's `Datasheet` field links to it with a path relative to its own project folder (e.g. `../Datasheet/SMTSO.pdf`), so this directory should only ever contain files that a `.kicad_sch` actually references.

Datasheets for parts that aren't placed as KiCad components — hand-assembly hardware sourced in [Ordering-Instructions.md](../Ordering-Instructions.md), like the carriage-capture bolt — belong in the root [`Datasheets/`](../Datasheets/) directory instead, not here.

## Generating fabrication outputs

Gerbers, BOM, and pick-and-place files are not committed to this repo — they're derived from the `.kicad_pcb`/`.kicad_sch` source, and a stale committed copy is worse than none. Each project folder includes a `fabrication-toolkit-options.json`, pre-configured for the [KiCad Fabrication Toolkit](https://github.com/bennymeg/Fabrication-Toolkit) plugin:

1. Install the Fabrication Toolkit plugin in KiCad.
2. Open the relevant `.kicad_pcb`.
3. Run the plugin. Outputs are written to a local `production/` folder (gitignored).

Always regenerate from the current source files before ordering.
