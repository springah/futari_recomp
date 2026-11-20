// Standalone test of src/dev/input_recording.h (pure std, no SDK): the text
// format of futari_dev_input_record / futari_dev_input_replay and the replay
// cursor. Runs in CI (Guards, "input-recording-test") on g++, and locally with
// any C++20 compiler:
//
//   g++ -std=c++20 -Wall -Wextra -I src tests/input_recording_test.cpp -o input_recording_test && ./input_recording_test
//   cl /std:c++20 /EHsc /W4 /I src tests\input_recording_test.cpp && input_recording_test.exe
//
// One line per change: "<frames since the previous line> <state | capture |
// window event>", frames counted in guest Swaps, at most 2^32 per line; '#'
// comments, blank lines and a UTF-8 BOM at the start are ignored.
#include "dev/input_recording.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using futari::dev::FormatCaptureLine;
using futari::dev::FormatLine;
using futari::dev::FormatWindowLine;
using futari::dev::FramesSince;
using futari::dev::PadState;
using futari::dev::ParseRecording;
using futari::dev::ParseWindowEvent;
using futari::dev::Quantise;
using futari::dev::ReplayCursor;
using futari::dev::WindowEvent;

int g_failed = 0;
int g_checks = 0;

void Check(bool ok, const char* what, const std::string& got) {
  ++g_checks;
  if (ok)
    return;
  ++g_failed;
  std::printf("FAIL: %s\n---- got ----\n%s\n---- end ----\n", what, got.c_str());
}

std::string Error(const futari::dev::ParsedRecording& parsed) {
  return parsed.error ? std::to_string(parsed.error->line) + ": " + parsed.error->reason : std::string("no error");
}

PadState Pad(uint16_t buttons) {
  PadState pad;
  pad.buttons = buttons;
  return pad;
}

void RoundTrip() {
  PadState all;
  all.buttons = 0xF3FF;  // every named button
  all.lt = 255;
  all.rt = 7;
  all.lx = -32768;
  all.ly = 32767;
  all.rx = 1;
  all.ry = -2;
  const std::string text = FormatLine(12, all) + "\n" + FormatLine(0, PadState{}) + "\n" + FormatCaptureLine(3) + "\n";
  Check(FormatLine(12, all) ==
            "12 a b x y start back up down left right lb rb ls rs lx=-32768 ly=32767 rx=1 ry=-2 lt=255 rt=7",
        "every button and axis by name", FormatLine(12, all));
  Check(FormatLine(5, PadState{}) == "5 -", "nothing held is '-'", FormatLine(5, PadState{}));
  Check(FormatCaptureLine(60) == "60 capture", "a capture line", FormatCaptureLine(60));
  const auto parsed = ParseRecording(text);
  Check(!parsed.error && parsed.lines.size() == 3, "the three lines parse", Error(parsed));
  if (parsed.lines.size() == 3) {
    Check(parsed.lines[0].frames == 12 && parsed.lines[0].state == all && !parsed.lines[0].capture, "state back",
          parsed.lines[0].text);
    Check(parsed.lines[1].state == PadState{} && !parsed.lines[1].capture, "'-' back", parsed.lines[1].text);
    Check(parsed.lines[2].capture && parsed.lines[2].frames == 3, "capture back", parsed.lines[2].text);
    Check(parsed.lines[0].text == FormatLine(12, all), "the line's text", parsed.lines[0].text);
  }
  Check(parsed.captures == 1, "captures counted", std::to_string(parsed.captures));
}

void HandEdits() {
  const auto parsed = ParseRecording("# recorded run\r\n\r\n  30   start  \r\n# wait for the load\r\n4 -\r\n10 a\tdown\r\n");
  Check(!parsed.error && parsed.lines.size() == 3, "comments, blank lines, CRLF, extra spaces and tabs", Error(parsed));
  if (parsed.lines.size() == 3) {
    Check(parsed.lines[0].frames == 30 && parsed.lines[0].state == Pad(0x0010), "start after 30", parsed.lines[0].text);
    Check(parsed.lines[0].source_line == 3, "the file's line number is kept", std::to_string(parsed.lines[0].source_line));
    Check(parsed.lines[2].state == Pad(0x1002), "two buttons on one line", parsed.lines[2].text);
  }
}

void BadInput() {
  const struct {
    const char* text;
    uint32_t line;
    const char* what;
  } cases[] = {
      {"", 0, "an empty file"},
      {"# only a comment\n\n", 0, "no input lines"},
      {"5 a\nx a\n", 2, "frames not a number"},
      {"-1 a\n", 1, "negative frames"},
      {"5\n", 1, "frames without a state"},
      {"5 a jump\n", 1, "an unknown button"},
      {"5 a\n\n5 lx=40000\n", 3, "an axis past 16 bits"},
      {"5 lt=256\n", 1, "a trigger past 255"},
      {"5 lx=\n", 1, "an axis without a value"},
      {"5 lx=1x\n", 1, "an axis with junk after the number"},
      {"5 - a\n", 1, "'-' with a button"},
      {"5 capture a\n", 1, "capture with a button"},
  };
  for (const auto& c : cases) {
    const auto parsed = ParseRecording(c.text);
    Check(parsed.error && parsed.error->line == c.line && !parsed.error->reason.empty() && parsed.lines.empty(), c.what,
          Error(parsed));
  }
}

// A frame count past 2^32 Swaps (over two years at 60 Hz) is a typo: a parse error naming its line, never a replay
// that waits forever or wraps and applies the rest at once.
void FrameCountBound() {
  const auto edge = ParseRecording("4294967296 a\n");
  Check(!edge.error && edge.lines.size() == 1 && edge.lines[0].frames == 4294967296u, "2^32 frames are accepted",
        Error(edge));
  const struct {
    const char* text;
    uint32_t line;
    const char* what;
  } cases[] = {
      {"4294967297 a\n", 1, "one frame past 2^32"},
      {"99999999999999999999 a\n", 1, "frames past 64 bits"},
      {"10 a\n18446744073709551610 capture\n5 -\n", 2, "a capture ~2^64 frames on, a line after it"},
      {"1 a\n18446744073709551615 b\n", 2, "the last line at 2^64-1 frames"},
      {"10 a\n18446744073709551610 capture\n", 2, "a capture-only wrap"},
      {"18446744073709551615 a\n", 1, "the first line at 2^64-1 frames"},
      {"18446744073709551615 a\n0 b\n", 1, "2^64-1 frames, a line after it"},
      {"0 a\n1000000000000000000 b\n", 2, "a 10^18 typo"},
  };
  for (const auto& c : cases) {
    const auto parsed = ParseRecording(c.text);
    Check(parsed.error && parsed.error->line == c.line && parsed.error->reason.find("too large") != std::string::npos &&
              parsed.lines.empty(),
          c.what, Error(parsed));
  }
}

futari::dev::RecordingLine Line(uint64_t frames, bool capture, uint16_t buttons) {
  futari::dev::RecordingLine line;
  line.frames = frames;
  line.capture = capture;
  line.state = Pad(buttons);
  return line;
}

// The cursor saturates its due Swap, so lines it is handed directly (not through the parser) never wrap around.
void CursorDueSaturates() {
  const uint64_t max = UINT64_MAX;
  const struct {
    std::vector<futari::dev::RecordingLine> lines;
    uint64_t swap;
    const char* what;
  } early[] = {
      {{Line(10, false, 0x1000), Line(max - 5, true, 0), Line(5, false, 0)}, 10, "a capture ~2^64 Swaps on, at 10"},
      {{Line(10, false, 0x1000), Line(max - 5, true, 0), Line(5, false, 0)}, 100, "the same past the wrap point"},
      {{Line(1, false, 0x1000), Line(max, false, 0x2000)}, 2, "the last line at 2^64-1"},
      {{Line(10, false, 0x1000), Line(max - 5, true, 0)}, 10, "a capture-only wrap"},
  };
  for (const auto& c : early) {
    ReplayCursor cursor(c.lines);
    const auto step = cursor.Advance(c.swap);
    Check(step.applied == std::vector<size_t>{0} && step.captures == 0 && !step.ended && step.state == Pad(0x1000),
          c.what, std::to_string(step.applied.size()) + " applied, " + std::to_string(step.captures) + " captures");
    const auto last = cursor.Advance(max);
    Check(last.ended && cursor.applied() == c.lines.size(), "the rest is due at the largest Swap",
          std::to_string(cursor.applied()));
  }
  ReplayCursor blocked({Line(max, false, 0x1000), Line(0, false, 0x2000)});
  Check(blocked.Advance(1000000000).applied.empty(), "2^64-1 frames are not due early", "");
  const auto end = blocked.Advance(max);
  Check(end.ended && end.state == Pad(0x2000), "2^64-1 frames, then a line at once", FormatLine(0, end.state));
}

void ByteOrderMark() {
  const auto parsed = ParseRecording("\xEF\xBB\xBF" "5 a\r\n");
  Check(!parsed.error && parsed.lines.size() == 1 && parsed.lines[0].frames == 5 && parsed.lines[0].state == Pad(0x1000),
        "a UTF-8 BOM at the start is skipped", Error(parsed));
  const auto only = ParseRecording("\xEF\xBB\xBF");
  Check(only.error && only.error->line == 0, "a BOM alone has no input lines", Error(only));
}

void FramesBetweenLines() {
  Check(FramesSince(4, 10) == 6, "frames since the last line", std::to_string(FramesSince(4, 10)));
  Check(FramesSince(10, 10) == 0, "the same Swap: 0", std::to_string(FramesSince(10, 10)));
  Check(FramesSince(10, 4) == 0, "a Swap behind the last line: 0, not a wrap", std::to_string(FramesSince(10, 4)));
}

void Quantisation() {
  PadState raw;
  raw.lx = 16383;    // under half: centre
  raw.ly = 16384;    // half: full
  raw.rx = -16384;   // half the other way: full
  raw.ry = -16383;   // under half: centre
  raw.lt = 127;      // under half: off
  raw.rt = 128;      // half: on
  raw.buttons = 0x1001;
  const PadState q = Quantise(raw);
  Check(q.lx == 0 && q.ly == 32767 && q.rx == -32768 && q.ry == 0, "sticks to -32768/0/32767 at half travel",
        FormatLine(0, q));
  Check(q.lt == 0 && q.rt == 255 && q.buttons == 0x1001, "triggers to 0/255, buttons kept", FormatLine(0, q));
  PadState jitter;
  jitter.lx = 900;  // a resting stick's noise
  Check(Quantise(jitter) == PadState{}, "stick jitter quantises to nothing", FormatLine(0, Quantise(jitter)));
}

void Cursor() {
  // start at 10, capture at 15, a+down at 15 too, '-' at 40 (the end).
  const auto parsed = ParseRecording("10 start\n5 capture\n0 a down\n25 -\n");
  Check(!parsed.error && parsed.lines.size() == 4, "the cursor's file parses", Error(parsed));
  ReplayCursor cursor(parsed.lines);
  auto step = cursor.Advance(9);
  Check(step.state == PadState{} && step.applied.empty() && step.captures == 0 && !step.ended,
        "nothing held before the first line", FormatLine(0, step.state));
  step = cursor.Advance(10);
  Check(step.state == Pad(0x0010) && step.applied == std::vector<size_t>{0}, "the first line at its frame",
        FormatLine(0, step.state));
  step = cursor.Advance(12);
  Check(step.state == Pad(0x0010) && step.applied.empty(), "a state holds between lines", FormatLine(0, step.state));
  step = cursor.Advance(30);  // the Swaps jumped past 15
  Check(step.state == Pad(0x1002) && step.applied == (std::vector<size_t>{1, 2}) && step.captures == 1 && !step.ended,
        "every line due by a jump applies, the capture once", FormatLine(0, step.state));
  step = cursor.Advance(31);
  Check(step.captures == 0 && step.applied.empty(), "the capture is not reported again", std::to_string(step.captures));
  Check(cursor.applied() == 3 && cursor.size() == 4, "three of four lines applied", std::to_string(cursor.applied()));
  step = cursor.Advance(40);
  Check(step.state == PadState{} && step.ended, "the last line ends the replay", FormatLine(0, step.state));
  step = cursor.Advance(50);
  Check(!step.ended && step.state == PadState{} && step.applied.empty(), "the end is reported once",
        FormatLine(0, step.state));
}

WindowEvent Size(uint32_t width, uint32_t height) {
  WindowEvent event;
  event.width = width;
  event.height = height;
  return event;
}

WindowEvent Event(WindowEvent::Kind kind, bool on = false) {
  WindowEvent event;
  event.kind = kind;
  event.on = on;
  return event;
}

// Window lines: "<frames> window <W>x<H>", "<frames> fullscreen on|off", "<frames> minimise", "<frames> restore".
void WindowLines() {
  const WindowEvent events[] = {Size(1600, 900), Event(WindowEvent::Kind::kFullscreen, true),
                                Event(WindowEvent::Kind::kFullscreen, false), Event(WindowEvent::Kind::kMinimise),
                                Event(WindowEvent::Kind::kRestore), Size(1, 65535)};
  const char* const texts[] = {"7 window 1600x900", "7 fullscreen on", "7 fullscreen off",
                               "7 minimise",        "7 restore",       "7 window 1x65535"};
  std::string file;
  for (size_t i = 0; i < std::size(events); ++i) {
    Check(FormatWindowLine(7, events[i]) == texts[i], texts[i], FormatWindowLine(7, events[i]));
    file += FormatWindowLine(7, events[i]) + "\n";
  }
  const auto parsed = ParseRecording("3 a\n" + file + "  2   window\t1280x720 \r\n");
  Check(!parsed.error && parsed.lines.size() == std::size(events) + 2, "the window lines parse", Error(parsed));
  if (parsed.lines.size() == std::size(events) + 2) {
    for (size_t i = 0; i < std::size(events); ++i) {
      const auto& line = parsed.lines[i + 1];
      Check(line.window && *line.window == events[i] && !line.capture && line.frames == 7 && line.text == texts[i],
            texts[i], line.text);
    }
    Check(parsed.lines.back().window && *parsed.lines.back().window == Size(1280, 720) &&
              parsed.lines.back().text == "2 window 1280x720",
          "extra spaces, a tab and CRLF", parsed.lines.back().text);
    Check(!parsed.lines[0].window, "a state line has no window event", parsed.lines[0].text);
  }
  Check(parsed.captures == 0, "window lines are not captures", std::to_string(parsed.captures));
}

void BadWindowLines() {
  const struct {
    const char* text;
    uint32_t line;
    const char* what;
  } cases[] = {
      {"5 a\n5 window 0x900\n", 2, "a zero width"},
      {"5 window 1600x0\n", 1, "a zero height"},
      {"5 window 1600900\n", 1, "no 'x'"},
      {"5 window 1600x\n", 1, "no height"},
      {"5 window x900\n", 1, "no width"},
      {"5 window 16a0x900\n", 1, "a non-numeric width"},
      {"5 window 1600x9o0\n", 1, "a non-numeric height"},
      {"5 window -1600x900\n", 1, "a negative width"},
      {"5 window 1600X900\n", 1, "a capital X"},
      {"5 window 1600x900x2\n", 1, "two 'x'"},
      {"5 window 65536x900\n", 1, "a width past 65535"},
      {"5 window 1600x99999999999\n", 1, "a height past 32 bits"},
      {"5 window\n", 1, "window without a size"},
      {"5 window 1600x900 a\n", 1, "window with an extra token"},
      {"5 fullscreen\n", 1, "fullscreen without on/off"},
      {"5 fullscreen yes\n", 1, "an unknown fullscreen argument"},
      {"5 fullscreen ON\n", 1, "a capital ON"},
      {"5 fullscreen on off\n", 1, "fullscreen with an extra token"},
      {"5 minimise 1\n", 1, "minimise with an extra token"},
      {"5 restore now\n", 1, "restore with an extra token"},
      {"5 minimize\n", 1, "the other spelling is unknown"},
  };
  for (const auto& c : cases) {
    const auto parsed = ParseRecording(c.text);
    Check(parsed.error && parsed.error->line == c.line && !parsed.error->reason.empty() && parsed.lines.empty(), c.what,
          Error(parsed));
  }
}

// Window lines change no pad state, like capture; the cursor reports each due one once, in file order with captures,
// also when the Swaps jump past several lines.
void CursorWindowEvents() {
  const auto parsed =
      ParseRecording("10 a\n5 window 1600x900\n0 capture\n0 fullscreen on\n5 b\n5 minimise\n0 restore\n10 capture\n"
                     "5 fullscreen off\n60 -\n");
  Check(!parsed.error && parsed.lines.size() == 10, "the cursor's window file parses", Error(parsed));
  if (parsed.lines.size() != 10) {
    return;
  }
  ReplayCursor cursor(parsed.lines);
  auto step = cursor.Advance(10);
  Check(step.state == Pad(0x1000) && step.events.empty(), "a state line is no event", FormatLine(0, step.state));
  step = cursor.Advance(14);
  Check(step.events.empty() && step.applied.empty(), "nothing due before 15", std::to_string(step.events.size()));
  step = cursor.Advance(15);
  Check(step.state == Pad(0x1000) && step.events == (std::vector<size_t>{1, 2, 3}) && step.captures == 1,
        "window, capture, fullscreen in file order; the pad unchanged", FormatLine(0, step.state));
  step = cursor.Advance(16);
  Check(step.events.empty() && step.captures == 0, "the events are not reported again",
        std::to_string(step.events.size()));
  step = cursor.Advance(40);  // the Swaps jumped past 20, 25 and 35
  Check(step.state == Pad(0x2000) && step.applied == (std::vector<size_t>{4, 5, 6, 7, 8}) &&
            step.events == (std::vector<size_t>{5, 6, 7, 8}) && step.captures == 1 && !step.ended,
        "a jump reports every due event once, in order, interleaved with the capture", FormatLine(0, step.state));
  const bool kinds = cursor.line(5).window->kind == WindowEvent::Kind::kMinimise &&
                     cursor.line(6).window->kind == WindowEvent::Kind::kRestore && cursor.line(7).capture &&
                     cursor.line(8).window->kind == WindowEvent::Kind::kFullscreen && !cursor.line(8).window->on;
  Check(kinds, "the events' lines are the file's", cursor.line(8).text);
  step = cursor.Advance(100);
  Check(step.ended && step.events.empty() && step.state == PadState{}, "the end, no more events",
        std::to_string(step.events.size()));
}

// No words after the frames is an error, not a read past the list.
void WindowEventWithoutWords() {
  std::string error;
  const auto event = ParseWindowEvent({}, error);
  Check(!event && !error.empty(), "an empty word list is an error", error);
}

}  // namespace

int main() {
  RoundTrip();
  HandEdits();
  BadInput();
  Quantisation();
  Cursor();
  FrameCountBound();
  CursorDueSaturates();
  ByteOrderMark();
  FramesBetweenLines();
  WindowLines();
  BadWindowLines();
  CursorWindowEvents();
  WindowEventWithoutWords();
  std::printf("input_recording_test: %d checks, %d failed\n", g_checks, g_failed);
  return g_failed == 0 ? 0 : 1;
}
