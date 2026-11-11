// Standalone test of src/ini_resolution.h (pure std, no SDK): the [Display]
// resolution a futari.ini save writes. Runs in CI (Guards, "ini-resolution-test")
// on g++, and locally with any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/ini_resolution_test.cpp -o ini_resolution_test && ./ini_resolution_test
//   cl /std:c++20 /EHsc /W4 /I src tests\ini_resolution_test.cpp && ini_resolution_test.exe
//
// The line holds the frame in physical pixels; the window cvars hold it in
// logical pixels. Unchanged since boot, the size goes back as typed; a size the
// launcher picked goes out as it is, even when it equals the boot logical size.
#include "ini_resolution.h"

#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {

using futari::ini_resolution::BootAtDpi;
using futari::ini_resolution::BootResolution;
using futari::ini_resolution::RepairedResolution;
using futari::ini_resolution::SavedResolution;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s\n---- end ----\n", what, got.c_str());
}

void CheckSaved(const char* w, const char* h, bool picked, const BootResolution& boot, const char* want,
                const char* what) {
  const auto [got_w, got_h] = SavedResolution(w, h, picked, boot);
  const std::string got = got_w.empty() && got_h.empty() ? std::string() : got_w + "x" + got_h;
  Check(got == want, what, got.empty() ? std::string("nothing") : got);
}

// 1920x1080 typed at 150%: the window cvars became 1280x720.
constexpr BootResolution kAt150{1920, 1080, 1280, 720};
// 3840x2160 typed at 200%: 1920x1080.
constexpr BootResolution kAt200{3840, 2160, 1920, 1080};
// 1600x900 typed at 125%: 1280x720.
constexpr BootResolution kAt125{1600, 900, 1280, 720};

void Unchanged() {
  CheckSaved("1280", "720", false, kAt150, "1920x1080", "150%: no change saves the size as typed");
  CheckSaved("1920", "1080", false, kAt200, "3840x2160", "200%: no change saves the size as typed");
  CheckSaved("1280", "720", false, kAt125, "1600x900", "125%: no change saves the size as typed");
  CheckSaved("1280", "720", false, {1280, 720, 1280, 720}, "1280x720", "100%: the typed size is the logical one");
}

void PickEqualToTheBootLogicalSize() {
  CheckSaved("1280", "720", true, kAt150, "1280x720", "150%: an F2 pick of 1280x720 is still a pick");
  CheckSaved("1920", "1080", true, kAt200, "1920x1080", "200%: an F2 pick of 1920x1080 is still a pick");
  CheckSaved("1280", "720", true, kAt125, "1280x720", "125%: an F2 pick of 1280x720 is still a pick");
}

void OtherPicks() {
  CheckSaved("2560", "1440", true, kAt150, "2560x1440", "150%: an F2 pick of 2560x1440 goes out as picked");
  CheckSaved("1920", "1080", true, kAt150, "1920x1080", "150%: an F2 pick of the typed size goes out as picked");
  CheckSaved("1600", "900", false, kAt150, "1600x900", "a size set without the launcher (no flag) goes out as typed");
}

void NoBootLine() {
  CheckSaved("1280", "720", false, {}, "1280x720", "no [Display] resolution at boot: the cvars as they are");
  CheckSaved("2560", "1440", true, {}, "2560x1440", "no [Display] resolution at boot, a pick: as picked");
}

void CheckAtDpi(int32_t w, int32_t h, uint32_t dpi, const BootResolution& want, const char* what) {
  const BootResolution got = BootAtDpi(w, h, dpi);
  Check(got.width == want.width && got.height == want.height && got.logical_width == want.logical_width &&
            got.logical_height == want.logical_height,
        what,
        std::to_string(got.width) + "x" + std::to_string(got.height) + " logical " +
            std::to_string(got.logical_width) + "x" + std::to_string(got.logical_height));
}

// The window cvars for a typed size at a DPI: the SDK's SizeToLogical, rounded up.
void AtDpi() {
  CheckAtDpi(1920, 1080, 96, {1920, 1080, 1920, 1080}, "100%: the typed size is the logical one");
  CheckAtDpi(1920, 1080, 144, kAt150, "150%: 1920x1080 is 1280x720 logical");
  CheckAtDpi(3840, 2160, 192, kAt200, "200%: 3840x2160 is 1920x1080 logical");
  CheckAtDpi(1600, 900, 120, kAt125, "125%: 1600x900 is 1280x720 logical");
  CheckAtDpi(1600, 900, 144, {1600, 900, 1067, 600}, "150%: 1600x900 rounds up to 1067x600 logical");
  CheckAtDpi(1600, 900, 0, {1600, 900, 1600, 900}, "an unknown DPI (0) is 100%");
  // The window opened on a 150% monitor after the boot sized it at 100%: the cvars are re-derived at 144, and a save
  // with no change since still writes the size as typed.
  CheckSaved("1067", "600", false, BootAtDpi(1600, 900, 144), "1600x900",
             "re-derived at the window's DPI: no change saves the size as typed");
}

void AbsentCvars() {
  CheckSaved("", "", false, kAt150, "", "absent cvars save nothing");
  CheckSaved("", "720", true, kAt150, "", "an absent width saves nothing");
  CheckSaved("1280", "", false, kAt150, "", "an absent height saves nothing");
}

// The window cvars at 0 (their default): the next boot ignores a 0x0 line, so none is written.
void ZeroSize() {
  CheckSaved("0", "0", false, kAt150, "", "0x0 saves nothing");
  CheckSaved("0", "0", true, kAt150, "", "0x0 saves nothing, even after a pick");
  CheckSaved("0", "0", false, {}, "", "0x0 with no boot line saves nothing");
  CheckSaved("0", "720", false, kAt150, "", "a zero width saves nothing");
  CheckSaved("1280", "0", false, {}, "", "a zero height saves nothing");
}

// Any width or height the next boot would reject (it reads [Display] resolution as two positive int32 values) saves
// nothing, whichever way the size came.
void SizeTheBootRejects() {
  CheckSaved("00", "720", false, kAt150, "", "00 saves nothing");
  CheckSaved("-5", "720", false, kAt150, "", "a negative width saves nothing");
  CheckSaved("-1920", "-1080", false, {}, "", "a negative size saves nothing");
  CheckSaved("1920", "-1080", true, kAt150, "", "a negative height after a pick saves nothing");
  CheckSaved("+5", "720", false, kAt150, "", "+5 saves nothing");
  CheckSaved("2147483648", "720", false, kAt150, "", "an int32 overflow saves nothing");
  CheckSaved("1920.0", "1080", true, kAt150, "", "1920.0 saves nothing");
  CheckSaved("0x780", "1080", true, kAt150, "", "0x780 saves nothing");
  CheckSaved("1920x1080", "1080", true, kAt150, "", "1920x1080 as a width saves nothing");
  CheckSaved("2147483647", "1080", true, kAt150, "2147483647x1080", "the largest int32 is still a size");
}

void CheckRepaired(std::optional<std::string_view> line, int32_t booted_width, int32_t booted_height,
                   const char* want, const char* what) {
  const auto [got_w, got_h] = RepairedResolution(line, booted_width, booted_height);
  const std::string got = got_w.empty() && got_h.empty() ? std::string() : got_w + "x" + got_h;
  Check(got == want, what, got.empty() ? std::string("nothing") : got);
}

// A save with no size to write: a [Display] resolution line the boot rejected gets the size the window booted at
// (a fresh install's 1920x1080 when that is unknown); a line the boot used, or no line, is left alone.
void DamagedLineRepaired() {
  CheckRepaired("0x0", 1600, 900, "1600x900", "0x0 becomes the size the window booted at");
  CheckRepaired("0x0", 0, 0, "1920x1080", "0x0 with no booted size becomes a fresh install's 1920x1080");
  CheckRepaired("-5x1080", 1600, 900, "1600x900", "-5x1080 is repaired");
  CheckRepaired("big", 1600, 900, "1600x900", "garbage is repaired");
  CheckRepaired("", 1600, 900, "1600x900", "an empty value is repaired");
  CheckRepaired("1920x1080", 1600, 900, "", "a line the boot used is kept");
  CheckRepaired(" 1920 X 1080 ", 1600, 900, "", "spaces and an upper-case X are a size the boot uses");
  CheckRepaired(std::nullopt, 1600, 900, "", "no line: nothing is added");
}

}  // namespace

int main() {
  Unchanged();
  PickEqualToTheBootLogicalSize();
  OtherPicks();
  NoBootLine();
  AtDpi();
  AbsentCvars();
  ZeroSize();
  SizeTheBootRejects();
  DamagedLineRepaired();
  std::printf("ini_resolution_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
