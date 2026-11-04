/**
 * @file        renut_shader_translate_tool.h
 * @brief       Tool mode for the extractor, step 2: XenosRecomp built into futari.exe (futari_recomp, 2026-11-03).
 *
 * scripts/make_shaders.py runs the game's exe three times to make vulkan_spirv.bin from the player's own game: the
 * container dump (renut_shader_dump_tool.h), then this translation twice around the early-alpha pass. The translator
 * is Renut's XenosRecomp (thirdparty/reblue-XenosRecomp, FUTARI_RENUT_IN_EXE) linked into the exe, so no separate
 * executable ships: Windows Defender's cloud heuristics quarantined v1.5.5-rc1's standalone XenosRecomp.exe on every
 * player's PC. The DirectX Shader Compiler stays a DLL (renut_tools/dxcompiler.dll), loaded only when this runs.
 */
#pragma once

#include <filesystem>
#include <string>

namespace futari::renut_tool {

struct TranslateArgs {
  std::filesystem::path dump;        // the folder renut_shader_dump_tool wrote (required)
  std::filesystem::path out;         // the shader-cache .cpp XenosRecomp always writes (default: beside the dump)
  std::filesystem::path common;      // shader_common.h (default: renut_tools/shader_common.h beside the exe)
  std::filesystem::path hlsl_dump;   // the HLSL dump folder of the early-alpha pass (optional)
  std::filesystem::path dxcompiler;  // dxcompiler.dll (default: FUTARI_RENUT_DXCOMPILER, else renut_tools/ beside the exe)
};

// Runs XenosRecomp's main with the equivalent command line. The package itself is asked for through the environment
// as for the standalone tool (XENOS_RECOMP_SPIRV_PACKAGE, BANJO_HLSL_DUMP_ONLY, BANJO_HLSL_OVERRIDE), set by
// make_shaders.py. Returns the tool's exit code (0 = every shader translated); `error` names a refused argument.
int Translate(const TranslateArgs& args, std::string& error);

// Ends the process with `code` at once: stdio flushed, then TerminateProcess. A tool mode must not take the normal
// exit path: ExitProcess unloads dxcompiler.dll and runs the translator's thread-local destructors (its DXC objects,
// the STL's parallel workers) after the compile work, which fast-fails the process (0xC0000409) once the package is
// already written.
[[noreturn]] void EndProcess(int code);

}  // namespace futari::renut_tool
