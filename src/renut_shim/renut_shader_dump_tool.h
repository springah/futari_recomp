/**
 * @file        renut_shader_dump_tool.h
 * @brief       "Tool mode": dump the game's Xbox 360 shader containers from the loaded executables.
 *
 * futari_recomp (BSD 3-Clause). A C++ port of tools/renut/renut_dump_shaders.py that reads the images from the
 * running runtime instead of from <module>_image.bin files, so the player's own machine can produce the folder
 * Renut's shader cache is built from (XenosRecomp input). The output is byte-identical to the script's:
 * {ps|vs}_{xxh3_64:016x}.bin plus manifest.csv.
 */

#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

namespace rex {
class Runtime;
}

namespace futari::renut_tool {

struct DumpResult {
  size_t occurrences = 0;  // containers found, counting repeats across modules (manifest rows)
  size_t unique = 0;       // distinct containers written as .bin files
  size_t rejected = 0;     // 0x102A11xx candidates that are not containers
  std::vector<std::string> notes;  // per-module summaries, rejected candidates and problems
};

// The runtime has loaded default.xex (the entrypoint) and nothing else; the game has not started. Dumps the
// entrypoint's containers, then loads each of the eight game cores in turn (call_entry = false), dumps it and
// unloads it again. Returns false, with `error` set, only when the entrypoint yields nothing or the output cannot
// be written; a core that fails to load is a note.
bool DumpShaders(rex::Runtime& runtime, const std::filesystem::path& out_dir, DumpResult& result,
                 std::string& error);

}  // namespace futari::renut_tool
