**Windows and macOS.** One change, for Windows on the default renderer: turning VSync on no longer slows the game down ([#8](https://github.com/springah/futari_recomp/issues/8)). The macOS bundle is the same build with the new version number; nothing changed in how it plays.

**What changed since v1.5.8**

- **Windows: VSync on no longer waits inside the game's frame.** With the F2 VSync box ticked, the window used to wait for the display's refresh every time it showed a frame. On some machines that wait held the game itself below its 60 frames a second: 58 to 59 in a measurement here on 2026-11-07 (the reason v1.5.7 turned the box off by default), and a plain slowdown for a player on a Ryzen 4750GE's integrated Radeon. VSync on now shows the newest finished frame at each refresh and waits for nothing, so the game keeps its own 60 Hz pace on any display, and fullscreen still does not tear. Measured here at 60.00 in a window and in fullscreen, on a 60 Hz and a 100 Hz monitor.
- **The box stays off by default.** Windowed, the desktop composes the picture, so nothing tears either way; tick it on F2 if fullscreen tears on your display.
- **For comparison:** `--present_vsync_wait=true` on the command line brings the old waiting path back. The log line `D3D12Presenter: Presenting with sync interval ...` now says which path is in use.
- **`make_shaders.py`:** a failed close of its own log file (seen once on a macOS Desktop folder) no longer hides a finished shader package behind a traceback or leaves `shader_work/` behind.

What this release has not had: a check by eye for tearing in fullscreen with the new VSync path (it follows from how Windows presents a frame that is not allowed to tear), and a run on a machine that showed the slowdown. If VSync on still slows your game, or fullscreen tears with the box ticked, please say so on #8 with the newest `logs\futari_*.log`: it prints the game's true frame rate every 10 seconds (`[vblank] ... guest Swaps (60.00 fps)`).

Known limits are those of v1.5.8 (Renut is Vulkan-only and experimental on macOS; Renut has its own vsync switch, `renut_vsync`, which this release does not touch).

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.9/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.9.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.8:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it; until then the game runs on the default renderer and says so).

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.9.zip` | `240ff146726a46679e908c5c3508794921a663f1e2b9d2a54d9b7c094949c012` |
| `Mushihimesama-Futari-macOS-v1.5.9.zip` | `cbab5339cb732361e7329d07f430418ab9c298b0a10c1eda6d341c727ae58b5e` |
