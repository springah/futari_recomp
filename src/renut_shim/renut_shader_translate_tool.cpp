/**
 * @file        renut_shader_translate_tool.cpp
 * @brief       Tool mode for the extractor, step 2: XenosRecomp built into futari.exe (see the header).
 */
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "renut_shim/renut_shader_translate_tool.h"

// thirdparty/reblue-XenosRecomp/XenosRecomp/main.cpp, FUTARI_RENUT_IN_EXE.
int futari_xenos_recomp_main(int argc, char** argv);

namespace futari::renut_tool {

namespace {

std::filesystem::path ExeDir() {
  std::wstring buffer(1024, L'\0');
  const DWORD n = GetModuleFileNameW(nullptr, buffer.data(), DWORD(buffer.size()));
  buffer.resize(n);
  return std::filesystem::path(buffer).parent_path();
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

  std::filesystem::path common = args.common.empty() ? tools / "shader_common.h" : args.common;
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
    } else if (std::filesystem::is_regular_file(tools / "dxcompiler.dll", ec)) {
      dxc = tools / "dxcompiler.dll";
    } else if (std::filesystem::is_regular_file(exe_dir / "dxcompiler.dll", ec)) {
      dxc = exe_dir / "dxcompiler.dll";
    }
  }
  // Load it here first, so a missing or broken DLL is one plain error instead of 42 "dxc-spirv-compile-failed"
  // lines (the translator's own message would be buried under them). The same module handle serves the
  // translator's own load (dxc_compiler.cpp); the loader just counts the reference up.
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
  TerminateProcess(GetCurrentProcess(), static_cast<UINT>(code));
  std::_Exit(code);  // not reached
}

}  // namespace futari::renut_tool
