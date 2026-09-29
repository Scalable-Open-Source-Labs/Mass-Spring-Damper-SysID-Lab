# Ordering Instructions

Sourcing a complete lab unit means placing orders with a handful of different vendors and fabrication services — PCB assembly, 3D printing, laser cutting, and a few off-the-shelf hardware suppliers. This doc groups everything by where you'd actually order it from, so each section is one checkout.

Quantities below are **per unit**. Multiply for a batch build.

## Order at a glance

| Vendor / service | What you get |
|---|---|
| [JLCPCB](https://jlcpcb.com) | Main board + carriage, fabricated and assembled |
| [JLC3DP](https://jlc3dp.com) (or in-house printer) | Case + Guide Rail + Spacer |
| [acrylicsonline.com.au](https://www.acrylicsonline.com.au) | Lid, laser-cut from 3mm clear acrylic |
| [nhtb.com.au](https://nhtb.com.au) | M3 fasteners |
| JLCMC | Knurled carriage-capture bolt |
| LCSC Electronics | Button caps |
| [RS Components](https://au.rs-online.com) | Extension springs |
| [Core Electronics](https://core-electronics.com.au) | USB-C cable |
| Commodity / no fixed vendor | Cable tie, threadlocker |

---

## Fabrication services

### PCB fabrication & assembly — JLCPCB

| Board | Soldermask / silkscreen | Assembly | Qty |
|---|---|---|---|
| `PCB/main-board/` (DynaLab) | White / black | Both sides assembled | 1 |
| `PCB/carriage/` | Red / white | Bottom side only assembled | 1 |

Follow [JLCPCB Ordering Instructions](JLCPCB-Ordering-Instructions.md) for a step-by-step walkthrough of both orders, including the placement checks that catch rotated parts. Use the fab files from the latest release, not any BOM or gerber files you find lying around locally, so what you order matches the current design.

### 3D printing — JLC3DP or in-house

| Part | File | Process | Qty |
|---|---|---|---|
| Case | [`Enclosure/Case - Resin Print.stl`](Enclosure/Case%20-%20Resin%20Print.stl) | Resin | 1 |
| Guide Rail | [`Enclosure/Guide Rail variant-hrs20-cwc09 - Resin Print.stl`](Enclosure/Guide%20Rail%20variant-hrs20-cwc09%20-%20Resin%20Print.stl) | Resin | 2 (both guides are identical — print the same file twice) |
| Spacer | [`Enclosure/Spacer - FDM or Resin Print.stl`](<Enclosure/Spacer - FDM or Resin Print.stl>) | FDM or resin | 2 |

Production units were printed via [JLC3DP](https://jlc3dp.com). This is an open-source project, so if your institution has a suitable printer, printing in-house is equally valid — no need to use JLC3DP specifically.

### Laser cutting (acrylic) — acrylicsonline.com.au

| Part | File | Material | Qty |
|---|---|---|---|
| Lid | [`Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid.pdf`](<Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid.pdf>) | 3mm clear acrylic, clear | 1 |

Submit [`Lid-dimensions-and-notes.png`](<Enclosure/Lid - Lasercut Acrylic - Clear 3mm/Lid-dimensions-and-notes.png>) alongside the cut file — it's a useful sanity check for the cutting service to confirm the vector was interpreted at the right scale/orientation before they cut.

[acrylicsonline.com.au](https://www.acrylicsonline.com.au) supplies both the 3mm clear acrylic stock and the laser-cutting service at a reasonable price.

---

## Off-the-shelf parts

### Fasteners — nhtb.com.au

| Part | Qty | Used for |
|---|---|---|
| [SC M3x8 304](https://nhtb.com.au/SC%20M3X8%20304.html) (short) | 12 | Guide mounting (6), board-to-enclosure (4), lid (2) |
| [SC M3x16 304](https://nhtb.com.au/SC%20M3X16%20304.html) (long) | 2 | Lid (2) |

### Carriage-capture bolt — JLCMC

| Part | Part # | Qty | Used for | Datasheet |
|---|---|---|---|---|
| Knurled bolt | `ESLD-C2-M3-L6` | 1 | Captures the carriage in the guide rails | [`Datasheets/ESLD.pdf`](Datasheets/ESLD.pdf) |

### Button caps — LCSC Electronics

| Part | Part # | Qty | Used for |
|---|---|---|---|
| Button cap, red | `SC215AB1` | 1 | Record button |
| Button cap, white | `SC215AE1` | 1 | Reset button |

### Springs — RS Components

| Part | Qty |
|---|---|
| [Steel Extension Spring, 27.2mm x 4mm OD](https://au.rs-online.com/web/p/extension-springs/0751663) | 2 |

### USB-C cable — Core Electronics

| Part | Part # | Qty | Used for |
|---|---|---|---|
| USB-C cable | `CE09402` | 1 | Connects the main board to the host — secured internally with a cable tie, trimmed to length during assembly |

### Consumables — no fixed vendor

| Item | Qty | Notes |
|---|---|---|
| Cable tie | 1 | Bunnings I/N [4431099](https://www.bunnings.com.au/crescent-100mm-x-2-5mm-black-cable-ties-100-pack_p4431099). The locking mechanism must be no more than **4mm deep** (the axis through which the tail passes through) — oversized locking mechanisms mechanically foul against the enclosure during installation. Confirm this dimension against any candidate part before bulk ordering |
| Threadlocker — Loctite 242 (or equivalent medium-strength) | small qty | Recommended on the knurled carriage-capture bolt to prevent loosening in use |
