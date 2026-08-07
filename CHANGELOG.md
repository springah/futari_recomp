# Changelog

Versions are per-repo; each release is a tag, with per-OS zips on the download
share. Ports can sit on different versions — see [PORTS.md](PORTS.md).

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
