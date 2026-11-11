// Standalone test of src/ini_merge.h (pure std, no SDK): the rules the F4 / F2
// save path relies on when it writes futari.ini back. Runs in CI (Guards,
// "ini-merge-test") on g++, and locally with any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/ini_merge_test.cpp -o ini_merge_test && ./ini_merge_test
//   cl /std:c++20 /EHsc /W4 /I src tests\ini_merge_test.cpp && ini_merge_test.exe
//
// Each case is one thing that bit a cut or a player: the in-place update
// keeping the player's spelling and comments (rule 1), a key the section lacks
// being appended (the renut_vsync line of v1.5.6), the managed block replaced
// wholesale so a dropped cvar really goes (present_vsync on Renut), a save
// that does not grow the file, and CRLF input.
#include "ini_merge.h"

#include <cstdio>
#include <string>
#include <vector>

namespace {

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s---- end ----\n", what, got.c_str());
}

bool Contains(const std::string& text, const std::string& needle) { return text.find(needle) != std::string::npos; }

size_t Count(const std::string& text, const std::string& needle) {
  size_t n = 0;
  for (size_t at = text.find(needle); at != std::string::npos; at = text.find(needle, at + needle.size()))
    ++n;
  return n;
}

const std::string kPlayerIni =
    "futari_renderer = renut\n"
    "# Mushihimesama Futari (recompiled) PC settings\n"
    "\n"
    "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
    "present_vsync = false\n"
    "renut_gpu_backend = vulkan\n"
    "# --- end overlay settings ---\n"
    "\n"
    "[Display]\n"
    "# Window size as WxH.\n"
    "resolution = 1920x1080\n"
    "Fullscreen=true\n"
    "# vsync locks the game to refresh_rate\n"
    "vsync = true\n"
    "refresh_rate = 60\n"
    "\n"
    "[Storage]\n"
    "user_data_path =\n";

void InPlaceUpdateKeepsSpellingAndComments() {
  const std::string out = ini_merge::Merge(
      kPlayerIni, {{"Display", "fullscreen", "false"}, {"display", "VSYNC", "false"}}, "renut_gpu_backend = vulkan\n");
  Check(Contains(out, "Fullscreen= false\n"), "value replaced after the player's own 'Fullscreen=' spelling", out);
  Check(Contains(out, "vsync = false\n"), "vsync line updated (key matched case-insensitively)", out);
  Check(Contains(out, "# vsync locks the game to refresh_rate\n"), "comment above the line kept", out);
  Check(Contains(out, "# Window size as WxH.\n"), "other comments kept", out);
  Check(Contains(out, "[Storage]\nuser_data_path =\n"), "untouched section verbatim", out);
  Check(Count(out, "vsync = ") == 1, "no second vsync line", out);
}

void MissingKeyIsAppendedToItsSection() {
  const std::string out = ini_merge::Merge(kPlayerIni, {{"Display", "renut_vsync", "false"}}, "");
  Check(Contains(out, "refresh_rate = 60\nrenut_vsync = false\n\n[Storage]\n"),
        "key the section lacks lands at the section's end, before the blank line", out);
}

void ManagedBlockIsReplacedWholesale() {
  // The F4 save on Renut no longer carries present_vsync: the old line must go,
  // the block must still sit at the top level before the first header, and the
  // player's own top-level line above it must survive.
  const std::string out = ini_merge::Merge(kPlayerIni, {}, "renut_gpu_backend = vulkan\n");
  Check(!Contains(out, "present_vsync"), "a cvar missing from the new block is dropped", out);
  Check(Count(out, ini_merge::kBlockBegin.data()) == 1, "exactly one block begin marker", out);
  Check(Count(out, ini_merge::kBlockEnd.data()) == 1, "exactly one block end marker", out);
  Check(out.rfind("futari_renderer = renut\n", 0) == 0, "player's top-level renderer line still first", out);
  Check(out.find(ini_merge::kBlockEnd.data()) < out.find("[Display]"), "block precedes the first section header", out);
}

void EmptyBlockRemovesTheOldOne() {
  const std::string out = ini_merge::Merge(kPlayerIni, {}, "");
  Check(!Contains(out, "overlay settings"), "no block markers when nothing is managed", out);
  Check(!Contains(out, "renut_gpu_backend"), "old block contents gone with it", out);
  Check(Contains(out, "futari_renderer = renut\n# Mushihimesama"), "surrounding top-level lines intact", out);
}

void RepeatedSavesDoNotGrowTheFile() {
  const std::vector<ini_merge::SectionUpdate> updates = {{"Display", "vsync", "true"}, {"Display", "renut_vsync", "false"}};
  const std::string once = ini_merge::Merge(kPlayerIni, updates, "renut_gpu_backend = vulkan\n");
  const std::string twice = ini_merge::Merge(once, updates, "renut_gpu_backend = vulkan\n");
  const std::string thrice = ini_merge::Merge(twice, updates, "renut_gpu_backend = vulkan\n");
  Check(once == twice && twice == thrice, "merge is idempotent (no accumulating blank lines or duplicate keys)", thrice);
}

void MissingSectionIsAppended() {
  const std::string out = ini_merge::Merge("[Display]\nvsync = true\n", {{"Graphics", "upscaler", "cas"}}, "");
  Check(Contains(out, "[Display]\nvsync = true\n\n[Graphics]\nupscaler = cas\n"),
        "a section the file lacks is appended with its key", out);
}

void CrlfInputIsHandled() {
  const std::string out = ini_merge::Merge("[Display]\r\nvsync = true\r\nrefresh_rate = 60\r\n", {{"Display", "vsync", "false"}}, "");
  Check(Contains(out, "vsync = false\n"), "CRLF line updated", out);
  Check(!Contains(out, "\r"), "output normalised to LF", out);
}

void DamagedBlockMarkersLoseNothing() {
  // Only a begin marker: the file must be left alone rather than truncated.
  const std::string ini = "# --- overlay settings (saved in-game by F4; managed block, hand edits here are lost) ---\n"
                          "present_vsync = false\n"
                          "[Display]\nvsync = true\n";
  const std::string out = ini_merge::Merge(ini, {}, "");
  Check(Contains(out, "present_vsync = false\n"), "unterminated block is not deleted", out);
  Check(Contains(out, "[Display]\nvsync = true\n"), "section after it intact", out);
}

// A file saved with a UTF-8 BOM (PowerShell 5.1's Set-Content, some editors): the block right after the BOM is still
// the block, so a save replaces it rather than adding a second one.
void BomBeforeTheBlockIsReplaced() {
  const std::string ini = "\xEF\xBB\xBF" + std::string(ini_merge::kBlockBegin) +
                          "\nrenut_resolution = 1080p\nfullscreen = 1\n" + std::string(ini_merge::kBlockEnd) +
                          "\n[Display]\nresolution = 1920x1080\n";
  const std::string once = ini_merge::Merge(ini, {}, "renut_resolution = 1080p\n");
  Check(Count(once, ini_merge::kBlockBegin.data()) == 1, "BOM: one block after a save", once);
  Check(Count(once, "renut_resolution") == 1, "BOM: the key once", once);
  Check(!Contains(once, "fullscreen = 1"), "BOM: the old block's contents are replaced", once);
  const std::string twice = ini_merge::Merge(once, {}, "renut_resolution = 1080p\n");
  const std::string thrice = ini_merge::Merge(twice, {}, "renut_resolution = 1080p\n");
  Check(once == twice && twice == thrice, "BOM: repeated saves are stable", thrice);
}

// A file that went through two BOM-adding tools starts with two BOMs: still the same block.
void DoubleBomBeforeTheBlockIsReplaced() {
  const std::string ini = "\xEF\xBB\xBF\xEF\xBB\xBF" + std::string(ini_merge::kBlockBegin) +
                          "\nrenut_resolution = 1080p\n" + std::string(ini_merge::kBlockEnd) +
                          "\n[Display]\nresolution = 1920x1080\n";
  const std::string once = ini_merge::Merge(ini, {}, "renut_resolution = 1080p\nfoo = 1\n");
  Check(Count(once, ini_merge::kBlockBegin.data()) == 1, "double BOM: one block after a save", once);
  Check(Count(once, "foo = 1") == 1, "double BOM: the new key once", once);
  const std::string twice = ini_merge::Merge(once, {}, "renut_resolution = 1080p\nfoo = 1\n");
  const std::string thrice = ini_merge::Merge(twice, {}, "renut_resolution = 1080p\nfoo = 1\n");
  Check(once == twice && twice == thrice, "double BOM: repeated saves are stable", thrice);
}

// A BOM pasted in with a later line (files joined by hand): the begin marker after it still counts.
void BomOnALaterLineBeforeTheBlock() {
  const std::string ini = "fullscreen = true\n\xEF\xBB\xBF" + std::string(ini_merge::kBlockBegin) +
                          "\nrenut_resolution = 1080p\n" + std::string(ini_merge::kBlockEnd) +
                          "\n[Display]\nresolution = 1920x1080\n";
  const std::string out = ini_merge::Merge(ini, {}, "foo = 1\n");
  Check(Count(out, ini_merge::kBlockBegin.data()) == 1, "BOM on a later line: one block after a save", out);
  Check(!Contains(out, "renut_resolution"), "BOM on a later line: the old block's contents are replaced", out);
  Check(Contains(out, "fullscreen = true\n"), "BOM on a later line: the line above it kept", out);
}

}  // namespace

int main() {
  InPlaceUpdateKeepsSpellingAndComments();
  MissingKeyIsAppendedToItsSection();
  ManagedBlockIsReplacedWholesale();
  EmptyBlockRemovesTheOldOne();
  RepeatedSavesDoNotGrowTheFile();
  MissingSectionIsAppended();
  CrlfInputIsHandled();
  DamagedBlockMarkersLoseNothing();
  BomBeforeTheBlockIsReplaced();
  DoubleBomBeforeTheBlockIsReplaced();
  BomOnALaterLineBeforeTheBlock();
  std::printf("ini_merge_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
