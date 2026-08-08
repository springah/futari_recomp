# Two macOS fixes to contribute to rexglue/rexglue-sdk PR #356

Both are small, self-contained, and required for an actual game to run on macOS
arm64 on top of #356. Suggest offering them on the PR (review comment with the
patch, or a follow-up PR against `Graine25:dev/mac-release`). Commits live on
`springah/rexglue-sdk` branch `futari-mac-merge`.

---

## Fix 1 — `EnableHighResolutionTiming()` undefined on Darwin (link error)
Commit `72eab46`. `src/core/threading_mac.cpp`.

`threading_mac.cpp` replaces `threading_posix.cpp` on Apple, but #356 omitted
`rex::thread::EnableHighResolutionTiming()` (defined as a no-op in the POSIX
backend). Result: `librexruntime.dylib` fails to link —
`Undefined symbol rex::thread::EnableHighResolutionTiming()` from
`Runtime::Setup`. Add the Darwin no-op (macOS has no `timeBeginPeriod`
equivalent; the Mach scheduler already provides high-res timing):

```cpp
// macOS has no equivalent of the Windows multimedia timer-resolution API
// (timeBeginPeriod); the Mach scheduler already provides high-resolution
// timing. No-op, matching the POSIX backend.
void EnableHighResolutionTiming() {}
```

---

## Fix 2 — recompiled guest modules can't be `dlopen`ed by bare name on macOS
Commit `ea57c2c`. `src/core/dynlib_posix.cpp`.

Recompiled modules are registered by undecorated name (e.g. `futari_reco_normal`)
and loaded via `DynamicLibrary::Load`, relying on Windows `LoadLibrary`-style
resolution. POSIX `dlopen()` does **not** add a `lib` prefix / platform suffix,
and on macOS does **not** search the executable's directory — so it can't find
`libfutari_reco_normal.dylib` sitting next to the host. The module fails to load,
then the guest call into it traps (`InvalidFunctionTrap`) — manifests as a crash
the moment the game leaves the title for its first mode.

Add a fallback: when given a bare name (no parent dir, no extension) and the
plain `dlopen` fails, resolve `<exe_dir>/lib<name>.dylib` (`.so` on Linux):

```cpp
handle_ = dlopen(path.c_str(), flags);
if (handle_) return true;
if (!path.has_parent_path() && !path.has_extension()) {
#if defined(__APPLE__)
  constexpr const char* kExt = ".dylib";
#else
  constexpr const char* kExt = ".so";
#endif
  const std::filesystem::path exe_dir = rex::filesystem::GetExecutableFolder();
  if (!exe_dir.empty())
    handle_ = dlopen((exe_dir / ("lib" + path.string() + kExt)).c_str(), flags);
}
return handle_ != nullptr;
```

(Also hardens the Linux module-load path.)

---

## Toolchain notes worth adding to #356's docs
- macOS build **requires Homebrew LLVM clang** (≥18). The SDK's compiler-ID gate
  (`CMakeLists.txt`) rejects AppleClang by both ID and version, but the
  `mac-*-base` presets default to bare `clang` (= AppleClang). Either fix the
  presets to point at `/opt/homebrew/opt/llvm/bin/clang(++)` or document it.
- Bundling: MoltenVK loads from the `.app`'s `Contents/lib/libMoltenVK.dylib`
  (and the loader already searches `/opt/homebrew` as a fallback) — works with
  no `VK_ICD_FILENAMES` env once bundled.
