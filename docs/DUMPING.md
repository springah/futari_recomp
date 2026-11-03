# Dumping your own copy

This port ships no game data. You provide it from a copy you own — a retail
**Mushihimesama Futari Ver. 1.5** disc (title ID `435A07D7`) and, optionally, the
marketplace DLC bought on that console.

Everything below reads data off hardware you own. Nothing here involves
downloading game files.

## What the port actually needs

| Goes in | What it is | Required |
|---------|-----------|----------|
| `assets/` | the disc contents — `default.xex` plus the `media/` tree | yes |
| `dlc/` | the marketplace DLC, as STFS packages, filenames unchanged | no |

Title updates are **not** used. The runtime looks for a `default.xexp` patch, logs
that there isn't one, and carries on — Ver. 1.5 is already the final revision.

## What you'll need

- Your Xbox 360 and the game disc.
- A way to read console-formatted (FATX) storage on a PC — either an **Xbox 360 HDD
  transfer cable / SATA-to-USB adapter** for the internal drive, or a **USB stick
  formatted by the console** that you copy content onto from the dashboard
  (*Settings → System → Storage*).
- On the PC: [FATXplorer](https://fatxplorer.eaton-works.com/) to browse FATX
  storage. That's it — the extractor here reads the console's own formats
  directly, so no conversion tools are needed.

## Route A — the game installed on the console (easiest)

Install the disc to the console first (*Settings → System → Storage → select your
disc → Install*). The install — and any marketplace DLC bought on the console —
lives under one folder named after the game's title ID:

```
Content\0000000000000000\435A07D7\
├── 00007000\   the installed disc (a "Games on Demand" container)
└── 00000002\   the marketplace DLC packages, if you have them
```

1. Connect the console's drive (or the USB stick you copied the install onto) to
   your PC and open it in FATXplorer.
2. Copy the whole `435A07D7` folder out to your PC.
3. Drag that folder onto `extract_assets.bat` — or run:

   ```sh
   python scripts/extract_iso.py "path/to/435A07D7" --out assets/ --dlc-out dlc/
   ```

That's the whole route: it verifies your copy against the container's own
integrity hashes (so a bad copy is caught immediately, with the file named),
extracts the game into `assets/`, and installs any DLC packages it finds into
`dlc/`. Nothing needs converting first, and a corrupt or incomplete copy cannot
slip through to the build. In a release zip the same step ends by translating
the game's shaders for the Renut renderer into `vulkan_spirv.bin` beside
`futari.exe` (see `RUN.txt`); the default renderer does not need that file.

## Route B — dumping the disc directly

Reading an Xbox 360 disc at full size needs a PC optical drive whose firmware can
be persuaded to read the XGD layout, which in practice means a handful of specific
Lite-On / BenQ / Samsung models. If you have one of those and it's already set up,
dump the disc to an ISO, then drag the `.iso` onto `extract_assets.bat` (or run
`python scripts/extract_iso.py game.iso --out assets/`). If you don't, Route A is
far less trouble — a disc install reads the same data off the disc using the
console's own drive.

## DLC

Route A step 3 already handles this — packages found under `00000002` are
validated (right console format, right title ID) and copied into `dlc/`
automatically. If you'd rather do it by hand:

```
Content\0000000000000000\435A07D7\00000002\<long hex filename>
```

Copy those files into a `dlc/` folder next to `futari.exe`. **Leave the filenames
alone** — the port sniffs each file's first four bytes for the STFS magic
(`LIVE`, `CON `, `PIRS`), so the console's hex names are fine. The retail set is
two packages, roughly 264 MB and 56 KB. In FATXplorer these also show up under
*Shared Items → DLC*.

## Checking you got the right files

These are the executables from a retail Ver. 1.5 disc. If yours match, the dump is
good; if the sizes match but the hashes don't, you likely have a different revision
or region rather than a bad dump.

| File | Bytes | MD5 |
|------|------:|-----|
| `assets/default.xex` | 2,760,704 | `46A2E23336131DFFFE93DB134F8640DE` |
| `assets/media/Module/reco_normal.xex` | 3,547,136 | `FFB7D930EDC1D0D048F1BFB2616F652E` |
| `assets/media/Module/reco_abnormal.xex` | 3,547,136 | `62788E41F3E1C83957FE86F1BF94031E` |
| `assets/media/Module/palm_normal.xex` | 3,579,904 | `934E83E3FFB393C400CCE975CDEF0493` |
| `assets/media/Module/palm_abnormal.xex` | 3,678,208 | `25400FE6923FB32B9FC5E609C5963439` |
| `assets/media/Module/hirow_normal.xex` | 3,547,136 | `FFCF35D940C95CFA417B094514EF92DA` |
| `assets/media/Module/hirow_abnormal.xex` | 3,547,136 | `DDCDFCCB16B44E24B2092F61FBA9B4C3` |
| `assets/media/Module/kiniro_normal.xex` | 3,481,600 | `5A3F373600D5CD09088BEAD1A122C50C` |
| `assets/media/Module/kiniro_abnormal.xex` | 3,481,600 | `1485672D50B038EA633A255958D01824` |

The bulk data isn't hashed here, but should be present and roughly this size:
`media/Rom/cg_v154hi.bin` and `cg_v102hi.bin` ~212 MB each, `cg_v154.bin` and
`cg_v102.bin` ~58 MB each, `media/Sound/BGM.xwb` ~183 MB, plus the `font`,
`motion`, `text`, and `wall` folders.

Check a file on Windows with:

```powershell
Get-FileHash assets\default.xex -Algorithm MD5
```

The real test is launching: the log next to the exe should report all 8 modules
registered, and — if you added DLC — a line per package:

```
DLC: InstallContent('496F816DD4B5...') -> 0x00000000
```

`0x00000000` is success. DLC unpacks once into `Documents\futari`, so later
launches say `already installed, skipping` instead.

## If something's wrong

- **The extractor reports a data file "fails its hash" or "is incomplete"** — the
  copy off the console's storage went bad (loose adapter, interrupted copy).
  Nothing was harmed: re-copy the `435A07D7` folder and run it again.
- **`extract_iso.py` says it can't find the XDVDFS volume descriptor** in an ISO —
  the file isn't a plain XDVDFS ISO; some dumps are split or truncated.
  `extract-xiso` is a useful second opinion.
- **The game starts but modules fail to load** — `media/Module/` is incomplete;
  there should be 8 `.xex` files there.
- **DLC lines never appear in the log** — the `dlc/` folder must sit next to
  `futari.exe`, and the files must be the raw STFS packages, not zipped or renamed
  with an added extension.
