**Windows and macOS.** The Renut renderer now draws the game at the window's own size, and Picture Setting gains a Sharp smoothing mode. The Renut work is [Decidetto](https://github.com/Decidetto)'s ([#10](https://github.com/springah/futari_recomp/pull/10)). Players on the default renderer see one change: `[Display] resolution` is now a size in physical pixels.

**What changed since v1.5.11**

- **Renut draws the game at the window's size.** The game's frame used to be 1280x720, 1920x1080 or 1920x960, scaled to the window afterwards. It is now the window itself, at any aspect ratio, portrait included, and it follows a resize, F11, minimise and restore without a restart. `renut_resolution = 720p` or `1080p` at the top of `futari.ini` goes back to a fixed frame.
- **Layouts away from 16:9, and a zoom multiplier.** Under the window-sized frame the wallpaper and title picture cover the frame and menus stay readable in a narrow window. Screen > Layout Setting's zoom shows its multiplier (`3.00x`) instead of a raw number, and the largest whole multiplier that fits is always reachable (6x at 3840x2160). Old saves keep their zoom.
- **Sharp smoothing.** Picture Setting > Smoothing reads Smooth / Sharp / Off. Sharp keeps the art's pixels crisp at non-integer zooms and is exactly Off at whole ones. Only Renut draws it; the default renderer draws it as Smooth. The choice is kept in `futari.ini` (`futari_sharp_smoothing`), not in the game save, so a save stays safe to open in v1.5.11 and older, futari-linux and futari-nx. It is one setting for all eight Picture presets and is off until you pick it.
- **`[Display] resolution` is physical pixels, on both renderers.** It used to be multiplied by the Windows display scaling: at 150 % a `resolution = 1920x1080` line opened a 2880x1620 window, and it now opens exactly 1920x1080. At 100 % nothing changes. If your window is smaller after updating, raise the line or pick a size on F2.
- **A window you resized keeps its size.** Resize the window and save (F4 "Save to config", or F2) and `[Display] resolution` gets the window's size. A size picked on F2 since the start still wins.
- **Fix: Options > Screen no longer crashes Renut** (the page drew through a call Renut did not handle).
- **macOS, Renut: `--sdl_high_pixel_density=true` works.** With that option the swapchain was rebuilt on every frame, in v1.5.11 and earlier too. It is now made once and the game draws at the display's full pixels (a 3600x2072 frame in an 1800x1036-point window on a 14-inch MacBook Pro), at a GPU cost. The option stays off by default.
- The F4 overlay no longer lists the window and frame size settings (`[Display]` and F2 own them); the default renderer's `resolution_scale` stays listed under the advanced settings.

How this release was checked: **Windows** — the staging script's checks (no game data in the zip, both renderers boot, the shader translator is built in). On the staged build: a scripted Renut run through Picture Setting (Smooth, Sharp and back, the `futari.ini` line written each time, 60 fps); on the default renderer, `resolution_scale` listed in F4's advanced settings, and a window set to 2100x1200, saved with F4 and reopened at 2100x1200 on the next start. **macOS** (M3 Max) — signature verified after the zip round-trip, the Renut fallback line, a 25 s boot on the default renderer at 60 fps with no errors, a shader package made with the shipped `make_shaders.py`, then Renut at 1800x1036 and, with `--sdl_high_pixel_density=true`, at 3600x2072 with the swapchain made once and Sharp switched on and off.

Not checked: resizing by dragging with the mouse on the release builds (the window was resized by program); `[Display] resolution` on a display scaled above 100 %; fullscreen and live resize on macOS with high pixel density on.

Known limits are those of v1.5.11 (Renut is Vulkan-only and experimental on macOS), plus: Sharp is Renut's, and if the F4 overlay was opened before the window was made smaller, its "Save to config" button can sit below the window's edge until the window is enlarged again (seen once at 1600x900).

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.12/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.12.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.11:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it).

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.12.zip` | `799d67e17a202b73124e147b94517e87ff3bf9819b26b975ee3c8a1e44d6a43c` |
| `Mushihimesama-Futari-macOS-v1.5.12.zip` | `6390568c4c3e7447dfc8c19321c1e067b84073d495ea0c63af836ebd3f4349dc` |
