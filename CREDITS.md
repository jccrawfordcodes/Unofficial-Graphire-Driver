# Graphire Driver: Credits and Licenses

Graphire Driver is a user-mode tablet driver for the Wacom ET-0405A-U (Graphire2 4x5, USB `056A:0011`) on Windows 10/11. It was written from scratch. No third-party source code is copied into it. The sources below supplied protocol knowledge or device facts, or are linked into the executable.

## Protocol and device information

| Source | License | What was used | How |
|---|---|---|---|
| [Linux kernel Wacom driver](https://github.com/torvalds/linux/blob/master/drivers/hid/wacom_wac.c) (`drivers/hid/wacom_wac.c`, `wacom_graphire_irq`) | GPL-2.0 | Graphire report layout: proximity and tool bits, X/Y position, pressure bits, stylus and mouse buttons, wheel sign, tablet ranges (10206 × 7422, 511 pressure) | Reimplemented from knowledge of the driver. `src/graphire.c` follows the structure of `wacom_graphire_irq` closely (see note below). |
| [OpenTabletDriver](https://github.com/OpenTabletDriver/OpenTabletDriver): [`ET-0405A-U.json`](https://github.com/OpenTabletDriver/OpenTabletDriver/blob/master/OpenTabletDriver.Configurations/Configurations/Wacom/ET-0405A-U.json) and `BambooReportParser.cs` | LGPL-3.0 | Facts only: product ID `0x0011`, 8-byte input report, feature report `02 02` to switch to Wacom mode, check of the mouse-tool bit | Used to confirm details. No code taken. |
| [OpenBSD Graphire4 notes](https://mongers.org/openbsd/graphire4) (mongers.org) | Not stated | Facts only: coordinate and pressure ranges, digitizer collection layout | Used to confirm details. |
| [linuxwacom/wacom-hid-descriptors](https://github.com/linuxwacom/wacom-hid-descriptors) | Not stated | Background reference | Consulted; nothing used directly. |

## Linked into the executable

| Component | License | Obligations |
|---|---|---|
| [MinGW-w64](https://www.mingw-w64.org/) runtime and headers | Permissive (ZPL 2.1 / public domain portions) | None for binaries |
| GCC runtime (`libgcc`), statically linked | GPL-3.0 with GCC Runtime Library Exception | None for binaries built with GCC |
| Windows APIs (HID, SetupAPI, `InjectSyntheticPointerInput`, Shell tray) | Microsoft system libraries | Called only; no Microsoft sample code used |

## Original assets

- **Icon** (`res/app.ico`): generated for this project.
- **Source, settings file, README**: written for this project.

## Licensing note

Protocol facts such as bit positions and value ranges generally aren't copyrightable. However, the report parser (`src/graphire.c`, about 40 lines) mirrors the logic of GPL-2.0 Linux code closely enough to be a gray area.

- **Personal use:** no action needed.
- **Sharing or publishing:** the cautious choice is to release the project under **GPL-2.0** (or GPL-2.0-or-later) and keep this credits file. Alternatively, license the parser under GPL-2.0 and the rest however you like, provided the parser stays clearly credited.

To adopt GPL-2.0, add the official license text as `LICENSE` from <https://www.gnu.org/licenses/old-licenses/gpl-2.0.txt> and a header like this to each source file:

```
Graphire Driver - user-mode driver for Wacom Graphire2 tablets
SPDX-License-Identifier: GPL-2.0-or-later
Report format derived from the Linux kernel Wacom driver (drivers/hid/wacom_wac.c).
```

## Disclaimer

Wacom and Graphire are trademarks of Wacom Co., Ltd. This project is not affiliated with or endorsed by Wacom.
