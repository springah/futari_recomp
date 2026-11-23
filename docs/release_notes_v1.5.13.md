**Windows and macOS.** Fixes to the setup screen, the settings file and the release tooling, from a read-through of the port's own code before handing it on ([#11](https://github.com/springah/futari_recomp/issues/11)). Nothing changes in the game itself or in either renderer. The macOS zip was attached later the same day, from the same tag as the Windows one.

**What changed since v1.5.12**

- **A damaged disc image no longer ends the program during setup.** A directory table that listed itself as its own subdirectory, or one with an absurd size, crashed the game while it was extracting. Such an image is now refused with a message, and file names Windows cannot store as the file they name (NUL, CON, a trailing dot, `?`, `*` and the like) are refused as unsafe. `extract_iso.py` has the same checks.
- **An extraction that did not finish is asked for again.** A cancelled or failed extraction left `default.xex` behind, and every later start took that for an installed game and ran on incomplete data. The extractor now keeps a marker file, `futari_extract_incomplete`, in `assets\` until everything has come out whole; while it is there the setup screen comes back and says why. `extract_iso.py` keeps the same marker.
- **A full disk can no longer cut `futari.ini` short.** Saving settings (F4 "Save to config", F2, the title screen) replaced the file without checking that the new copy had been written.
- **A failed shader dump reports it.** `--futari_renut_dump_shaders` always ended with exit code 0; a failed dump now ends with 2, which the setup screen's shader step and `make_shaders.py` both test.
- **numpy is named where it is needed.** `extract_assets.bat`'s extra early-alpha shader pass needs Python's numpy module and used to do nothing, silently, without it; it now says so, and `RUN.txt` and the README name it. `extract_iso.py` no longer needs Python 3.9.
- For people building or maintaining the port: `stage_win_zip.sh` finds a Python that runs, refuses a version that is not the tree's and the exe's, and checks the `futari.ini` it ships; the CI identity check accepts any GitHub noreply address, so a second maintainer can push; `build.ps1` installs numpy with xxhash.

How this release was checked: **Windows** — the staging script's checks (no game data in the zip, the default renderer and Renut both boot, the shader translator is built in), and the zip's file list is the same as v1.5.12's. On the unzipped build, with made-up test images that contain no game data: an image with a looping directory is refused and the game stays up (v1.5.12 dies on it), the next start shows the setup screen again, a real extraction over it then completes (54 files, 42 shaders translated) and the game starts; a plain start and a Renut start both run. A failed shader dump ends with exit code 2 (v1.5.12: 0). **macOS** (M3 Max) — signature verified after the zip round-trip, the zip's file list is the same as v1.5.12's, and on the unzipped app: a 25 s boot on the default renderer at 60 fps with no errors, the Renut fallback line, the made-up image with a looping directory refused with the app still up and the setup screen back on the next start, a shader package made with the shipped `make_shaders.py` (42 shaders), a 25 s Renut start with no errors, and a failed shader dump ending with exit code 2.

Not checked: a full disk, `build.ps1`, and on macOS a real extraction from a disc image or console install (the game data used there was already extracted).

Known limits are those of v1.5.12.

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.13/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.13.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.12:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it).

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.13.zip` | `ae85ec6a1da4bda615a9d3c2870127756033f52804f4181e7085aada8cce2ba9` |
| `Mushihimesama-Futari-macOS-v1.5.13.zip` | `2157e051e409c0951e60033badef40218b7729212abda69b8163da91ebeb13be` |
