/**
 * @file        shader_pipeline.h
 * @brief       vulkan_spirv.bin for the Renut renderer, made by the launcher without Python.
 *
 * futari_recomp (BSD 3-Clause), 2026-11-03. scripts/make_shaders.py's two steps, run as child processes of this
 * same exe (its tool modes --futari_renut_dump_shaders and --futari_renut_translate): no helper program ships,
 * nothing new for an antivirus to judge. One translation pass only: the early-alpha rewrite make_shaders.py runs
 * between two passes is a Python tool (renut_tools/early_alpha.py, numpy) and a performance pass, not a correctness
 * one, so the launcher's package draws the same picture with a few of its pixel shaders unoptimised. Windows only
 * while the translator is (cmake/futari_renut_gpu.cmake).
 */
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

#include "launcher/game_source.h"

namespace futari::launcher {

struct ShaderEnv {
  std::filesystem::path exe;      // this exe
  std::filesystem::path exe_dir;  // where renut_tools/ and the package live
  std::filesystem::path assets;   // the game data root
};

struct ShaderResult {
  bool ok = false;
  std::string message;
  int shaders = 0;
  uint64_t bytes = 0;
};

// The header and table of a BVKS package (render/shader_library.cpp): the shader count, or false with a reason.
bool CheckPackage(const std::filesystem::path& package, int& count, std::string& why);

// The package beside the exe, if one is there and usable.
bool PackageStatus(const std::filesystem::path& exe_dir, int& count, uint64_t& bytes);

// renut_tools/ beside the exe holds what the translation needs (the DXC DLL and the shader header).
bool ToolsPresent(const std::filesystem::path& exe_dir, std::string& missing);

// True on the builds whose exe carries the translator (the pipeline can run at all).
bool TranslatorAvailable();

// Worker thread. Dumps, translates, checks, installs the package beside the exe and deletes the work folder.
ShaderResult MakeShaderPackage(const ShaderEnv& env, Progress& progress);

}  // namespace futari::launcher
