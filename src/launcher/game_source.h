/**
 * @file        game_source.h
 * @brief       The player's game data, from their own copy: a C++ port of scripts/extract_iso.py for the launcher.
 *
 * futari_recomp (BSD 3-Clause), 2026-11-03. Same sources and the same refusals as the script:
 *   - a disc ISO (XDVDFS), located by its volume descriptor;
 *   - a console disc install (the "Games on Demand" SVOD container: the 00007000 folder or anything above it),
 *     verified against its own SHA-1 hash tree before a byte is extracted;
 *   - the marketplace DLC packages (00000002), copied into dlc/ unchanged;
 *   - an already-extracted game folder (default.xex and media/ at its top), copied into assets/.
 * The work runs on a worker thread; Progress carries its lines, a fraction and the cancel flag to the UI.
 */
#pragma once

#include <atomic>
#include <cstddef>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

namespace futari::launcher {

// What the worker tells the screen. Lines are appended under the mutex; the UI copies them out each frame.
struct Progress {
  std::mutex mutex;
  std::vector<std::string> lines;
  float fraction = -1.0f;  // -1 = unknown (indeterminate bar)
  std::atomic<bool> cancel{false};

  void Log(std::string line);
  void SetFraction(float f);
  std::vector<std::string> Snapshot(float& fraction_out);
};

enum class SourceKind {
  kNone,            // nothing of this game at the path
  kIso,             // a file that is not an XContent package: presumably a disc image
  kConsoleInstall,  // the 00007000 disc install (header + .data folder) found at or under the path
  kLooseFolder,     // default.xex + media/ at the folder's top
  kDlcOnly,         // only the 00000002 packages were found
  kWrongTitle,      // an XContent package of another game
  kUnsupported,     // an XContent package of a type the game does not use
};

struct SourceInfo {
  SourceKind kind = SourceKind::kNone;
  std::filesystem::path god_header;             // kConsoleInstall
  std::filesystem::path iso;                    // kIso
  std::filesystem::path loose;                  // kLooseFolder
  std::vector<std::filesystem::path> dlc;       // packages found beside the game (any kind)
  std::string note;                             // what to tell the player (kWrongTitle, kUnsupported, kNone)
};

// Looks at the path the player chose. Cheap (headers only), safe to call from the UI thread.
SourceInfo ClassifySource(const std::filesystem::path& path);

bool IsLooseDump(const std::filesystem::path& dir);

// Is there a game to start in `assets_dir`? default.xex is there AND no extraction was left unfinished. ExtractGame
// puts a marker file (futari_extract_incomplete) into assets/ before it writes the first game file and removes it
// only when everything came out whole, so a cancelled, failed or crashed extraction is asked for again at the next
// start instead of being played from. scripts/extract_iso.py keeps the same marker. Use this, never a bare test for
// default.xex.
bool GameInPlace(const std::filesystem::path& assets_dir);
// The marker alone: something was extracted here and did not finish (for the message on the setup screen).
bool ExtractionUnfinished(const std::filesystem::path& assets_dir);

struct ExtractResult {
  enum class Status { kOk, kFailed, kDlcOnly, kCancelled };
  Status status = Status::kFailed;
  std::string message;                // one line for the screen
  std::vector<std::string> problems;  // the script's PROBLEMS list: truncated files, bad hashes, short copies
  size_t files = 0;                   // files extracted or copied into assets/
  size_t dlc_installed = 0;           // packages copied into dlc/
};

// The whole job: classify, verify, extract or copy, install the DLC, make sure assets/default.xex is in place.
// Worker thread. `assets_dir` is the game data root (the exe's assets/ unless --game_data_root), `dlc_dir` the
// exe's dlc/. Cancelling leaves whatever was written (the next run overwrites it) and the marker GameInPlace reads.
ExtractResult ExtractGame(const std::filesystem::path& source, const std::filesystem::path& assets_dir,
                          const std::filesystem::path& dlc_dir, Progress& progress);

}  // namespace futari::launcher
