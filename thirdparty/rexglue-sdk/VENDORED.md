# Vendored ReXGlue SDK

This directory is a **vendored copy** of the ReXGlue SDK, not a submodule. It was
taken verbatim from `springah/rexglue-sdk` (a fork of
[rexglue/rexglue-sdk](https://github.com/rexglue/rexglue-sdk)) at tag
**v0.9.3.1**, commit `040d5bca3de2f2970e9e29b4bf789739a48f5301`.

This port maintains its own SDK from here: fixes land in this tree directly and
are cherry-picked to/from the fork or upstream when they apply. There is no pin
to bump — the tree *is* the SDK.

Changes made *after* vendoring are ordinary commits in this repository's
history — `git log -- thirdparty/rexglue-sdk` is the authoritative record. The
list below describes only the vendoring commit itself.

## Deviations from the v0.9.3.1 tag

- **`tools/binutils/` is dropped** (~130 MB of prebuilt Windows PowerPC binutils
  plus `cygwin1.dll`). It is used only by `cmake/ppc_test_pipeline.cmake` to
  assemble the SDK's own PPC test cases, which are not run from this repository —
  and redistributing prebuilt GPL binaries in a public source repo would carry a
  corresponding-source obligation. Restore it from the fork tag if the test
  pipeline is ever needed here.
- **`REXGLUE_VERSION` added** (contains the vendored tag). `cmake/rex_version.cmake`
  is patched to read it before falling back to `git describe`: a vendored tree has
  no git history of its own, and `git describe` here would otherwise pick up the
  *host* repository's tags (which hard-fails the version floor check). The file
  is deliberately not named `VERSION` — the SDK root is on the include path, and
  on case-insensitive filesystems (macOS) libc++ resolves C++20's
  `#include <version>` to it, breaking every TU.
- **Nested third-party dependencies stay git submodules**, now registered in the
  host repository's root `.gitmodules` at `thirdparty/rexglue-sdk/thirdparty/*`,
  pointing at their public upstreams. The SDK's own `.gitmodules` file is kept
  in place for reference but git does not read it (only the root one counts).
- **Compiler flags reach clang-cl in its own spellings** (root `CMakeLists.txt`,
  2026-11-07). clang-cl read the GNU `-Wall` as MSVC's `/Wall` (every warning
  clang has: ~170,000 `-Wc++98-compat` lines per build log), dropped
  `-ffp-model=strict` as an unknown argument and never applied `-O3`, so the
  Windows builds compiled at CMake's `/O2` with the default `/fp:precise` while
  the clang++ builds had strict FP and `-O3`. On the MSVC frontend the flags are
  now `/clang:-Wall /clang:-Wextra`, `/fp:strict` (clang-cl's alias of
  `-ffp-model=strict`) and `/clang:-O3`; the clang++ branch is unchanged.

- `cmake/rexglue_helpers.cmake`: on macOS the host's post-build step staged
  `$<TARGET_RUNTIME_DLLS>`, which CMake defines for DLL platforms only, so it
  was a no-op and a dev-tree `futari` died at launch with "Library not loaded:
  @rpath/librexruntime.dylib" (the bundle script copies the dylibs itself, so
  the .app never showed it). It now copies `librexruntime.dylib` and
  `libTracyClient.dylib` by target, as the FidelityFX branch does on Windows.

- `src/system/gpu_plugin_loader.cpp`: `PluginFileName()` asked for
  `librexgpu-<name>.so` on every non-Windows platform; on macOS it now asks
  for `.dylib`, the name CMake builds and the helper stages (the bundle script
  used to rename the file to `.so` to satisfy it).

## Auditing against the source tag

```sh
git -C <a-rexglue-sdk-checkout> archive v0.9.3.1 | tar -x -C /tmp/sdk-tag
diff -r --exclude=tools /tmp/sdk-tag thirdparty/rexglue-sdk
# expected deltas: VERSION, VENDORED.md, cmake/rex_version.cmake, empty submodule dirs
```
