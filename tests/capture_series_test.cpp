// Standalone test of src/dev/capture_series.h (pure std, no SDK): the names of
// the F9 frame captures written next to futari_renut_capture_path. Runs in CI
// (Guards, "capture-series-test") on g++, and locally with any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/capture_series_test.cpp -o capture_series_test && ./capture_series_test
//   cl /std:c++20 /EHsc /W4 /I src tests\capture_series_test.cpp && capture_series_test.exe
//
// A capture is <stem>_NNN_<W>x<H>.ppm (at least three digits), and the series
// continues past the highest number already in the folder, so nothing is
// overwritten.
#include "dev/capture_series.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace {

using futari::dev::CaptureFileName;
using futari::dev::CaptureNumber;
using futari::dev::FreeCaptureNumber;
using futari::dev::NextCaptureNumber;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s\n---- end ----\n", what, got.c_str());
}

std::string Text(std::optional<uint32_t> n) { return n ? std::to_string(*n) : std::string("nothing"); }

void FileNames() {
  Check(CaptureFileName("f", 1, 1600, 900) == "f_001_1600x900.ppm", "number 1 has three digits",
        CaptureFileName("f", 1, 1600, 900));
  Check(CaptureFileName("frame", 42, 1080, 1920) == "frame_042_1080x1920.ppm", "portrait size in the name",
        CaptureFileName("frame", 42, 1080, 1920));
  Check(CaptureFileName("f", 1234, 3840, 2160) == "f_1234_3840x2160.ppm", "past 999 the number grows",
        CaptureFileName("f", 1234, 3840, 2160));
}

void Numbers() {
  Check(CaptureNumber("f", "f_001_1600x900.ppm") == 1u, "a capture's number",
        Text(CaptureNumber("f", "f_001_1600x900.ppm")));
  Check(CaptureNumber("f", "f_1234_3840x2160.ppm") == 1234u, "four digits",
        Text(CaptureNumber("f", "f_1234_3840x2160.ppm")));
  Check(!CaptureNumber("f", "f.ppm"), "the single capture is not in the series", Text(CaptureNumber("f", "f.ppm")));
  Check(!CaptureNumber("f", "g_001_1600x900.ppm"), "another stem", Text(CaptureNumber("f", "g_001_1600x900.ppm")));
  Check(!CaptureNumber("f", "ff_001_1600x900.ppm"), "a longer stem", Text(CaptureNumber("f", "ff_001_1600x900.ppm")));
  Check(!CaptureNumber("f", "f_01_1600x900.ppm"), "two digits", Text(CaptureNumber("f", "f_01_1600x900.ppm")));
  Check(!CaptureNumber("f", "f_001_1600x900.png"), "another extension", Text(CaptureNumber("f", "f_001_1600x900.png")));
  Check(!CaptureNumber("f", "f_001_1600.ppm"), "a size without x", Text(CaptureNumber("f", "f_001_1600.ppm")));
  Check(!CaptureNumber("f", "f_001.ppm"), "no size", Text(CaptureNumber("f", "f_001.ppm")));
  Check(!CaptureNumber("f", "f_abc_1600x900.ppm"), "not a number", Text(CaptureNumber("f", "f_abc_1600x900.ppm")));
  Check(!CaptureNumber("f", "f_99999999999_1600x900.ppm"), "a number past 32 bits",
        Text(CaptureNumber("f", "f_99999999999_1600x900.ppm")));
}

// Windows file names ignore case: a capture that differs from the series' name only in case is the same file.
void CaseVariants() {
  for (const char* name : {"frame_001_640x480.PPM", "frame_001_640x480.Ppm", "FRAME_001_640x480.ppm",
                           "Frame_001_640x480.ppm", "frame_001_640X480.ppm"}) {
    Check(CaptureNumber("frame", name) == 1u, name, Text(CaptureNumber("frame", name)));
  }
  Check(CaptureNumber("Frame", "frame_002_640x480.ppm") == 2u, "a stem given in capitals",
        Text(CaptureNumber("Frame", "frame_002_640x480.ppm")));
  const std::vector<std::string> names = {"frame_001_640x480.PPM"};
  Check(NextCaptureNumber("frame", names) == 2u, "the series continues past a case variant",
        Text(NextCaptureNumber("frame", names)));
}

// The write never opens an existing file: the first number from the one handed out whose name is free.
void FreeNumbers() {
  const auto taken_in = [](std::vector<uint32_t> taken) {
    return [taken](uint32_t n) { return std::find(taken.begin(), taken.end(), n) != taken.end(); };
  };
  Check(FreeCaptureNumber(1, taken_in({})) == 1u, "a free number is kept", Text(FreeCaptureNumber(1, taken_in({}))));
  Check(FreeCaptureNumber(1, taken_in({1, 2})) == 3u, "taken numbers are skipped",
        Text(FreeCaptureNumber(1, taken_in({1, 2}))));
  Check(FreeCaptureNumber(4, taken_in({1, 2})) == 4u, "only from the number handed out",
        Text(FreeCaptureNumber(4, taken_in({1, 2}))));
  Check(FreeCaptureNumber(UINT32_MAX - 1, taken_in({UINT32_MAX - 1})) == UINT32_MAX, "up to the largest u32",
        Text(FreeCaptureNumber(UINT32_MAX - 1, taken_in({UINT32_MAX - 1}))));
  Check(!FreeCaptureNumber(UINT32_MAX, taken_in({UINT32_MAX})), "nothing past the largest u32",
        Text(FreeCaptureNumber(UINT32_MAX, taken_in({UINT32_MAX}))));
}

void Next() {
  Check(NextCaptureNumber("f", {}) == 1u, "an empty folder starts at 1", Text(NextCaptureNumber("f", {})));
  const std::vector<std::string> names = {"f.ppm", "f_003_1920x1080.ppm", "f_001_1600x900.ppm", "g_007_640x480.ppm",
                                          "notes.txt"};
  Check(NextCaptureNumber("f", names) == 4u, "past the highest of the stem's own", Text(NextCaptureNumber("f", names)));
  Check(NextCaptureNumber("g", names) == 8u, "each stem has its own series", Text(NextCaptureNumber("g", names)));
  const std::vector<std::string> last = {"f_4294967295_640x480.ppm"};
  Check(!NextCaptureNumber("f", last), "no number after the largest u32", Text(NextCaptureNumber("f", last)));
}

}  // namespace

int main() {
  FileNames();
  Numbers();
  CaseVariants();
  FreeNumbers();
  Next();
  std::printf("capture_series_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
