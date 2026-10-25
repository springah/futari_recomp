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

## Third-party components

The ReXGlue SDK (`thirdparty/rexglue-sdk`) is a vendored copy of a separate
project under its own BSD 3-Clause licence, itself derived from the Xenia
project (Copyright (c) 2022, Ben Vanik and Xenia project contributors). See
`thirdparty/rexglue-sdk/VENDORED.md` for its provenance.
