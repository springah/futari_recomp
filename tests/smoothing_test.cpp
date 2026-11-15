// Standalone test of src/smoothing_math.h (pure std, no SDK): the game's Smoothing modes and the dev value that
// forces one. Locally with any C++20 compiler:
//
//   clang++ -std=c++20 -Wall -Wextra -I src tests/smoothing_test.cpp -o smoothing_test
//
// The game draws its gameplay image with Smoothing 0 (Off) or 1 (On); 4 is Sharp bilinear, which the game draws as
// On and the renderer then redraws sharp. futari_dev_smoothing forces one of the three for captures. The Picture
// page's Smoothing item shows them as Smooth / Sharp / Off through a menu index.
#include "smoothing_math.h"

#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>

namespace {

using futari::smoothing::BlitDrawMode;
using futari::smoothing::FindSmoothingLabels;
using futari::smoothing::kSmoothingOff;
using futari::smoothing::kSmoothingOn;
using futari::smoothing::kSmoothingSharp;
using futari::smoothing::ParseDevSmoothing;
using futari::smoothing::ResetSmoothing;
using futari::smoothing::SmoothingFallbackIndex;
using futari::smoothing::SmoothingIndex;
using futari::smoothing::SmoothingLabel;
using futari::smoothing::SmoothingLabelSet;
using futari::smoothing::SmoothingStore;
using futari::smoothing::SmoothingWord;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n", what);
}

// Sharp draws as On (the guest's linear filter and On shader pair); every other value is the game's own.
void SharpDrawsAsOn() {
  Check(BlitDrawMode(kSmoothingSharp) == kSmoothingOn, "Sharp draws as On");
  Check(BlitDrawMode(kSmoothingOn) == kSmoothingOn, "On stays On");
  Check(BlitDrawMode(kSmoothingOff) == kSmoothingOff, "Off stays Off");
  Check(BlitDrawMode(2) == 2, "the game's mode 2 stays");
  Check(BlitDrawMode(3) == 3, "the game's mode 3 stays");
  Check(BlitDrawMode(5) == 5, "an unknown value stays");
}

void CheckDevSmoothing(const char* text, std::optional<uint32_t> want, const char* what) {
  Check(ParseDevSmoothing(text) == want, what);
}

// The dev value: 0, 1 or 4, spaces around it allowed; else nothing.
void ParseDevSmoothingValue() {
  CheckDevSmoothing("0", kSmoothingOff, "0 is Off");
  CheckDevSmoothing("1", kSmoothingOn, "1 is On");
  CheckDevSmoothing("4", kSmoothingSharp, "4 is Sharp");
  CheckDevSmoothing(" 4\t", kSmoothingSharp, "spaces around the number");
  CheckDevSmoothing("2", std::nullopt, "2 is nothing");
  CheckDevSmoothing("3", std::nullopt, "3 is nothing");
  CheckDevSmoothing("5", std::nullopt, "5 is nothing");
  CheckDevSmoothing("-1", std::nullopt, "a negative number is nothing");
  CheckDevSmoothing("", std::nullopt, "empty is nothing");
  CheckDevSmoothing("  ", std::nullopt, "only spaces is nothing");
  CheckDevSmoothing("+4", std::nullopt, "a plus sign is nothing");
  CheckDevSmoothing("4x", std::nullopt, "trailing garbage is nothing");
  CheckDevSmoothing("sharp", std::nullopt, "a word is nothing");
  CheckDevSmoothing("4294967300", std::nullopt, "past 32 bits is nothing");
}

// The menu item's indices: Smooth 0, Sharp 1, Off 2, each the index of its word and back.
void SmoothingIndexRoundTrip() {
  Check(SmoothingIndex(kSmoothingOn) == 0, "On shows as Smooth (index 0)");
  Check(SmoothingIndex(kSmoothingSharp) == 1, "Sharp shows as index 1");
  Check(SmoothingIndex(kSmoothingOff) == 2, "Off shows as index 2");
  Check(SmoothingWord(0) == kSmoothingOn, "index 0 stores On");
  Check(SmoothingWord(1) == kSmoothingSharp, "index 1 stores Sharp");
  Check(SmoothingWord(2) == kSmoothingOff, "index 2 stores Off");
  for (uint32_t index = 0; index < 3; ++index) {
    Check(SmoothingIndex(SmoothingWord(index)) == index, "index -> word -> the same index");
  }
}

// The handler's store: the stored word stays while it already shows as the index (the store the game makes without
// input when the cursor first reaches the row after a load), else the index's own word.
void SmoothingStoreKeeps() {
  Check(SmoothingStore(0, 2) == 2, "first-frame store keeps a stored 2");
  Check(SmoothingStore(0, 3) == 3, "first-frame store keeps a stored 3");
  Check(SmoothingStore(0, kSmoothingOn) == kSmoothingOn, "first-frame store keeps On");
  Check(SmoothingStore(1, kSmoothingSharp) == kSmoothingSharp, "first-frame store keeps Sharp");
  Check(SmoothingStore(2, kSmoothingOff) == kSmoothingOff, "first-frame store keeps Off");
  Check(SmoothingStore(2, 7) == 7, "first-frame store keeps an unknown word shown as Off");
  Check(SmoothingStore(1, 2) == kSmoothingSharp, "moving from a stored 2 to Sharp stores 4");
  Check(SmoothingStore(2, 3) == kSmoothingOff, "moving from a stored 3 to Off stores 0");
  Check(SmoothingStore(0, kSmoothingSharp) == kSmoothingOn, "moving from Sharp to Smooth stores 1");
}

// The game's row step for a list item: right +1, left -1, a value outside the labels reverts (clamps); 2 is the last
// index of the three labels.
uint32_t GameStep(uint32_t index, int delta) {
  const int next = static_cast<int>(index) + delta;
  return next < 0 || next > 2 ? index : static_cast<uint32_t>(next);
}

// Right goes Smooth -> Sharp -> Off, left the reverse; left at Smooth and right at Off stay.
void SmoothingRightStepOrder() {
  Check(SmoothingWord(GameStep(SmoothingIndex(kSmoothingOn), 1)) == kSmoothingSharp, "right from Smooth is Sharp");
  Check(SmoothingWord(GameStep(SmoothingIndex(kSmoothingSharp), 1)) == kSmoothingOff, "right from Sharp is Off");
  Check(SmoothingWord(GameStep(SmoothingIndex(kSmoothingOff), 1)) == kSmoothingOff, "right at Off stays Off");
  Check(SmoothingWord(GameStep(SmoothingIndex(kSmoothingOff), -1)) == kSmoothingSharp, "left from Off is Sharp");
  Check(SmoothingWord(GameStep(SmoothingIndex(kSmoothingSharp), -1)) == kSmoothingOn, "left from Sharp is Smooth");
  Check(SmoothingWord(GameStep(SmoothingIndex(kSmoothingOn), -1)) == kSmoothingOn, "left at Smooth stays Smooth");
}

// The game's unused linear modes show as Smooth; any other word shows as Off.
void SmoothingUnknownWords() {
  Check(SmoothingIndex(2) == 0, "2 shows as Smooth");
  Check(SmoothingIndex(3) == 0, "3 shows as Smooth");
  Check(SmoothingIndex(5) == 2, "5 shows as Off");
  Check(SmoothingIndex(0xFFFFFFFFu) == 2, "0xFFFFFFFF shows as Off");
  Check(SmoothingWord(3) == kSmoothingOff, "index 3 stores Off");
  Check(SmoothingWord(0xFFFFFFFFu) == kSmoothingOff, "index 0xFFFFFFFF stores Off");
}

// The game's Smoothing default: Sharp with Renut, On (Smooth) otherwise.
void ResetSmoothingPerRenderer() {
  Check(ResetSmoothing(true) == kSmoothingSharp, "Renut resets to Sharp");
  Check(ResetSmoothing(false) == kSmoothingOn, "other renderers reset to On");
}

// The labels fit the game's short-string buffer (15 bytes) and use only ASCII; Off keeps the game's own string.
void SmoothingLabels() {
  Check(SmoothingLabel(0) == std::optional<std::string_view>("Smooth"), "Smooth is labelled Smooth");
  Check(SmoothingLabel(1) == std::optional<std::string_view>("Sharp"), "Sharp is labelled Sharp");
  Check(!SmoothingLabel(2).has_value(), "Off keeps the game's own label");
  for (uint32_t index = 0; index < 2; ++index) {
    const std::string_view label = SmoothingLabel(index).value_or("");
    Check(!label.empty() && label.size() <= 15, "label fits the short-string buffer");
    bool ascii = true;
    for (const char c : label) {
      ascii = ascii && static_cast<unsigned char>(c) >= 0x20 && static_cast<unsigned char>(c) < 0x7F;
    }
    Check(ascii, "label is printable ASCII");
  }
}

// The item's labels as the item load finds them: the game's two, our three (a reload: its first two are our texts),
// or anything else, which keeps the game's item untouched. Elements must be short strings with no nested labels.
void SmoothingLabelsFoundOnLoad() {
  Check(FindSmoothingLabels(2, true, "Off", "On") == SmoothingLabelSet::kGames, "two plain labels are the game's");
  Check(FindSmoothingLabels(3, true, "Smooth", "Sharp") == SmoothingLabelSet::kOurs, "three plain labels are ours");
  Check(FindSmoothingLabels(2, false, "Off", "On") == SmoothingLabelSet::kUnexpected,
        "a heap string or nested labels: unexpected");
  Check(FindSmoothingLabels(3, false, "Smooth", "Sharp") == SmoothingLabelSet::kUnexpected,
        "three labels, not plain: unexpected");
  Check(FindSmoothingLabels(0, true, "", "") == SmoothingLabelSet::kUnexpected, "no labels: unexpected");
  Check(FindSmoothingLabels(1, true, "Off", "") == SmoothingLabelSet::kUnexpected, "one label: unexpected");
  Check(FindSmoothingLabels(4, true, "Smooth", "Sharp") == SmoothingLabelSet::kUnexpected, "four labels: unexpected");
}

// Three plain labels that are not our texts are someone else's list: unexpected, not ours.
void SmoothingThreeForeignLabels() {
  Check(FindSmoothingLabels(3, true, "Off", "On") == SmoothingLabelSet::kUnexpected, "three labels, the game's texts");
  Check(FindSmoothingLabels(3, true, "Low", "Mid") == SmoothingLabelSet::kUnexpected, "three labels, other texts");
  Check(FindSmoothingLabels(3, true, "Smooth", "On") == SmoothingLabelSet::kUnexpected, "only the first is ours");
  Check(FindSmoothingLabels(3, true, "Off", "Sharp") == SmoothingLabelSet::kUnexpected, "only the second is ours");
  Check(FindSmoothingLabels(3, true, "smooth", "sharp") == SmoothingLabelSet::kUnexpected, "texts compare exactly");
}

// Two plain labels are the game's only when they read exactly Off/On; other texts (a localised build, another list)
// are unexpected and keep the item untouched.
void SmoothingTwoForeignLabels() {
  Check(FindSmoothingLabels(2, true, "\xE3\x82\xAA\xE3\x83\x95", "\xE3\x82\xAA\xE3\x83\xB3") ==
            SmoothingLabelSet::kUnexpected,
        "two labels, Japanese texts");
  Check(FindSmoothingLabels(2, true, "On", "Off") == SmoothingLabelSet::kUnexpected, "two labels, swapped");
  Check(FindSmoothingLabels(2, true, "Low", "High") == SmoothingLabelSet::kUnexpected, "two labels, other texts");
  Check(FindSmoothingLabels(2, true, "Off", "Sharp") == SmoothingLabelSet::kUnexpected, "only the first is the game's");
  Check(FindSmoothingLabels(2, true, "Smooth", "On") == SmoothingLabelSet::kUnexpected, "only the second is the game's");
  Check(FindSmoothingLabels(2, true, "off", "on") == SmoothingLabelSet::kUnexpected, "texts compare exactly");
  Check(FindSmoothingLabels(2, true, "", "") == SmoothingLabelSet::kUnexpected, "two empty labels");
}

// The game's own Off/On item keeps a valid label index for any word: Off for 0, On for everything else, and never
// at or past the label count (0 when there are none).
void SmoothingFallbackIndices() {
  Check(SmoothingFallbackIndex(kSmoothingOff, 2) == 0, "Off shows as the game's Off");
  Check(SmoothingFallbackIndex(kSmoothingOn, 2) == 1, "On shows as the game's On");
  Check(SmoothingFallbackIndex(kSmoothingSharp, 2) == 1, "Sharp shows as the game's On");
  Check(SmoothingFallbackIndex(2, 2) == 1, "2 shows as the game's On");
  Check(SmoothingFallbackIndex(0xFFFFFFFFu, 2) == 1, "0xFFFFFFFF shows as the game's On");
  Check(SmoothingFallbackIndex(kSmoothingOn, 3) == 1, "On with three labels shows as label 1");
}

// With fewer than two labels the index stays below the count; with none it is 0.
void SmoothingFallbackBelowTheCount() {
  Check(SmoothingFallbackIndex(kSmoothingOn, 1) == 0, "On with one label: label 0");
  Check(SmoothingFallbackIndex(kSmoothingSharp, 1) == 0, "Sharp with one label: label 0");
  Check(SmoothingFallbackIndex(kSmoothingOff, 1) == 0, "Off with one label: label 0");
  Check(SmoothingFallbackIndex(kSmoothingOn, 0) == 0, "On with no labels: 0");
  Check(SmoothingFallbackIndex(kSmoothingOff, 0) == 0, "Off with no labels: 0");
}

}  // namespace

int main() {
  SharpDrawsAsOn();
  ParseDevSmoothingValue();
  SmoothingIndexRoundTrip();
  SmoothingStoreKeeps();
  SmoothingRightStepOrder();
  SmoothingUnknownWords();
  ResetSmoothingPerRenderer();
  SmoothingLabels();
  SmoothingLabelsFoundOnLoad();
  SmoothingThreeForeignLabels();
  SmoothingTwoForeignLabels();
  SmoothingFallbackIndices();
  SmoothingFallbackBelowTheCount();
  std::printf("smoothing_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
