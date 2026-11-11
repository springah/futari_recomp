// The resolution settings the F4 overlay hides, and when a futari.ini save keeps one in the managed block
// (SaveFutariIni, src/main.cpp); also the settings F4's "Reset all to defaults" leaves alone. Pure std so a test can run the rules without the SDK
// (tests/hidden_settings_test.cpp).
//
// The window size lives in the friendly [Display] resolution line; the other resolution settings still work from a
// hand-edited ini or the command line, but F4 doesn't list them and a save writes one only when the file's managed
// block already had it (a released build saved it from F4, or the player put it there), with the value it had there.
#pragma once

#include "ini_merge.h"

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace futari::hidden_settings {

// Renut's frame preset, the SDK's resolution preset, the window size cvars ([Display] resolution carries them) and
// the default renderer's texture scale.
inline constexpr std::string_view kHiddenSettings[] = {"renut_resolution", "resolution", "window_width",
                                                       "window_height", "resolution_scale"};

constexpr bool IsHiddenSetting(std::string_view name) {
  for (std::string_view hidden : kHiddenSettings)
    if (name == hidden)
      return true;
  return false;
}

// Settings F4 lists but "Reset all to defaults" leaves alone: the renderer choice (init-only; its default is the xenos
// renderer, so a reset and save would quietly start the next run without Renut). F4 can't change it (its row is
// disabled); the renderer is chosen through the launcher, futari.ini or the command line.
inline constexpr std::string_view kResetProofSettings[] = {"futari_renderer"};

constexpr bool IsResetProofSetting(std::string_view name) {
  for (std::string_view kept : kResetProofSettings)
    if (name == kept)
      return true;
  return false;
}

// The first `key = ...` line inside the original file's managed block, as the file has it (markers matched as
// ini_merge::Merge matches them; no complete block has no line). A save writes this line rather than the cvar's
// current value, so a value given only on the command line never replaces the stored one.
inline std::optional<std::string> HiddenBlockLine(std::string_view key, std::string_view original_ini) {
  using ini_merge::detail::LowerTrim;
  const std::vector<std::string> lines = ini_merge::detail::SplitLines(original_ini);
  const auto block = ini_merge::detail::ManagedBlockRange(lines);
  if (!block)
    return std::nullopt;
  const std::string wanted = LowerTrim(key);
  for (size_t i = block->first + 1; i < block->second; ++i) {
    const std::string& line = lines[i];
    const size_t eq = line.find('=');
    const std::string trimmed = LowerTrim(line);
    if (eq == std::string::npos || trimmed.empty() || trimmed.front() == '#' || trimmed.front() == ';')
      continue;
    if (LowerTrim(std::string_view(line).substr(0, eq)) == wanted)
      return line;
  }
  return std::nullopt;
}

// The managed-block lines a save writes for the hidden settings (key, line): each hidden key the file's block has, once,
// with its HiddenBlockLine, in kHiddenSettings order. Whether the SDK serialised the cvar this run doesn't matter (a
// value back at its default isn't serialised), so the caller skips the serialised hidden lines and writes these.
inline std::vector<std::pair<std::string, std::string>> HiddenBlockLines(std::string_view original_ini) {
  std::vector<std::pair<std::string, std::string>> out;
  for (std::string_view key : kHiddenSettings)
    if (auto line = HiddenBlockLine(key, original_ini))
      out.emplace_back(std::string(key), std::move(*line));
  return out;
}

}  // namespace futari::hidden_settings
