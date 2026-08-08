# Contributing

This is a hobby port maintained in spare time. Issues and pull requests are
welcome; please read the two rules below first, because they are the ones that
actually matter here.

## 1. Never attach game data

No file derived from the game belongs in an issue, a pull request, or this
repository — not `default.xex`, not a module XEX, not a `.iso`, not an STFS DLC
package, not the recompiled C++ under `generated/`. CI rejects tracked game
data, and issues containing it will be deleted rather than edited.

If you are reporting a crash, attach the log from `logs/futari_NNN.log`. Skim it
first: it contains local filesystem paths, and depending on your `futari.ini` it
may include your user data directory. Redact anything you would not post
publicly.

## 2. Bug reports need a build and a dump you can describe

Because everyone builds from their own dump, "it crashes" is not actionable on
its own. Useful reports include:

- the release version or commit you built from, and your OS;
- the SDK the binary reports — `strings <binary> | grep rexglue-v`;
- confirmation that your dump matches the checksums in
  [docs/DUMPING.md](docs/DUMPING.md) (a mismatched revision or region behaves
  differently, and that is by far the most common cause of odd reports);
- the tail of the log around the failure.

## Pull requests

- Keep changes scoped, and match the surrounding style — the tracked C++ is one
  file and follows Google-ish formatting; the shell scripts are `set -euo
  pipefail` and shellcheck-clean at error severity.
- `scripts/regen.sh` output is not tracked. If a change alters codegen input
  (`*.toml`), say what you regenerated and what you observed in-game.
- Recompiler and runtime bugs usually belong in the
  [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk), not here. This repo is
  the host app, the codegen inputs, and the build; anything about how PowerPC
  becomes C++, or how the runtime behaves, is upstream. When in doubt, open the
  issue here and it can be moved.
- By contributing you agree your changes are licensed under this repository's
  [BSD 3-Clause licence](LICENSE).

## What this project will not do

Requests for game files, links to dumps, or help obtaining the game by any route
other than your own disc and console will be closed without discussion. See
[docs/DUMPING.md](docs/DUMPING.md) for the supported path.
