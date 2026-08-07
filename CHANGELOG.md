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

**macOS** — rebuilt; no packaging problems found. `libTracyClient.dylib` stays, as
the app and runtime link against it.

## v1.4.0 — 2026-08-05 (Windows, macOS, Steam Deck)

First release on the SDK v0.9 line: upstream v0.9.0 merge with XMA audio fixes,
manual switch-table support, a DLL indirect-call fix, SDL3 windowing, and the
split of the GPU backend into the `rexgpu-xenos` plugin.

## Earlier

See the `v1.0.x`–`v1.3.x` tags. v1.3.x tracked the SDK v0.8 line; the recompiler
jump-table bug that crashed game-mode entry was fixed during it.
