// Standalone test of src/dev/capture_series.h (pure std, no SDK): the names of
// the F9 frame captures written next to futari_renut_capture_path, and of
// src/renut_engine/gpu/backend/capture_math.h: the conversions of a 16-bit
// float source capture. Runs in CI
// (Guards, "capture-series-test") on g++, and locally with any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/capture_series_test.cpp -o capture_series_test && ./capture_series_test
//   cl /std:c++20 /EHsc /W4 /I src tests\capture_series_test.cpp && capture_series_test.exe
//
// A capture is <stem>_NNN_<W>x<H>.ppm (at least three digits), and the series
// continues past the highest number already in the folder, so nothing is
// overwritten.
#include "dev/capture_series.h"
#include "renut_engine/gpu/backend/capture_math.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <optional>
#include <string>
#include <vector>

namespace {

using futari::dev::CaptureFileName;
using futari::dev::CaptureNumber;
using futari::dev::CaptureOutputFileName;
using futari::dev::CaptureSourceFileName;
using futari::dev::CaptureSourceFloatFileName;
using futari::dev::FreeCaptureNumber;
using futari::dev::NextCaptureNumber;
using futari::dev::PfmFile;
using renut::gpu::capture_math::HalfToFloat;
using renut::gpu::capture_math::UnitToByte;

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
  Check(CaptureOutputFileName("f", 7, 2560, 1440) == "f_007_2560x1440_output.ppm",
        "the presented picture: same number, its own size, _output", CaptureOutputFileName("f", 7, 2560, 1440));
  Check(!CaptureNumber("f", CaptureOutputFileName("f", 7, 2560, 1440)),
        "an output capture is not a number of the frame series", "");
  Check(CaptureSourceFileName("f", 7, 320, 240) == "f_007_source_320x240.ppm",
        "the blit's source image: same number, its own size, source_", CaptureSourceFileName("f", 7, 320, 240));
  Check(CaptureSourceFileName("frame", 1234, 640, 512) == "frame_1234_source_640x512.ppm",
        "the source image past 999", CaptureSourceFileName("frame", 1234, 640, 512));
  Check(!CaptureNumber("f", CaptureSourceFileName("f", 7, 320, 240)),
        "a source capture is not a number of the frame series", "");
  Check(CaptureSourceFloatFileName("f", 7, 320, 240) == "f_007_source_320x240.pfm",
        "the float source image: the source's name as .pfm", CaptureSourceFloatFileName("f", 7, 320, 240));
  Check(!CaptureNumber("f", CaptureSourceFloatFileName("f", 7, 320, 240)),
        "a float source capture is not a number of the frame series", "");
}

std::string Text(float v) { return std::to_string(v); }

// The fp16 source's values as written to the .pfm: IEEE half to float, exact.
void Halves() {
  const auto is = [](uint16_t h, float want, const char* what) {
    Check(HalfToFloat(h) == want && std::signbit(HalfToFloat(h)) == std::signbit(want), what, Text(HalfToFloat(h)));
  };
  is(0x0000, 0.0f, "zero");
  is(0x8000, -0.0f, "negative zero keeps its sign");
  is(0x3C00, 1.0f, "one");
  is(0x3800, 0.5f, "a half");
  is(0xC000, -2.0f, "minus two");
  is(0x3555, 0.333251953125f, "a third as a half holds it");
  is(0x7BFF, 65504.0f, "the largest finite half");
  is(0x0400, 6.103515625e-05f, "the smallest normal");
  is(0x0001, 5.9604644775390625e-08f, "the smallest subnormal");
  is(0x03FF, 6.0975551605224609375e-05f, "the largest subnormal");
  is(0x7C00, std::numeric_limits<float>::infinity(), "infinity");
  is(0xFC00, -std::numeric_limits<float>::infinity(), "minus infinity");
  Check(std::isnan(HalfToFloat(0x7E00)), "NaN stays NaN", Text(HalfToFloat(0x7E00)));
}

// The 8-bit view of a float source: clamped to [0, 1], rounded to the nearest of 255 steps.
void Bytes() {
  const auto is = [](float v, int want, const char* what) {
    Check(UnitToByte(v) == want, what, std::to_string(int(UnitToByte(v))));
  };
  is(0.0f, 0, "zero");
  is(1.0f, 255, "one");
  is(-0.25f, 0, "below zero clamps");
  is(1.5f, 255, "above one clamps");
  is(0.5f, 128, "127.5 rounds away from zero");
  is(127.0f / 255.0f, 127, "a byte value comes back");
  is(0.49f / 255.0f, 0, "below half a step");
  is(0.51f / 255.0f, 1, "above half a step");
  is(std::numeric_limits<float>::quiet_NaN(), 0, "NaN is black");
}

uint32_t Le32(const std::string& s, size_t at) {
  return uint32_t(uint8_t(s[at])) | uint32_t(uint8_t(s[at + 1])) << 8 | uint32_t(uint8_t(s[at + 2])) << 16 |
         uint32_t(uint8_t(s[at + 3])) << 24;
}

// A colour PFM: "PF", the size, a negative scale (little-endian), then float R G B rows from the bottom up.
void PfmFiles() {
  // 2x2, top row (1, 2) then bottom row (3, 4), each texel's R G B = n, n + 0.25, n + 0.5.
  std::vector<float> rgb;
  for (float n : {1.0f, 2.0f, 3.0f, 4.0f}) {
    rgb.insert(rgb.end(), {n, n + 0.25f, n + 0.5f});
  }
  const std::string file = PfmFile(2, 2, rgb);
  const std::string header = "PF\n2 2\n-1.0\n";
  Check(file.size() == header.size() + 2 * 2 * 3 * 4, "header and 12 floats", std::to_string(file.size()));
  Check(file.compare(0, header.size(), header) == 0, "colour, size, little-endian scale", file.substr(0, 12));
  const auto at = [&](size_t i) { return std::bit_cast<float>(Le32(file, header.size() + i * 4)); };
  Check(at(0) == 3.0f && at(1) == 3.25f && at(2) == 3.5f, "the bottom row comes first", Text(at(0)));
  Check(at(3) == 4.0f && at(5) == 4.5f, "left to right within a row", Text(at(3)));
  Check(at(6) == 1.0f && at(11) == 2.5f, "the top row comes last", Text(at(6)));
  Check(PfmFile(3, 1, {}).empty(), "too few values: nothing", "");
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
  Halves();
  Bytes();
  PfmFiles();
  Numbers();
  CaseVariants();
  FreeNumbers();
  Next();
  std::printf("capture_series_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
