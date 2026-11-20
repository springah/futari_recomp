// Standalone test of src/hidden_settings.h (pure std, no SDK): the resolution
// settings the F4 overlay hides, and when a save keeps one of them in the
// managed block. Runs in CI (Guards, "hidden-settings-test") on g++, and
// locally with any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/hidden_settings_test.cpp -o hidden_settings_test && ./hidden_settings_test
//   cl /std:c++20 /EHsc /W4 /I src tests\hidden_settings_test.cpp && hidden_settings_test.exe
//
// A hidden setting is saved only when the file already had it in the managed
// block (a released build saved it from F4, or the player put it there). A
// value that came only from the command line is not saved.
#include "hidden_settings.h"

#include "ini_merge.h"

#include <cstdio>
#include <string>

namespace {

using futari::hidden_settings::HiddenBlockLine;
using futari::hidden_settings::IsHiddenSetting;

bool KeepHiddenInBlock(const char* key, const std::string& ini) {
  return HiddenBlockLine(key, ini).has_value();
}

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s---- end ----\n", what, got.c_str());
}

size_t Count(const std::string& text, const std::string& needle) {
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
    ++n;
  return n;
}

// A futari.ini a released build wrote after an F4 save of renut_resolution.
const std::string kReleasedIni =
    "# Mushihimesama Futari (recompiled) PC settings\n"
    "\n"
    "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
    "futari_renderer = renut\n"
    "renut_resolution = 1080p\n"
    "# --- end overlay settings ---\n"
    "\n"
    "[Display]\n"
    "resolution = 1920x1080\n";

// The shape WriteDefaultIni writes: no managed block, only the friendly line.
const std::string kFreshIni =
    "# Mushihimesama Futari (recompiled) PC settings\n"
    "\n"
    "[Display]\n"
    "resolution = 1920x1080\n"
    "fullscreen = false\n";

void HiddenSettingsAreTheFour() {
  for (const char* name : {"renut_resolution", "resolution", "window_width", "window_height", "resolution_scale"})
    Check(IsHiddenSetting(name), "a resolution setting is hidden", std::string(name) + "\n");
  for (const char* name : {"renut_vsync", "fullscreen", "video_mode_width", "", "Resolution"})
    Check(!IsHiddenSetting(name), "any other setting is shown", std::string(name) + "\n");
}

void HiddenKeyKeptWhenInOldBlock() {
  Check(KeepHiddenInBlock("renut_resolution", kReleasedIni), "a hidden key in the old block stays", kReleasedIni);
  Check(!KeepHiddenInBlock("resolution_scale", kReleasedIni), "a hidden key the block lacks is not added",
        kReleasedIni);
}

void HiddenKeyDroppedWhenOnlyFromCommandLine() {
  Check(!KeepHiddenInBlock("renut_resolution", kFreshIni), "a fresh ini has no block: a command-line value is dropped",
        kFreshIni);
  Check(!KeepHiddenInBlock("resolution", kFreshIni), "the friendly [Display] resolution line is not the SDK cvar",
        kFreshIni);
}

void HiddenKeyOutsideBlockNotDuplicated() {
  const std::string ini = "renut_resolution = 1080p\n" + kReleasedIni.substr(kReleasedIni.find('\n') + 1);
  const std::string hand = "renut_resolution = 720p\n" + kFreshIni;
  Check(!KeepHiddenInBlock("renut_resolution", hand), "a top-level hand line outside the block is not a block key",
        hand);
  const std::string out = ini_merge::Merge(hand, {}, "futari_renderer = renut\n");
  Check(Count(out, "renut_resolution = 720p\n") == 1, "the hand line survives the merge once", out);
  Check(Count(out, "renut_resolution") == 1, "and is not written a second time in the block", out);
  Check(KeepHiddenInBlock("renut_resolution", ini), "a key in both places still counts as in the block", ini);
}

void HiddenKeyInSectionIgnored() {
  const std::string ini = kFreshIni + "renut_resolution = 1080p\n";
  Check(!KeepHiddenInBlock("renut_resolution", ini), "a key inside [Display] is not a block key", ini);
  const std::string after_block = kReleasedIni + "resolution_scale = 2\n";
  Check(!KeepHiddenInBlock("resolution_scale", after_block), "a key below the block's section is not a block key",
        after_block);
}

void BrokenBlockMarkersKeepNothing() {
  const std::string no_end =
      "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
      "renut_resolution = 1080p\n"
      "[Display]\nresolution = 1920x1080\n";
  Check(!KeepHiddenInBlock("renut_resolution", no_end), "a begin marker without an end keeps nothing", no_end);
  const std::string no_begin = "renut_resolution = 1080p\n# --- end overlay settings ---\n[Display]\n";
  Check(!KeepHiddenInBlock("renut_resolution", no_begin), "an end marker without a begin keeps nothing", no_begin);
  const std::string commented =
      "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
      "# renut_resolution = 1080p\n"
      "renut_resolutionx = 1080p\n"
      "# --- end overlay settings ---\n";
  Check(!KeepHiddenInBlock("renut_resolution", commented), "a commented or longer key is not the key", commented);
}

void CrlfIni() {
  std::string crlf;
  for (char c : kReleasedIni) {
    if (c == '\n')
      crlf += '\r';
    crlf += c;
  }
  Check(KeepHiddenInBlock("renut_resolution", crlf), "CRLF: a hidden key in the old block stays", crlf);
  const std::string spaced =
      "  # --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---  \r\n"
      "  renut_resolution=4K\r\n"
      "# --- end overlay settings ---\r\n";
  Check(KeepHiddenInBlock("renut_resolution", spaced), "spacing around markers and '=' as Merge reads it", spaced);
}

// The save loop skips every hidden key the SDK serialised, then writes the file's own block lines (as main.cpp does).
std::string SavedBlock(const std::string& serialised, const std::string& original) {
  std::string raw;
  for (const std::string& line : ini_merge::detail::SplitLines(serialised))
    if (!IsHiddenSetting(ini_merge::detail::LowerTrim(line.substr(0, line.find('=')))))
      raw += line + "\n";
  for (const auto& [key, line] : futari::hidden_settings::HiddenBlockLines(original))
    raw += line + "\n";
  return ini_merge::Merge(original, {}, raw);
}

// The block has renut_resolution = 1080p and this run got --renut_resolution=720p: the save writes the block's own
// line, so the command-line value is not saved over the stored one.
void BlockValueBeatsCommandLine() {
  const auto kept = HiddenBlockLine("renut_resolution", kReleasedIni);
  Check(kept && *kept == "renut_resolution = 1080p", "the block's own line, value from the file",
        kept ? *kept + "\n" : "nothing\n");
  const std::string out = SavedBlock("futari_renderer = renut\nrenut_resolution = 720p\n", kReleasedIni);
  Check(Count(out, "renut_resolution = 1080p\n") == 1, "the saved block keeps 1080p once", out);
  Check(Count(out, "720p") == 0, "the command-line 720p is not saved", out);
  const std::string spaced = "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
                             "  Renut_Resolution=4K\r\n"
                             "# --- end overlay settings ---\n";
  const auto as_typed = HiddenBlockLine("renut_resolution", spaced);
  Check(as_typed && *as_typed == "  Renut_Resolution=4K", "the line as the file has it", as_typed.value_or("nothing"));
  Check(!HiddenBlockLine("resolution_scale", kReleasedIni), "a key the block lacks has no line", kReleasedIni);
}

// renut_resolution is back at its default, so the SDK doesn't serialise it: the block's line still stays, once.
void BlockLineKeptWhenNotSerialised() {
  const std::string out = SavedBlock("futari_renderer = renut\n", kReleasedIni);
  Check(Count(out, "renut_resolution = 1080p\n") == 1, "a default-valued hidden key keeps its block line once", out);
  const std::string listed = SavedBlock("futari_renderer = renut\nrenut_resolution = 720p\n", kReleasedIni);
  Check(Count(listed, "renut_resolution") == 1, "a serialised hidden key is not written twice", listed);
  Check(Count(listed, "renut_resolution = 1080p\n") == 1, "and keeps the file's value", listed);
  Check(SavedBlock("futari_renderer = renut\n", listed) == listed, "repeated saves are stable", listed);
  const std::string twice = "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
                            "resolution_scale = 2\n"
                            "renut_resolution = 1080p\n"
                            "renut_resolution = 4K\n"
                            "# --- end overlay settings ---\n";
  const auto lines = futari::hidden_settings::HiddenBlockLines(twice);
  std::string got;
  for (const auto& [key, line] : lines)
    got += key + ": " + line + "\n";
  Check(got == "renut_resolution: renut_resolution = 1080p\nresolution_scale: resolution_scale = 2\n",
        "each hidden key once, first line, in the hidden list's order", got);
  Check(futari::hidden_settings::HiddenBlockLines(kFreshIni).empty(), "no block: no lines", kFreshIni);
}

// A file saved with a UTF-8 BOM: the block right after it still counts.
void BomBeforeTheBlock() {
  const std::string ini = "\xEF\xBB\xBF" + kReleasedIni.substr(kReleasedIni.find("# --- overlay"));
  Check(KeepHiddenInBlock("renut_resolution", ini), "BOM: a hidden key in the old block stays", ini);
}

// A BOM in front of a key line inside the block (pasted from a BOM file): still the key, and a save keeps the line.
void BomBeforeAKeyLine() {
  const std::string ini = "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
                          "\xEF\xBB\xBFrenut_resolution = 1080p\n"
                          "# --- end overlay settings ---\n"
                          "[Display]\nresolution = 1920x1080\n";
  const auto line = HiddenBlockLine("renut_resolution", ini);
  Check(line && *line == "renut_resolution = 1080p", "BOM before a key: the key's line", line.value_or("nothing\n"));
  const std::string out = SavedBlock("futari_renderer = renut\n", ini);
  Check(Count(out, "renut_resolution = 1080p\n") == 1, "BOM before a key: the line kept after a save", out);
}

// "Reset all to defaults" leaves the renderer choice alone (its default is the other renderer), while F4 still lists it.
void RendererSurvivesResetAll() {
  using futari::hidden_settings::IsResetProofSetting;
  Check(IsResetProofSetting("futari_renderer"), "the renderer choice is reset-proof", "futari_renderer\n");
  Check(!IsHiddenSetting("futari_renderer"), "and still listed in F4", "futari_renderer\n");
  for (const char* name :
       {"renut_vsync", "fullscreen", "renut_resolution", "", "Futari_Renderer", "futari_renderer_native"})
    Check(!IsResetProofSetting(name), "any other setting is reset as before", std::string(name) + "\n");
  // Reset all leaves futari_renderer = renut, so the SDK still serialises it and a save keeps the line. This pins the
  // save side only: the serialised line is given here, so the overlay's reset loop is checked in a dev run, not here.
  const std::string out = SavedBlock("futari_renderer = renut\n", kReleasedIni);
  Check(Count(out, "futari_renderer = renut\n") == 1, "a save after Reset all keeps the renderer line once", out);
}

}  // namespace

int main() {
  HiddenSettingsAreTheFour();
  HiddenKeyKeptWhenInOldBlock();
  HiddenKeyDroppedWhenOnlyFromCommandLine();
  HiddenKeyOutsideBlockNotDuplicated();
  HiddenKeyInSectionIgnored();
  BrokenBlockMarkersKeepNothing();
  CrlfIni();
  BlockValueBeatsCommandLine();
  BlockLineKeptWhenNotSerialised();
  BomBeforeTheBlock();
  BomBeforeAKeyLine();
  RendererSurvivesResetAll();
  std::printf("hidden_settings_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
