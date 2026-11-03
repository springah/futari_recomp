# Renut renderer: provenance and licence

This directory is futari_recomp's copy of the **Renut** Direct3D-level renderer, vendored
2026-11-03 from `app/src/renut_engine/` of **cerroverb/futari-linux** at commit deca8dd
(2026-11-01). Lineage, oldest first:

1. **reNut** by masterspike52 (github.com/masterspike52/reNut): the renderer itself, written
   for a Banjo-Kazooie: Nuts & Bolts recompilation. The repository carries no LICENSE file; its
   README says anyone is free to fork it, and the author confirmed that for this port in
   github.com/masterspike52/reNut/issues/74 (2026-11-03): "i have no problems with anyone doing
   such for any reason."
2. **banjo-nx** (ChanseyIsTheBest): the ReXGlue adaptation (`gpu/banjo_renut_*`, the SDK seam).
3. **futari-nx** (ChanseyIsTheBest, GPL-3.0): Renut taught Futari's Direct3D layout, hook
   tables and vblank thread (`futari_renut_*`, `futari_d3d_functions.txt`).
4. **futari-linux** (cerroverb, GPL-3.0): the Linux port, display-locked vblank, present_wait,
   the release-shader model this port follows.

## Licence

Files in this directory are **GPL-3.0-or-later** (see `LICENSE` here, copied from
futari-linux), with reNut's own permission above covering the reNut-derived parts. This is a
different licence from the rest of futari_recomp (BSD 3-Clause, see `../../LICENSE`):

- Source: the BSD licence of the repository does not apply to this directory.
- Binaries: a build that links this renderer (`FUTARI_RENUT_RENDERER=ON`, the default) is a
  combined work and is distributed under the GPL-3.0. The release notes and
  `THIRD-PARTY-NOTICES.txt` say so, and the complete corresponding source is this repository
  at the release tag.

Changes made here after vendoring are ordinary commits in this repository's history
(`git log -- src/renut_engine`). Upstream syncs: diff against futari-linux's
`app/src/renut_engine/`; keep edits minimal and marked `futari_recomp:` so they survive.

Not vendored: futari-nx's `me/` native PM4 renderer, its CPU natives and codegen rounds, and
everything Switch (deko3d, uam, the NRO build). The five futari-nx headers this code includes
(`me/me_platform_compat.h`, `cpu/banjo_enhancements.h`, `native/futari_native.h`,
`profiler/profiler.h`, `me/masseffect/banjo_pmull_fingerprint.h`) are replaced by
futari_recomp's own shims in `../renut_shim/`.
