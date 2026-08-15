# Releasing

A release is a tagged, tested snapshot containing only what's needed to manufacture and program one unit — not the KiCad/OnShape/firmware source. Source stays in the repo, for anyone who wants to inspect or fork the design; the release is for someone who just wants to build one.

## What a release contains

| Asset | Source |
|---|---|
| `firmware-vX.Y.uf2` | Compiled from `firmware-mass-spring-damper-sys-id-lab/` |
| `main-board-fab-vX.Y.zip` | Gerbers + BOM + CPL, regenerated fresh via the Fabrication Toolkit (see [PCB/README.md](PCB/README.md)) |
| `carriage-fab-vX.Y.zip` | Same, for `PCB/carriage/` |
| `Case.stl` | [`Enclosure/Case - Resin Print.stl`](<Enclosure/Case - Resin Print.stl>) |
| `Guide-Rail.stl` | [`Enclosure/Guide Rail variant-hrs20-cwc09 - Resin Print.stl`](<Enclosure/Guide Rail variant-hrs20-cwc09 - Resin Print.stl>) |
| `Spacer.stl` | [`Enclosure/Spacer - FDM or Resin Print.stl`](<Enclosure/Spacer - FDM or Resin Print.stl>) |
| `Lid.pdf` | [`Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid.pdf`](<Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid.pdf>) |

**Not included as files:** `Assembly-Instructions.md` and `Ordering-Instructions.md`. Both use paths relative to the repo (the assembly guide embeds images from `Images/`; the ordering doc links to `Enclosure/`, `PCB/README.md`, `Datasheets/`) — copying just the `.md` file into a flat release bundle breaks those links and the images render as missing. Instead, link both from the release notes as GitHub blob URLs pinned to the release tag (see template below) — GitHub resolves the relative image paths correctly when viewing a file at a specific ref, so the docs stay fully working without duplicating `Images/` into every release.

## Versioning

The release tag is its own product-level version — it is not the same number as the firmware version or the hardware revision, because those move independently between releases (firmware `VERSION_MAJOR`/`VERSION_MINOR` in the `.ino`; hardware revision in the KiCad title block, currently `rev "1.2"` on `main-board`). The tag just means "this combination was built and tested together." Record both underlying numbers in the release notes so anyone can trace a release back to exactly what it bundled.

## Steps

1. Confirm the working tree is clean and everything intended for this release is merged to `main`.
2. Regenerate PCB fab outputs fresh — for both `main-board` and `carriage` — via the KiCad Fabrication Toolkit plugin. Never reuse a local `production/` folder left over from earlier work; it may predate a schematic edit.
3. If firmware changed since the last release, bump `VERSION_MAJOR`/`VERSION_MINOR` in the `.ino` and commit that before tagging.
4. Compile the firmware and confirm the `.uf2` runs on real hardware.
5. Rename the generated files to match the naming convention above and zip the two fab-output folders.
6. Tag the commit (e.g. `git tag v1.3 && git push origin v1.3`).
7. Create the GitHub Release from that tag. Write release notes from the template below.
8. Attach all assets from the table above.
9. Publish.

## Release notes template

```md
## vX.Y

Firmware: vA.B
Hardware: main-board revC.D, carriage revE.F

### What's in this release
Manufacturing and programming files only — gerbers/BOM/CPL for JLCPCB, 3D print and
laser-cut files, and a compiled firmware binary. For KiCad/OnShape source, clone the repo.

### Instructions
- [Ordering Instructions](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/blob/vX.Y/Ordering-Instructions.md)
- [Assembly Instructions](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/blob/vX.Y/Assembly-Instructions.md)
- [Programming Instructions](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/blob/vX.Y/Programming-Instructions.md)
```
