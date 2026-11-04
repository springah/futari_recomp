/**
 * @file        renut_shader_translate_tool.cpp
 * @brief       Tool mode for the extractor, step 2: XenosRecomp built into futari.exe (see the header).
 */
#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
// futari_recomp (2026-11-04, leg 6): macOS, libdxcompiler.dylib by dlopen as dxcompiler.dll by LoadLibrary.
#include <dlfcn.h>
#include <mach-o/dyld.h>
#include <unistd.h>
#endif

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <vector>

#include "renut_shim/renut_shader_translate_tool.h"

// thirdparty/reblue-XenosRecomp/XenosRecomp/main.cpp, FUTARI_RENUT_IN_EXE.
int futari_xenos_recomp_main(int argc, char** argv);

namespace futari::renut_tool {

namespace {

#ifdef _WIN32
constexpr const char* kDxcompilerName = "dxcompiler.dll";

std::filesystem::path ExeDir() {
  std::wstring buffer(1024, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
  buffer.resize(n);
  return std::filesystem::path(buffer).parent_path();
}
#else
constexpr const char* kDxcompilerName = "libdxcompiler.dylib";

std::filesystem::path ExeDir() {
  uint32_t size = 0;
  _NSGetExecutablePath(nullptr, &size);
  std::string buffer(size, '\0');
  if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
    return {};
  }
  std::error_code ec;
  const auto canonical = std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec);
  return (ec ? std::filesystem::path(buffer.c_str()) : canonical).parent_path();
}
#endif

// The first of `candidates` that is a regular file, or empty.
std::filesystem::path FirstFile(std::initializer_list<std::filesystem::path> candidates) {
  std::error_code ec;
  for (const auto& c : candidates) {
    if (std::filesystem::is_regular_file(c, ec)) {
      return c;
    }
  }
  return {};
}

}  // namespace

int Translate(const TranslateArgs& args, std::string& error) {
  std::error_code ec;
  if (args.dump.empty() || !std::filesystem::is_directory(args.dump, ec)) {
    error = "no shader dump folder: " + args.dump.string();
    return 2;
  }
  const std::filesystem::path exe_dir = ExeDir();
  const std::filesystem::path tools = exe_dir / "renut_tools";

  // macOS bundle: no data file may sit in Contents/MacOS (the code seal), so there the header is in
  // Contents/Resources/renut_tools and the dylib in Contents/lib (mac-build/build_app.sh).
  std::filesystem::path common =
      args.common.empty()
          ? FirstFile({tools / "shader_common.h", exe_dir / ".." / "Resources" / "renut_tools" / "shader_common.h"})
          : args.common;
  if (common.empty()) {
    common = tools / "shader_common.h";
  }
  if (!std::filesystem::is_regular_file(common, ec)) {
    error = "no shader header: " + common.string() + " (the renut_tools folder beside the exe is incomplete)";
    return 2;
  }
  const std::filesystem::path out =
      args.out.empty() ? args.dump.parent_path() / "shader_cache_translated.cpp" : args.out;

  // The DirectX Shader Compiler: the argument, else the environment make_shaders.py may have set, else beside the exe.
  std::filesystem::path dxc = args.dxcompiler;
  if (dxc.empty()) {
    if (const char* env = std::getenv("FUTARI_RENUT_DXCOMPILER"); env && *env) {
      dxc = env;
    } else {
      dxc = FirstFile({tools / kDxcompilerName, exe_dir / kDxcompilerName, exe_dir / ".." / "lib" / kDxcompilerName});
    }
  }
  // Load it here first, so a missing or broken DLL is one plain error instead of 42 "dxc-spirv-compile-failed"
  // lines (the translator's own message would be buried under them). The same module handle serves the
  // translator's own load (dxc_compiler.cpp); the loader just counts the reference up.
#ifdef _WIN32
  {
    HMODULE module = dxc.empty() ? LoadLibraryW(L"dxcompiler.dll")
                                 : LoadLibraryExW(dxc.c_str(), nullptr, LOAD_WITH_ALTERED_SEARCH_PATH);
    const DWORD load_error = module ? 0 : GetLastError();
    if (module == nullptr || GetProcAddress(module, "DxcCreateInstance") == nullptr) {
      error = "cannot load the DirectX Shader Compiler " + (dxc.empty() ? std::string("dxcompiler.dll") : dxc.string()) +
              " (Windows error " + std::to_string(load_error) +
              "): the renut_tools folder beside the exe is incomplete, or the DLL was quarantined";
      return 2;
    }
  }
  if (!dxc.empty()) {
    // UTF-8, which the translator's loader decodes (a path is handed over as text, so it must be one encoding).
    const std::u8string u8 = dxc.u8string();
    _putenv_s("FUTARI_RENUT_DXCOMPILER", reinterpret_cast<const char*>(u8.c_str()));
  }
#else
  {
    void* module = dlopen(dxc.empty() ? kDxcompilerName : dxc.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (module == nullptr || dlsym(module, "DxcCreateInstance") == nullptr) {
      const char* why = dlerror();
      error = "cannot load the DirectX Shader Compiler " + (dxc.empty() ? std::string(kDxcompilerName) : dxc.string()) +
              " (" + (why ? why : "no DxcCreateInstance") + "): the bundle's copy is missing or damaged";
      return 2;
    }
  }
  if (!dxc.empty()) {
    setenv("FUTARI_RENUT_DXCOMPILER", dxc.c_str(), 1);
  }
#endif

  std::vector<std::string> words = {"futari.exe", args.dump.string(), out.string(), common.string()};
  if (!args.hlsl_dump.empty()) {
    words.push_back(args.hlsl_dump.string());
  }
  std::vector<char*> argv;
  for (auto& w : words) {
    argv.push_back(w.data());
  }
  argv.push_back(nullptr);

  std::fflush(stdout);
  std::fflush(stderr);
  const int rc = futari_xenos_recomp_main(int(words.size()), argv.data());
  std::fflush(stdout);
  std::fflush(stderr);
  return rc;
}

[[noreturn]] void EndProcess(int code) {
  std::fflush(stdout);
  std::fflush(stderr);
#ifdef _WIN32
  TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
#endif
  std::_Exit(code);  // macOS: no atexit handlers, no static destructors (the same reason as TerminateProcess)
}

}  // namespace futari::renut_tool
