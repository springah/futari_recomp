**Windows and macOS.** A small fix release: the game no longer crashes after it fails to load, and the F4 overlay's VSync box now does what the F2 one does. Nothing changed in how the game plays.

**What changed since v1.5.9**

- **No more crash after a failed game load.** When the game's executable cannot be loaded (a damaged or incomplete `default.xex`), the error box was followed by a crash with heap corruption (0xC0000374). The process now exits cleanly. The same change makes it exit at once if the game ever ends by itself, as it already did when the window is closed. Same fix as in the Zuma and Daytona ports.
- **F4 overlay: the VSync box now means what the F2 one means.** It used to show the game's own `vsync` setting and change both that and the window's. With `present_vsync` off by default it read ticked while the window presented at once, and unticking it took the game off its 60 Hz clock. It now reads and sets `present_vsync` alone.
- Launcher settings page: a developer build that has the shaders built in no longer says "not prepared yet (Renut would draw nothing)". Public builds read as before.

How this release was checked: **Windows** — the staging script's checks (no game data in the zip, both renderers boot, the shader translator is built in), the failed-load case (error box, then a clean exit), and the F4 overlay opened on a default install, where the VSync box reads unticked. Ticking it was not tried. **macOS** — signature verified after the zip round-trip, the Renut fallback line, and a 25 s boot on the default renderer at 60 Hz (M3 Max); the failed-load case and the F4 box were only looked at on Windows.

Known limits are those of v1.5.9 (Renut is Vulkan-only and experimental on macOS).

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.10/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.10.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.9:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it).

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.10.zip` | `6b7695f169ae13a9869d034bed1f1ff37edb16ad5da221c8ac5071502d97bd8c` |
| `Mushihimesama-Futari-macOS-v1.5.10.zip` | `433cea8277cd11b2b0e0ae113647627ae9b1855a98c183e56e562964954c5e9c` |
