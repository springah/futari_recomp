# The launcher: setup and settings inside futari.exe

2026-11-03, branch `launcher` (cut from `renderer/renut` after v1.5.5-rc2). The user's brief: a launcher in the
style of Witch Time's, so that a tester unzips, double-clicks, points the game at their own dump and plays, with no
Python, no batch file and no helper executable (Windows Defender quarantined rc1's `XenosRecomp.exe`; nothing ships
beside `futari.exe` that an antivirus can judge on its own).

## Design

The SDK already had the hook: `rex::ReXApp::OnFinalizePaths(defaults, resume)` runs after the window and the ImGui
drawer exist and before the game's executable is loaded, and returning `std::nullopt` keeps the event loop pumping
(overlay-only frames) until `resume(paths)` is called. So the launcher is an `ImGuiDialog` on the SDK's overlay
stack, drawn by whichever renderer is active (the xenos presenter, or Renut's overlay drawer, which the app starts
in the same hook), in the game's own window. No second window, no second ImGui backend.

`src/launcher/`:

| file | what |
|---|---|
| `launcher_dialog.{h,cpp}` | the screens: Setup (three picker buttons + a typed path), Working (log, bar, Cancel), Done (Play / Settings / Quit), Settings. One dialog, four pages; `Mode::kSetup` from the hook, `Mode::kSettings` from F2 in the game. |
| `game_source.{h,cpp}` | `scripts/extract_iso.py` in C++: `ClassifySource` (XContent header: title 435A07D7, type 0x7000 = disc install, 0x2 = DLC; a loose folder = `default.xex` + `media/`; a file that is no package = presumably an ISO), the XDVDFS reader (volume descriptor at the four known offsets, then a 2 GB scan; `fs_base = descriptor - 0x10000`), the GoD/SVOD container as a flat disc (level-1 / level-0 SHA-1 hash tree verified first; `sha1.h`), the DLC copy, the loose-folder copy with the copy-into-itself guard. Same refusals and messages as the script, exit statuses as `ExtractResult::Status`. |
| `shader_pipeline.{h,cpp}` | `make_shaders.py` without Python: this exe as a child twice (`--futari_renut_dump_shaders`, then `--futari_renut_translate` with `XENOS_RECOMP_SPIRV_PACKAGE` in the environment), the BVKS package check, the copy beside the exe. One translation pass. |
| `file_picker.{h,cpp}`, `file_picker_mac.mm` | the OS's open-file / open-folder dialog: `IFileOpenDialog` on its own thread (Windows), `NSOpenPanel` (macOS, unbuilt). The SDK's SDL3 is compiled without its dialog subsystem and lives inside the runtime library, so the launcher goes to the OS. |

`src/main.cpp`: `FinalizePathsThroughLauncher` (the decision: no `default.xex` under the game data root, or
`--futari_setup`, or `--futari_setup_source`), `MakeLauncherEnv`, the F2 bind, three init-only cvars. Play hands
the paths to the SDK through `CallInUIThreadDeferred(resume)` so the runtime is constructed outside the ImGui frame
that pressed the button.

## Decisions

- **One translation pass, no early-alpha rewrite.** `make_shaders.py` runs Renut's `early_alpha.py` between two
  translator passes; it is 1,600 lines of Python with numpy, and its output is a performance rewrite (an early
  alpha-test discard in a few pixel shaders, self-checked for exactness), not a correctness one. The launcher's
  package is therefore not byte-identical to the build's (73,040 vs 78,056 bytes for Ver 1.5; sha256
  `757af820…` vs `36869416…`) but draws the same picture. The script route keeps the optimisation for whoever wants
  it; the gap is noted in RUN.txt.
- **Child processes, not in-process.** The dump needs the runtime with the XEX loaded (it loads and unloads the
  eight cores), and the translator loads `dxcompiler.dll` and ends by `TerminateProcess` (the normal exit path
  fast-fails after DXC's thread-locals). Neither belongs inside the process that is about to run the game. The
  children are the same `futari.exe` the player already ran.
- **`--futari_renderer=default` on the children**, so an ini that asks for Renut does not start it without a
  package.
- **The renderer line is written by the launcher itself** (`WriteRendererLine`): `futari_renderer` is an init-only
  cvar, so the existing F4 merge (`SaveFutariIni`, modified cvars into the managed block) cannot carry it; the
  launcher removes any top-level `futari_renderer = …` line and inserts `futari_renderer = renut` as the first line
  of the file, where RUN.txt tells players to put it by hand. Display settings go through `SaveFutariIni`.
- **Settings apply at the next start** for the renderer and the window size (the SDK's window exists already);
  fullscreen and vsync are cvars the running game reads, but the screen says "next start" for all of them to keep
  one rule.
- **Font**: the SDK's ProggyTiny at 10 px, scaled 2x (1.5x below 1000 px tall) with `SetWindowFontScale`. Chunky,
  readable. A real font is a follow-up.

## Verified (2026-11-03, Windows, release-shader tree)

All scripted: `futari.exe --game_data_root=<empty folder>/assets --futari_setup_source=<source>`, judged by the exit
(124 = still running when `timeout` killed it), the log's `Launcher:` lines, the file count in `assets/` (54 for
Ver 1.5) and the package:

| source | result |
|---|---|
| loose game folder (with `dlc/`) | 54 files, 2 DLC installed, 42 shaders (1 s), XEX loaded, both `InstallContent -> 0x00000000`, alive at 100 s |
| the disc ISO (`D:` RF rip) | 54 files, 42 shaders, alive at 100 s; `assets/` identical to the loose folder's |
| a synthetic console install (`scripts/extract_iso.py`'s format, made from the ISO: 45 data files, 1,848,832 blocks) | hash tree verified (~25 s), 54 files identical to the ISO route, 42 shaders, XEX loaded. One of three runs ended in an access violation ~4 s into the game (exit 139); the two re-runs and a plain run of the game on those assets stayed alive. Not understood; see below. |
| the same with one data byte flipped (Data0003 block 199) | rejected: "data block 199 fails its hash", no `assets/`, the screen stays on the error |
| a folder holding only the two DLC packages | DLC copied into `dlc/`, "holds no game" refusal, no `assets/` |
| a text file named `.iso` | "not an Xbox 360 disc image" refusal |

The Python extractor on the synthetic console install verifies all 45 files and extracts the same 54 files, and
rejects the flipped copy with the same block number, so the C++ reader and the script agree.

## Not done / to watch

- **The one access violation** (above). Nothing in the log; the crash came after `SetInterruptCallback`, in the
  game. If it shows again on a launcher route and never on a plain start, suspect the long overlay-only
  presentation before the runtime (the hash check held the screen for 25 s) or the dialog's teardown order
  (`FinishPlay` -> deferred `resume` -> `Close`).
- **Human eyes**: the screens were never clicked by a person here (the routes are driven by `--futari_setup_source`);
  the file dialogs, Cancel, the Settings page's Save, and F2 over the running game are untested by hand. Screenshots
  in `out/launcher_shots/`.
- **macOS**: `file_picker_mac.mm` is written, not built; the Cocoa link line is in `CMakeLists.txt`. The setup
  screen has no shader step there (no translator).
- Gamepad navigation (ImGui nav flags), a proper font, an update check against GitHub releases, a package version
  check (a stale `vulkan_spirv.bin` after an update shows only as shader misses in the log).

## macOS, 2026-11-04

Branch `leg/macos` (leg 6; full write-up in `docs/NOTES_renut_leg6_macos.md`). The launcher builds and runs on
Apple Silicon:

- `file_picker_mac.mm` is built: NSOpenPanel opened non-modally on the main thread (`beginWithCompletionHandler`),
  polled by the dialog each frame like the Windows picker's thread, so the ImGui frame never blocks. Not yet clicked
  by a person.
- **The shader step exists on macOS.** The translator is in the macOS exe too; the DirectX Shader Compiler is a
  `libdxcompiler.dylib` built from source (`scripts/build_dxc_mac.sh`, v1.8.2407; no macOS binary exists to
  download), dlopened by the translate child. Build tree: `renut_tools/{libdxcompiler.dylib,shader_common.h}`.
  Bundle: `Contents/lib/libdxcompiler.dylib` and `Contents/Resources/renut_tools/shader_common.h`
  (Contents/MacOS takes Mach-O only). So Settings keeps the renderer choice on macOS; no "Windows-only" text needed.
- Verified headless (`--game_data_root=<empty>/assets --futari_setup_source=<loose folder>`), both from the build
  tree and from a SKIP_ASSETS `.app`: 54 files, 2 DLC, "Shaders ready: 42 translated into vulkan_spirv.bin
  (71 KB, 1 s)", the game boots on the default renderer; then `--futari_renderer=renut` draws the title from that
  package at 60 fps. The package is byte-identical to the Windows launcher's (sha256 `757af820…`).
- Open: the picker, Cancel, Settings and F2 by hand; whether ImGui input reaches the dialog when Renut draws it
  (Renut's own overlay input is Windows-only); the exe writes `vulkan_spirv.bin` (like logs/ and the ini) inside
  Contents/MacOS, which breaks the bundle's code seal after the first run.
