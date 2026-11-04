# Notice — what the licence covers

This file states the scope of [LICENSE](LICENSE). It is a clarification, not a
grant, and adds no conditions to the BSD 3-Clause licence itself.

In short: this repository's own files are BSD 3-Clause; the vendored Renut
renderer is GPL-3.0; the release binaries, which link it, are distributed under
the GPL-3.0; and the game, including its recompiled code, is Cave's and is
licensed by neither.

## Covered

The contents of this repository: the host application (`src/`, including the
launcher in `src/launcher/` and the Renut compatibility headers in
`src/renut_shim/`), the tooling (`scripts/`, `extract_assets.bat`,
`mac-build/`), the codegen inputs (`*.toml`), the build files, and the
documentation, **except for the two game-derived files listed below and the
Renut-derived and vendored third-party files named in the next sections**
(`src/renut_engine/`, `tools/renut/`, `cmake/futari_renut_gpu.cmake*`,
`thirdparty/`), which keep their own licences.

## Not covered

The licence does NOT cover, and this project does not grant any rights to:

  * **Mushihimesama Futari** — its code, data, audio, or artwork. All rights are
    owned by Cave Interactive Co., Ltd. No game *data* is distributed here: no
    disc image, no game executable, no `media/`, no audio.
  * **The recompiled C++ under `generated/`.** That is a machine translation of
    the game's own executable code, produced on your machine from a copy you
    own. It is a derivative of Cave's copyrighted work, not of this repository,
    and it is neither distributed here nor licensed by this project. The release
    bundles contain it in compiled form, which is why they are offered only to
    people who own the game, and why nothing in them is covered by the licence.
  * **`screenshots/title.png`** — a screenshot of the game, included for
    identification purposes only.
  * **`src/futari.ico`** — the application icon, taken from the game's own
    artwork and likewise included for identification purposes only. Note that
    `CMakeLists.txt` embeds it in `futari.exe` via `src/futari.rc.in`, so every
    Windows build carries it. It is inside `src/`, but it is **not** licensed by
    this project; if you fork this repository for anything beyond building the
    port for yourself, replace it with your own mark.

## The Renut renderer is GPL-3.0, and so are the release binaries

`src/renut_engine/` is a vendored copy of the Renut native renderer. It is
licensed under the **GNU GPL, version 3** (`src/renut_engine/LICENSE`, copied
from futari-linux; provenance in `src/renut_engine/PROVENANCE.md`). Its authors,
oldest first:

  * **reNut** by masterspike52 (github.com/masterspike52/reNut), the renderer
    itself. reNut has no licence file; masterspike52 granted its reuse for this
    port in [masterspike52/reNut#74](https://github.com/masterspike52/reNut/issues/74)
    (2026-11-03).
  * **banjo-nx** and **futari-nx** by ChanseyIsTheBest
    (github.com/ChanseyIsTheBest/futari-nx, GPL-3.0): the ReXGlue adaptation
    and this game's Direct3D layout, hook tables and vblank thread.
  * **futari-linux** by cerroverb (github.com/cerroverb/futari-linux, GPL-3.0):
    the copy vendored here, which also carries frame-pacing code from hedge-dev's
    UnleashedRecomp (GPL-3.0).

The BSD 3-Clause licence of this repository does not cover that directory, nor
two more pieces taken from the same projects and under the same GPL-3.0:

  * `tools/renut/`: the hook generator, layout table, shader dump, release
    shader table and early-alpha scripts from futari-linux, futari-nx and
    banjo-nx. Two of them, `early_alpha.py` and `renut_early_alpha.py`, ship in
    the Windows bundle's `renut_tools/`.
  * `cmake/futari_renut_gpu.cmake`, adapted from futari-linux's file of the same
    name, which is kept beside it verbatim as `futari_renut_gpu.cmake.upstream`.

**Binaries.** `futari.exe` (or the macOS `futari`) links the renderer, and the
eight game-mode libraries link its per-module hooks, so a build with
`FUTARI_RENUT_RENDERER=ON` (the default) is a
combined work distributed under the GPL-3.0. The full licence text ships in
every bundle, in `THIRD-PARTY-NOTICES.txt`, and the complete corresponding
source is this repository at the matching release tag, with the renderer, plume,
XenosRecomp and the SDK vendored in it.

**This repository's own files are not relicensed.** They stay BSD 3-Clause, and
anyone may take them under that licence on their own; the GPL governs the
combined binaries. That includes `src/renut_shim/`, this port's replacements for
the futari-nx headers the renderer includes, and the launcher.

**The game is outside both licences.** The GPL's corresponding source does not
include the recompiled game code (it is generated on each player's machine from
their own copy, see "Not covered" above), and neither licence grants any right
to it.

## Third-party components

The ReXGlue SDK (`thirdparty/rexglue-sdk`) is a vendored copy of a separate
project under its own BSD 3-Clause licence, itself derived from the Xenia
project (Copyright (c) 2022, Ben Vanik and Xenia project contributors). See
`thirdparty/rexglue-sdk/VENDORED.md` for its provenance.

`thirdparty/plume` (renderbag's render interface, with D3D12 Memory Allocator
and metal-cpp) and `thirdparty/reblue-XenosRecomp` (hedge-dev's XenosRecomp in
the fork made for reblue, as futari-linux carries it, with zstd and smol-v) are
vendored copies under their own licences (MIT, plus MIT, Apache-2.0,
BSD-3-Clause and MIT/Unlicense for their bundled parts). The Windows bundle also
ships Microsoft's DirectX Shader Compiler as `renut_tools/dxcompiler.dll`
(University of Illinois/NCSA Open Source License); its binaries are not in this
repository. Every one of those texts is reproduced in `THIRD-PARTY-NOTICES.txt`.
