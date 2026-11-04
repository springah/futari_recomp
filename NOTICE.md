# Notice — what the licence covers

This file states the scope of [LICENSE](LICENSE). It is a clarification, not a
grant, and adds no conditions to the BSD 3-Clause licence itself.

## Covered

The contents of this repository — the host application (`src/`), the tooling
(`scripts/`, `extract_assets.bat`, `mac-build/`), the codegen inputs (`*.toml`),
the build files, and the documentation — **except for the two game-derived files
listed below**.

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

## The Renut renderer is GPL-3.0, not BSD

`src/renut_engine/` is a vendored copy of the Renut native renderer (masterspike52/reNut by
way of banjo-nx, futari-nx and cerroverb/futari-linux). It is licensed **GPL-3.0-or-later**,
see `src/renut_engine/LICENSE` and `src/renut_engine/PROVENANCE.md`. The BSD 3-Clause licence
of this repository does not cover that directory, and a binary built with the renderer
(the default) is a combined work distributed under the GPL-3.0; the complete corresponding
source is this repository at the matching release tag. `src/renut_shim/` (this port's
compatibility headers for it) stays BSD 3-Clause.

## Third-party components

The ReXGlue SDK (`thirdparty/rexglue-sdk`) is a vendored copy of a separate
project under its own BSD 3-Clause licence, itself derived from the Xenia
project (Copyright (c) 2022, Ben Vanik and Xenia project contributors). See
`thirdparty/rexglue-sdk/VENDORED.md` for its provenance.

The launcher's font, embedded in the executable as `src/launcher/launcher_font.h`, is a Latin subset of
**Roboto Medium** (Copyright 2011 Google Inc.), licensed **Apache-2.0** (`dist/Roboto.LICENSE`, reproduced in
`THIRD-PARTY-NOTICES.txt`); `scripts/make_launcher_font.sh` regenerates it from Dear ImGui's copy of the font.
