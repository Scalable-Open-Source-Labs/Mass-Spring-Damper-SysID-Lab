# Releasing

A release is a tagged, tested snapshot containing only what's needed to manufacture and program one unit — not the KiCad/OnShape/firmware source. Source stays in the repo, for anyone who wants to inspect or fork the design; the release is for someone who just wants to build one.

## What a release contains

| Asset | Source |
|---|---|
| `firmware-{tag}.uf2` | Compiled from `firmware-mass-spring-damper-sys-id-lab/` |
| `main-board-fab-{tag}.zip` | Gerbers + BOM + CPL, regenerated fresh via the Fabrication Toolkit (see [PCB/README.md](PCB/README.md)) |
| `carriage-fab-{tag}.zip` | Same, for `PCB/carriage/` |
| `Case-{tag}.stl` | [`Enclosure/Case - Resin Print.stl`](<Enclosure/Case - Resin Print.stl>) |
| `Guide-Rail-{tag}.stl` | [`Enclosure/Guide Rail variant-hrs20-cwc09 - Resin Print.stl`](<Enclosure/Guide Rail variant-hrs20-cwc09 - Resin Print.stl>) |
| `Spacer-{tag}.stl` | [`Enclosure/Spacer - FDM or Resin Print.stl`](<Enclosure/Spacer - FDM or Resin Print.stl>) |
| `Lid-{tag}.pdf` | [`Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid.pdf`](<Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid.pdf>) |
| `Lid-dimensions-and-notes-{tag}.png` | [`Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid-dimensions-and-notes.png`](<Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid-dimensions-and-notes.png>) — include alongside `Lid-{tag}.pdf`, it's the sanity-check reference a cutting service uses to confirm scale/orientation |

`{tag}` is the release tag (e.g. `release-2026-08-16`) and goes on every asset uniformly, enclosure files included — they're just as versioned as the firmware or fab outputs, they'd just been missing the label. This is one shared label applied mechanically to everything in the release, not a per-file judgment call. It's also the only version baked into a filename: firmware's own internal `VERSION_MAJOR`/`VERSION_MINOR` stays out of asset names (it's already recorded in the release notes below, and in the binary itself) rather than mixing two numbering schemes into one string.

**Not included as files:** `Assembly-Instructions.md` and `Ordering-Instructions.md`. Both use paths relative to the repo (the assembly guide embeds images from `Images/`; the ordering doc links to `Enclosure/`, `PCB/README.md`, `Datasheets/`) — copying just the `.md` file into a flat release bundle breaks those links and the images render as missing. Instead, link both from the release notes as GitHub blob URLs pinned to the release tag (see template below) — GitHub resolves the relative image paths correctly when viewing a file at a specific ref, so the docs stay fully working without duplicating `Images/` into every release.

## Versioning

The release tag is its own product-level version, deliberately a different shape from the firmware version or the hardware revision — those move independently between releases (firmware `VERSION_MAJOR`/`VERSION_MINOR` in the `.ino`; hardware revision in the KiCad title block, currently `rev "1.2"` on `main-board`), and a bare `X.Y` tag would be indistinguishable from either at a glance. The tag just means "this combination was built and tested together." Record both underlying numbers in the release notes so anyone can trace a release back to exactly what it bundled.

**Tag format: `release-YYYY-MM-DD`** (e.g. `release-2026-08-16`), the date the release is cut. This repo isn't expected to release on any regular cadence — it'll mostly settle into a stable source repo after the first couple of releases — so the tag just records when, with no cadence implied. On the rare occasion two releases land the same day, append a letter (`-a`, `-b`).

## Steps

1. Confirm the working tree is clean and everything intended for this release is merged to `main`.
2. Regenerate PCB fab outputs fresh — for both `main-board` and `carriage` — via the KiCad Fabrication Toolkit plugin. Never reuse a local `production/` folder left over from earlier work; it may predate a schematic edit.
3. If firmware changed since the last release, bump `VERSION_MAJOR`/`VERSION_MINOR` in the `.ino` and commit that before tagging.
4. Compile the firmware and confirm the `.uf2` runs on real hardware.
5. Rename the generated files to match the naming convention above and zip the two fab-output folders.
6. Create the release commit. If necessary, create an empty commit to tag, when the release is a snapshot of work spread across many prior commits. It may not be the deliverable of whichever commit happens to be last, so it gets its own marker rather than borrowing an unrelated commit's meaning:
   ```
   git commit --allow-empty -m "Release release-2026-08-16"
   git tag release-2026-08-16
   git push origin main
   git push origin release-2026-08-16
   ```
7. Create the GitHub Release from that tag. Write release notes from the template below.
8. Attach all assets from the table above.
9. Publish.

## Release notes template

```md
## release-2026-08-16

Firmware: vA.B
Hardware: main-board revC.D, carriage revE.F

### What's in this release
Manufacturing and programming files only — gerbers/BOM/CPL for JLCPCB, 3D print and
laser-cut files, and a compiled firmware binary. For KiCad/OnShape source, clone the repo.

### What changed
Anything a builder needs to know before ordering or assembling — new/changed/removed parts,
sourcing changes, fit or compatibility notes for anyone with an already-built unit.

### Instructions
- [Ordering Instructions](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/blob/release-2026-08-16/Ordering-Instructions.md)
- [Assembly Instructions](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/blob/release-2026-08-16/Assembly-Instructions.md)
- [Programming Instructions](https://github.com/Scaleable-Open-Source-Labs/Mass-Spring-Damper-SysID-Lab/blob/release-2026-08-16/Programming-Instructions.md)
```
