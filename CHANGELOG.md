# Changelog

Versions are per-repo; each release is a tag, with per-OS zips on the download
share. Ports can sit on different versions — see [PORTS.md](PORTS.md).

## v1.4.1 — 2026-08-07 (Windows only)

Maintenance rebuild on SDK v0.9.3.1 (up from v0.9.2), plus a package cleanup.

- The Windows bundle no longer ships development leftovers: ~64 MB of superseded
  binary backups, an instrumented reference exe, the Tracy profiler DLL, 212 of the
  developer's own run logs, and a machine-local shortcut. 927.6 MB → 908.5 MB.
- The zip is now packed under a proper top-level folder with standard forward-slash
  entries (v1.4.0 had non-spec backslash entries).
- `RUN.txt` no longer shows `???` where dashes belong.
- The SDK bump carries a cvar fix and a macOS link fix, but an A/B of the two
  runtimes showed no behaviour change for futari — this is a currency rebuild, not
  a gameplay fix.

macOS and Steam Deck remain on v1.4.0.

## v1.4.0 — 2026-08-05 (Windows, macOS, Steam Deck)

First release on the SDK v0.9 line: upstream v0.9.0 merge with XMA audio fixes,
manual switch-table support, a DLL indirect-call fix, SDL3 windowing, and the
split of the GPU backend into the `rexgpu-xenos` plugin.

## Earlier

See the `v1.0.x`–`v1.3.x` tags. v1.3.x tracked the SDK v0.8 line; the recompiler
jump-table bug that crashed game-mode entry was fixed during it.
