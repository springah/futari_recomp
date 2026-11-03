# Changelog

Versions are per-repo; each release is a tag with a GitHub Release, and from
v1.5.3 on the per-OS zips are attached to it (earlier zips went to a private
download share). Ports can sit on different versions — see [PORTS.md](PORTS.md).

## Unreleased

Groundwork for the native renderer (issue #8, `docs/NOTES_renut_port_scoping.md`),
leg 1 of 6: the SDK additions the Renut renderer compiles against, ported from
cerroverb/futari-linux's SDK fork onto the vendored v0.9.3.1. No renderer code
yet; the xenos path is unchanged in behaviour.

- **Frame latency probe** (`rex/frame_latency.h`): input read, guest swap,
  present, GPU done and display vsync timestamps, a one-line report and an
  event trace. `VdSwap` now feeds its swap counter.
- **`GuestSwapCount` / `vblank_per_swap`** in the kernel video exports: a
  swap counter any renderer can follow, and a deterministic one-vblank-per-swap
  mode (`vblank_per_swap`, `vblank_per_swap_timeout_ms`, `vblank_per_swap_min_us`)
  for lockstep comparisons between renderers. Off by default.
- **Late input read** (`input_frame_read_delay_us`, default 0, and
  `rex::input::SetFrameReadDelayFunction`): the first controller read after each
  swap can be delayed, so a game that polls right after its swap samples closer
  to the frame that shows it. No effect unless set.
- `rex::memory::MarkStoreCardsAt` no-op, for Renut's draw path.

Two small follow-ups to the v1.5.4 vsync fix. SDK change in the vendored tree
for the log line, a save-path change in `main.cpp`, no codegen regen needed.

- **A saved VSync-off setting no longer sticks when `vsync = true` is edited
  back in.** The F4 VSync checkbox sets both `vsync` and `present_vsync`, and
  "Save to config" wrote the second into the managed block of `futari.ini` as
  well. The managed block is read last, so after saving with VSync off and
  later hand-editing `vsync = true` under `[Display]`, the game paced at 60 but
  the window still presented immediately and tore. `present_vsync` is now
  written only when it deliberately differs from `vsync` (the variable refresh
  rate case). An existing `present_vsync = false` line in the managed block
  keeps working; delete it, or save once from F4, to follow `vsync` again.
- **The log shows the Direct3D 12 present path.** One line when the swap chain
  is created (size, buffer count, whether tearing is supported) and one whenever
  the present path changes (sync interval and present flags), so a tearing or
  frame rate report can be read off the log. Vulkan already logged its
  presentation mode.

## v1.5.4 — 2026-11-02 (Windows, macOS)

One renderer fix, prompted by the first bug report from a player, and one platform
decision. SDK change in the vendored tree, no game-logic changes, and no codegen
regen needed. The fix was boot-checked on all three platforms: Direct3D 12 now
presents on the vertical blank, the Steam Deck swapchain moved from immediate to
mailbox, and macOS (MoltenVK) from immediate to FIFO.

- **Linux and Steam Deck are handed over to [futari-linux](https://github.com/cerroverb/futari-linux).** cerroverb
  maintains a native-renderer port of the same game (reNut's Direct3D-level
  renderer on Vulkan, from ChanseyIsTheBest's futari-nx) and measured it against
  this port's v1.5.3 Deck bundle: 23 % of one core versus 324 %, 5.4 W versus
  13.8 W, both at 60 fps. The emulated Xenos GPU path was never the right thing
  for a handheld, so from this version the releases carry Windows and macOS
  bundles only and the docs point Linux and Steam Deck players at futari-linux
  (#6). The `linux-amd64` preset stays in the tree and still builds; it is just
  no longer a supported target.

- **VSync now actually syncs the window to the monitor.** `vsync = true` paced
  the game at 60 but never reached the host swap chain: the SDK presenter always
  presented immediately with tearing allowed (sync interval 0 plus
  `DXGI_PRESENT_ALLOW_TEARING`, chosen upstream for variable refresh rate
  displays). On a fixed 60Hz monitor that showed as a tear line near the top of
  the screen, and while any overlay was drawn (the title-screen exit hint, a
  toast) the UI thread re-presented every vblank on top of the guest frames, so
  external counters read about 120 FPS (#5). A new SDK cvar `present_vsync`
  (default on) makes Direct3D 12 present on the vertical blank without the
  tearing flag, and Vulkan skip the immediate and FIFO-relaxed present modes;
  `DXGI_PRESENT_RESTART` keeps late guest frames from queueing behind a UI
  re-present. The `vsync` line in `futari.ini` and the F4 VSync checkbox set
  both the guest pacing and the host present. Players on variable refresh rate
  displays can keep 60 fps pacing but restore immediate presentation with
  `present_vsync = false` in the F4 cvar list (it persists in the managed block
  of `futari.ini`). SDK change, in-tree; upstream note in
  [docs/upstream/host-vsync.md](docs/upstream/host-vsync.md).

## v1.5.3 — 2026-10-29

The port's first outside contribution, plus a much shorter road to a build. No
SDK, renderer or game-logic changes, and no codegen regen needed.

- **Exit from the title screen with B or Esc.** The console title screen only
  accepts START/A, so on PC the only way out was the window's close button or a
  window-manager shortcut — awkward in fullscreen and with a controller. Like
  CAVE's own PC ports, the idle title now shows "Press B or Esc to exit." in the
  bottom-right corner and quits on **B** from any connected pad or on **Escape**
  on the keyboard. Escape is rebindable as `bind_title_exit` in the F4 settings
  overlay, and the hint follows the bind live. Exit takes the same shutdown path
  as the title-bar close button. B only fires on a fresh press (a B held over
  from backing out of the menus is ignored), and while an interactive SDK
  overlay is open (settings, console, debug, achievements) B and Esc belong to
  the overlay and the hint is hidden. Everything else on the title, in the menus
  and in gameplay is unchanged. Implemented by overriding the title's idle-state
  function (`sub_821CBFF0`) from `main.cpp`: generated guest functions are weak,
  so a strong definition takes over the dispatch entry without touching the
  recompiler output. Contributed by itemzer0 (#3); tested on Linux by the
  author, build-verified on macOS.
- **Bundles are published on GitHub Releases.** Until now the per-OS zips went
  only to a private download share and everyone else was told to build from
  source. The zips are now attached to each release, with the condition they
  always carried: they contain the game's own program code, recompiled, so they
  are for people who own the game. No game *data* is in them, as before. See the
  README's Legal section and PORTS.md.
- **One-shot Windows build: `build.ps1`.** Installs the missing toolchain via
  winget (Git, Python, CMake, Ninja, LLVM, VS 2022 Build Tools), enters the VS
  developer environment, checks out the submodules and fixes the libmspack
  symlink gotcha, builds the SDK CLI, extracts your ISO or console dump, runs
  `regen.sh`, configures and builds. `build.bat` wraps it for a double-click.
- **`win-amd64-*` presets now use clang-cl.** They used to name `clang++`, which
  does not build this project, so Windows was the one platform with no working
  preset. `cmake --preset win-amd64-release` replaces the hand-typed configure
  line; the build lands in `out/build/win-amd64-release/`.
- **Script and doc fixes from a readability review.** `build.ps1` resolves a
  relative `-GameSource` before changing directory, refuses a ZIP download (no
  `.git`, so no submodules) with a hint, and re-checks the CMake version after
  entering the VS developer shell. `extract_assets.bat` runs each Python
  candidate instead of trusting `where` (the Microsoft Store stub exits 9009),
  survives a dragged path containing `)`, and no longer claims `futari.exe` is
  ready when there is nothing built. `build_module_hints.sh` no longer overwrites
  the committed hint files with unfiltered seeds mid-run. `build_app.sh` fails
  on a failed codesign, regenerates the third-party notices every time instead
  of only when missing, derives the build and SDK dirs from the checkout it lives
  in, and uses the repo's own icon. `regen.sh` finds the SDK CLI for every
  platform and no longer falls back to a sibling SDK checkout. The LGPL written
  offer in `THIRD-PARTY-NOTICES.txt` now points at this repository's vendored
  SDK rather than upstream, which lacks the fixes the bundles need. Removed the
  unused `futari_manifest.toml` (`regen.sh` writes its own manifests) and the
  private family-and-friends `RUN.ff.txt`; added `dist/dlc-README.txt` (ships
  as `dlc/README.txt`), which `RUN.txt` had referred to without it existing;
  README gains the macOS/Linux
  build steps and a truthful release line.
- **`build.ps1` finds Git's `bash.exe` whichever `git.exe` PATH names.** Its
  first real run stopped at the toolchain check on a machine whose PATH put
  `Git\mingw64\bin\git.exe` ahead of `Git\cmd\git.exe`; it now walks up the
  parents until bash appears.

## v1.5.2 — 2026-08-11 (Windows, macOS, Steam Deck)

Clears the README's last known issue. Bundles for all three platforms were
restaged from this tag the same day the repository went public; the Steam Deck
build was boot-verified on the device.

- **The shell's only codegen fatal is gone.** The README's one known issue —
  the split block at `0x82081778` whose branch back into `sub_82081658`'s loop
  could not be expressed — turned out to rest on a pointer-scan false
  positive: the sole "pointer" to that address in the image is a
  `lwz r16,0x1778(r8)` instruction whose encoding aliases the address. The
  address is now declared a chunk of its parent, and the vendored SDK's
  half-built chunk mechanism was completed to honour it: a chunk claims its
  range (blocking the PDATA, exception-handler, gapfill, and pointer-scan
  paths from re-splitting it) without emitting a function, a dispatch entry,
  or the fatal. The generated shell changes by exactly one removed function;
  all 8 modules and the shell boot unchanged.

## v1.5.1 — 2026-08-11 (Windows, macOS, Steam Deck)

Extraction tooling and the macOS tag fix, cut mid-test-week for further testing.
No renderer or game-logic changes. Bundles were restaged from this tag on all
three platforms the same day; the Windows and Deck bundles now carry the
extraction tooling so the dump-your-own-copy route can be tested from them.

- **The extractor now reads console disc installs directly — no god2iso, no
  Velocity.** Dumping your own copy is now: install the disc on the console,
  copy the game's `Content\...\435A07D7` folder to your PC, and drag it onto
  `extract_assets.bat`. `scripts/extract_iso.py` reads the Games on Demand
  (SVOD) container natively, verifies the copy against the container's own
  SHA-1 hash tree first (a corrupt or truncated copy is refused up front, with
  the exact file named), and installs any marketplace DLC packages it finds
  into `dlc/` after checking each one really is DLC for this game. Validated
  against a real console install: all 9 executables match the checksums in
  DUMPING.md, and the extracted tree is byte-identical to a disc rip's.
- **ISO extraction produced garbage for every file; fixed.** XDVDFS sector
  numbers are relative to the partition, but the script based reads at the
  volume descriptor's own offset — 32 sectors (0x10000 bytes) too late, for
  every file, since the first release. It cannot ever have produced a correct
  extraction; working dumps historically came from the extract-xiso tooling
  this repo once vendored. An XGD2 rip (partition at 0xFD90000) now extracts
  correctly, byte-identical to the console-install route.
- **macOS builds from the tag work again.** The vendored SDK's version-pin file
  was named `VERSION`, the SDK root is on the include path, and APFS is
  case-insensitive — so libc++'s own `#include <version>` resolved to our data
  file and every macOS translation unit failed to compile. The file is now
  `REXGLUE_VERSION`, which no standard header will ever collide with. The
  shipped v1.5.0 macOS bundle already carried this fix (applied post-tag);
  v1.5.1 is the first tag that builds clean on macOS as checked out.

## v1.5.0 — 2026-08-09 (family & friends test round before going public)

- **Bundles gain `gamecontrollerdb.txt`** — the community
  [SDL_GameControllerDB](https://github.com/mdqinc/SDL_GameControllerDB)
  (Zlib licence, snapshot `629ccc7`, 2,254 mappings, tracked at
  `dist/gamecontrollerdb.txt`). The SDL input driver loads it from beside the
  exe, adding recognition for gamepads SDL3 doesn't know natively, and the
  startup warning about the file being absent goes away. Listed in
  THIRD-PARTY-NOTICES.txt.

- **The SDK is vendored.** `thirdparty/rexglue-sdk` is no longer a submodule
  pointing at a private fork — the tree itself is in the repo, taken verbatim
  from the fork at v0.9.3.1 (see `thirdparty/rexglue-sdk/VENDORED.md` for the
  documented deviations). A public clone now resolves every dependency.
- **One ini again.** v1.4.2 split the overlay's save target into
  `futari_cvars.ini` because the SDK's save was a truncating flat rewrite. The
  save now goes through a merge writer: F4's "Save to config" updates the
  friendly lines in `futari.ini` in place (comments and `[Storage]` intact) and
  keeps any other overlay-modified cvars in a marked block at the top of the
  file. A leftover `futari_cvars.ini` is loaded once at startup and parked as
  `.bak`. The write is temp-file + rename, so a crash mid-save cannot truncate
  the file.

## v1.4.2 — 2026-08-08 (source release; no new bundles yet)

A pre-publication pass over the repository. No renderer or game-logic changes, so
the v1.4.1 bundles remain current for players until these fixes are packaged.

**Two bugs worth a rebuild**
- **The in-game settings overlay no longer destroys `futari.ini`.** The SDK
  defaults its own cvar file to `<exe>/futari.ini` — the same file the game reads
  and the player edits — and F4's "Save to config" rewrote it as a flat cvar list
  with a truncating write. `[Storage]` went with it, and because `user_data_path`
  is not a cvar, saves silently relocated to `Documents\futari` on the next
  launch. The SDK now gets its own `futari_cvars.ini`.
- **`extract_iso.py` no longer reports a truncated extraction as success.** It
  compared nothing: the write loop broke on a short read, then logged the size
  from the directory table rather than the bytes written, and exited 0. A split or
  aborted rip produced empty files, a green log, and a confusing codegen failure
  hours later. It now reports each truncated file and exits non-zero, and rejects
  directory-table entries whose names would write outside the output directory.
- Quoted paths in `futari.ini` now work (`user_data_path = "D:\Futari Saves"`),
  matching the SDK's own parser for that file.

**Licensing and legal accuracy**
- `THIRD-PARTY-NOTICES.txt` is new, generated by `scripts/gen_third_party_notices.sh`
  and shipped in bundles. The runtime statically links FFmpeg and libmspack, both
  LGPL-2.1, so a written offer of source was required and absent; ~20 permissive
  components had no notice either. Bundles carried two lines of legal text before
  this. macOS bundles now also ship MoltenVK's Apache-2.0 licence, and the build
  fails rather than omitting it.
- **"No game content" is now stated accurately.** The repository does contain two
  game-derived files — `screenshots/title.png` and `src/futari.ico`, the latter
  compiled into every Windows binary — and the bundles necessarily contain the
  game's own program code, recompiled. Both are now said plainly in the README,
  `NOTICE.md`, `dist/RUN.txt` and `PORTS.md` rule 2. `NOTICE.md` also carves the
  icon out of the BSD-3 grant, which previously covered all of `src/`.

**Repository hygiene**
- Personal paths and machine aliases removed from `PORTS.md` throughout the
  history, and an internal workspace log removed from the trees of the v1.3.2–v1.4.1
  tags. The two vendored `extract-xiso` binaries are gone from every tag.

## v1.4.1 — 2026-08-07 (Windows, macOS, Steam Deck)

All three platforms rebuilt on SDK v0.9.3.1 (up from v0.9.2), plus packaging fixes.
The SDK bump carries a cvar fix and a macOS link fix, but an A/B of the two runtimes
showed no behaviour change for futari — the value here is in the packaging.

**Windows**
- The bundle no longer ships development leftovers: ~64 MB of superseded binary
  backups, an instrumented reference exe, the Tracy profiler DLL, 212 of the
  developer's own run logs, and a machine-local shortcut. 927.6 MB → 908.5 MB.
- Packed under a proper top-level folder with standard forward-slash entries
  (v1.4.0 had non-spec backslash entries).
- `RUN.txt` no longer shows `???` where dashes belong.

**Steam Deck / Linux** — the v1.4.0 bundle had three real problems, all fixed:
- **The launcher only worked from one directory.** It hardcoded the absolute path
  the build happened to be tested from, so extracting the zip anywhere else could
  not start the game. It now resolves its own location. `futari.ini` no longer
  pins an absolute `game_data_root` either.
- **The shipped runtime was the wrong build** — `librexruntime.so` identified as
  `rexglue-v0.8.3` while the game binary was built against v0.9.0-dev and the GPU
  plugin is v0.9-only. Every component now comes from one v0.9.3.1 build.
- **The zip lost Unix file modes and used backslash separators**, so the launcher
  arrived non-executable and paths needed repairing by hand. Packed with `zip -X`
  now, which preserves both.

**macOS** — this one turned out to be broken *before* v1.4.1, and the rebuild is
what surfaced it:
- **The GPU plugin was never in the bundle.** SDK v0.9 split the renderer out of
  the runtime, but `mac-build/build_app.sh` predates that and never staged it, so
  the app started and died with `Failed to load GPU plugin 'xenos'`. **v1.4.0
  shipped this way** — macOS has had no working renderer since the v0.9 migration.
  The plugin is staged now, deliberately renamed `.dylib` → `.so`, because the
  SDK's `PluginFileName()` asks for `.so` on every non-Windows platform including
  macOS. That rename is a workaround; the fix belongs in the SDK.
- **The bundle shipped a mismatched runtime.** The script preferred the
  `REXSDK_DIR` environment variable over the pinned submodule, so a v0.8.5.1
  `librexruntime.dylib` landed next to a v0.9.3.1 executable and dyld refused to
  launch it. The submodule now wins, and the build fails outright if the
  executable and runtime report different SDK versions.

`libTracyClient.dylib` stays — the app and runtime link against it.

Verified on an M3 Max: GPU plugin loads, MoltenVK initialises, all 8 modules
register, DLC installs, no errors in the log.

## v1.4.0 — 2026-08-05 (Windows, macOS, Steam Deck)

First release on the SDK v0.9 line: upstream v0.9.0 merge with XMA audio fixes,
manual switch-table support, a DLL indirect-call fix, SDL3 windowing, and the
split of the GPU backend into the `rexgpu-xenos` plugin.

## Earlier

See the `v1.0.x`–`v1.3.x` tags. v1.3.x tracked the SDK v0.8 line; the recompiler
jump-table bug that crashed game-mode entry was fixed during it.
