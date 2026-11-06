**Windows and macOS.** A fix release on top of v1.5.5. The default renderer is unchanged; the fixes are around Renut and the logs.

**What changed since v1.5.5**

- **Renut: the VSync box works, and Renut has its own vsync line.** On Renut the F2 VSync box always showed off and its save changed nothing: the setting it read belongs to the default renderer's plugin, which a Renut session never loads. `futari.ini` now has a `renut_vsync` line under `[Display]`, **off by default** (frames shown at once, the lowest latency, as Renut always ran); on waits for the display's refresh, which can hitch on a panel that is not exactly 60 Hz. While Renut runs, the F2 VSync box is that switch and it applies at once. An old `futari.ini` without the line keeps Renut off; the next Save adds it.
- **Renut falls back to the default renderer when its shaders are not there.** With `futari_renderer = renut` but no `vulkan_spirv.bin`, or one made by another build, the game used to start on Renut and draw black. It now starts on the default renderer for that session, the log says why in one `[warning]` line, and F2 says so above the shader button. Prepare or redo the shaders there and start again.
- **Renut's boot names its steps in the log** (`Host device:` lines, then `Renderer: host device ready` and `Renderer: presenting`), so a GPU driver crash during start-up ([#8](https://github.com/springah/futari_recomp/issues/8)) names the step it died in.
- **The default renderer logs the game's own frame rate:** a `[vblank]` line every 10 seconds with the vblanks delivered, bursts, stalls and the game's Swaps as fps, so a log shows whether the game itself runs at 60. Its vblank timer now runs above the game's threads and sleeps to the exact vblank (`vsync_sleep_exact`; `false` restores the old worker).
- **The F4 overlay uses the F2 screen's font**, at the size F2 picks for the window, instead of the SDK's 10 px font.
- **macOS: the bundle is `Futari-1.5.6.app`**, and Finder's Get Info shows the version and the exact build.

Known limits are those of v1.5.5 (Renut is Vulkan-only and experimental on macOS). Please attach the newest `logs\futari_*.log` with any report.

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.6/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.6.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.5:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder, start it, and if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it; until then the game runs on the default renderer and says so). Same as every bundle here, this contains no game data and is for owners of the game only.

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.6.zip` | `beb76772ef49717b9da2c3a32f26867a63a9dfb055145089e9fa1316135130f8` |
| `Mushihimesama-Futari-macOS-v1.5.6.zip` | `434a35438d17da20d15021416810177ea4cfe0fe9f27cb7cb8d4493b89d84f3b` |
