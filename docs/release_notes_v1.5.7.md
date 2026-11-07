**Windows and macOS.** A small release on top of v1.5.6: one default changed on Windows, the Windows build compiled the way the SDK intends, and the build-from-source script proven on a fresh clone. Nothing changed in how the game plays.

**What changed since v1.5.6**

- **Windows: the game holds 60 fps out of the box.** The window no longer waits for the display's refresh by default (`present_vsync = false`, now its own line under `[Display]` in `futari.ini`). With the wait on, the default renderer read 58 to 59 of the game's 60 frames a second on a 59.94 Hz panel, at the title and in the attract demo alike; with it off it holds 60.00. The game still paces itself with its own 60 Hz clock (`vsync`, which stays on). Windowed, the desktop composes the picture, so nothing tears; fullscreen on a fixed-refresh panel may, and turning the F2 VSync box back on is the cure. That box is now this switch on the default renderer (it used to also turn off the game's own pacing, which ran the game as fast as the machine goes). An older `futari.ini` without the line gets the new default; one that already says `present_vsync = false` in its managed block keeps working. macOS keeps following the `vsync` line as before (not measured there).
- **The Windows exe is compiled the way the SDK intends.** clang-cl had been dropping three of the vendored SDK's flags: strict floating point (`-ffp-model=strict`), `-O3`, and the warning set, which it read as MSVC's `/Wall` (every warning clang has: one build log carried about 170,000 `-Wc++98-compat` lines). They go through in clang-cl's own spellings now (`/fp:strict`, `/clang:-O3`, `/clang:-Wall`). Measured on full rebuilds: the frame rate is the same within noise, both renderers boot and run clean.
- **`build.ps1` proven end to end on a fresh clone** (clone, compiler fetch, SDK, ISO extraction, codegen, shader dump, configure, build, boot). It now refuses to start with under 8 GB free on the repository's drive, and names the harmless `'vswhere.exe' is not recognized` line Visual Studio's shell prints on some machines.
- **A first automated test:** the `futari.ini` merge that F2 and F4 saves write back is checked on every push.
- README and the GitHub page tidied; the log line for Renut falling back to the default renderer starts with `[renut-fallback]`.

Known limits are those of v1.5.5 (Renut is Vulkan-only and experimental on macOS). Please attach the newest `logs\futari_*.log` with any report.

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.7/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.7.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.6:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it; until then the game runs on the default renderer and says so). Same as every bundle here, this contains no game data and is for owners of the game only.

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.7.zip` | `64ca4a39ad361585c23918f8773f3493bf63067508ead36801a146cbeee49dd7` |
| `Mushihimesama-Futari-macOS-v1.5.7.zip` | `TBD` |
