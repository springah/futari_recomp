**Windows and macOS.** A maintenance release on top of v1.5.7, cut the same day: the macOS bundle gains the two-pass shader script, the build-from-source steps for macOS are corrected by a fresh clone, and two vendored-SDK defects that only hit a from-source macOS build are fixed. Nothing changed in how the game plays, and the Windows exe behaves as in v1.5.7.

**What changed since v1.5.7**

- **macOS: `make_shaders.py` ships in the zip and works on the Mac.** Run it next to `Futari-1.5.8.app` (`python3 make_shaders.py`) and it finds the app, the compiler and the tool files inside it, then writes the two-pass `vulkan_spirv.bin` into `Contents/MacOS/`. Until now the Mac had only the F2 button's one-pass translation (which still works). The bundle carries the early-alpha pass's two scripts under `Contents/Resources/renut_tools`, and the GPU plugin under its real name `librexgpu-xenos.dylib` (earlier bundles carried it renamed to `.so`).
- **macOS present_vsync census: no change needed.** Four 100 s runs at the title, presenter vsync on and off alternately, all read 60.00 fps on an M-series MacBook Pro. The Windows default change in v1.5.7 stays Windows-only; macOS keeps following the `vsync` line.
- **Build from source on macOS, corrected by a fresh clone:** the README names the Homebrew compiler the SDK needs (its preset picks Apple's, which it refuses), builds the SDK's binary directory instead of a preset that does not exist, and lists the shader dump (`scripts/renut_shader_dump.sh`, which needs `pip install xxhash`) and `scripts/build_dxc_mac.sh`, both of which the configure step refuses to run without. Two vendored-SDK defects that killed the dev-tree `futari` (not the .app) at launch are fixed: the runtime dylibs are now staged beside the executable on Apple, and the GPU plugin loader asks for `.dylib` instead of `.so` (`thirdparty/rexglue-sdk/VENDORED.md`).
- **Packaging:** the Windows stage script removes what its own boot checks wrote (Renut's `pipeline_list.bin`) and re-runs the shipping rules on the zip's contents. The published v1.5.7 zip was already staged with this fix.

Known limits are those of v1.5.7 (Renut is Vulkan-only and experimental on macOS). Please attach the newest `logs\futari_*.log` with any report.

Full details in [CHANGELOG.md](https://github.com/springah/futari_recomp/blob/v1.5.8/CHANGELOG.md).

**Install:** unzip anywhere and double-click `futari.exe` (macOS: `Futari-1.5.8.app`; `RUN.txt` inside has the macOS steps). **Upgrading from v1.5.7:** copy your `assets\`, `dlc\` and `futari.ini` into the new folder and start it; if you use Renut press F2 and "Redo the shaders" once (the shader file names the build that made it; until then the game runs on the default renderer and says so). On macOS, do not drop a v1.5.8 executable into an older app bundle or the reverse: the plugin's file name changed. Same as every bundle here, this contains no game data and is for owners of the game only.

| File | SHA-256 |
|---|---|
| `Mushihimesama-Futari-PC-v1.5.8.zip` | `18d75e38d9dd836c3b7b40837ee6d8a5a95cd78c4d8aff95c327a5c21687ff94` |
| `Mushihimesama-Futari-macOS-v1.5.8.zip` | `3f0fd0961f35d3ec17f9b763884291669d744f2d52f341b5c94be03e7d352f08` |
