// Dev aid: the text format of futari_dev_input_record / futari_dev_input_replay (src/dev/input_recording_driver.h) and
// the replay cursor, pure std so a test can run them without the SDK (tests/input_recording_test.cpp).
//
// One line per change of user 0's pad: "<frames since the previous line> <state | capture>", frames counted in guest
// Swaps from the start of the process. A state is the held buttons (a b x y start back up down left right lb rb ls rs)
// and any nonzero axis (lx= ly= rx= ry= -32768..32767, lt= rt= 0..255), or "-" for nothing held; it holds until the
// next state line. "capture" takes an F9 frame capture. Window lines (added by hand, never recorded) change the window:
// "window <W>x<H>" (client area in physical pixels, each 1..65535), "fullscreen on|off", "minimise", "restore"; like
// "capture" they change no pad state. '#' starts a comment line; the end of the file ends the run.
// A line counts at most 2^32 frames; a UTF-8 BOM at the start of the file is skipped.
#pragma once

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace futari::dev {

// User 0's pad as the game reads it (XInput button bits).
struct PadState {
  uint16_t buttons = 0;
  uint8_t lt = 0, rt = 0;
  int16_t lx = 0, ly = 0, rx = 0, ry = 0;
  bool operator==(const PadState&) const = default;
};

inline constexpr std::array<std::pair<std::string_view, uint16_t>, 14> kPadButtons = {{
    {"a", 0x1000}, {"b", 0x2000}, {"x", 0x4000}, {"y", 0x8000}, {"start", 0x0010}, {"back", 0x0020},
    {"up", 0x0001}, {"down", 0x0002}, {"left", 0x0004}, {"right", 0x0008}, {"lb", 0x0100}, {"rb", 0x0200},
    {"ls", 0x0040}, {"rs", 0x0080},
}};

// Sticks to -32768/0/32767 and triggers to 0/255 at half travel, so a resting stick's noise writes no lines.
inline PadState Quantise(PadState pad) {
  const auto stick = [](int16_t v) -> int16_t { return v >= 16384 ? 32767 : v <= -16384 ? -32768 : 0; };
  pad.lx = stick(pad.lx);
  pad.ly = stick(pad.ly);
  pad.rx = stick(pad.rx);
  pad.ry = stick(pad.ry);
  pad.lt = pad.lt >= 128 ? 255 : 0;
  pad.rt = pad.rt >= 128 ? 255 : 0;
  return pad;
}

inline std::string FormatLine(uint64_t frames, const PadState& pad) {
  std::string line = std::to_string(frames);
  for (const auto& [name, bit] : kPadButtons) {
    if (pad.buttons & bit) {
      line += " " + std::string(name);
    }
  }
  const std::pair<const char*, int> axes[] = {{"lx", pad.lx}, {"ly", pad.ly}, {"rx", pad.rx},
                                              {"ry", pad.ry}, {"lt", pad.lt}, {"rt", pad.rt}};
  for (const auto& [name, value] : axes) {
    if (value != 0) {
      line += " " + std::string(name) + "=" + std::to_string(value);
    }
  }
  return pad == PadState{} ? line + " -" : line;
}

inline std::string FormatCaptureLine(uint64_t frames) { return std::to_string(frames) + " capture"; }

// A window line's change.
struct WindowEvent {
  enum class Kind : uint8_t { kSize, kFullscreen, kMinimise, kRestore };
  Kind kind = Kind::kSize;
  uint32_t width = 0, height = 0;  // kSize: the client area in physical pixels
  bool on = false;                 // kFullscreen
  bool operator==(const WindowEvent&) const = default;
};

// The largest side a window line may ask for.
inline constexpr uint32_t kMaxWindowSide = 65535;

inline std::string FormatWindowLine(uint64_t frames, const WindowEvent& event) {
  std::string line = std::to_string(frames);
  switch (event.kind) {
    case WindowEvent::Kind::kSize:
      return line + " window " + std::to_string(event.width) + "x" + std::to_string(event.height);
    case WindowEvent::Kind::kFullscreen:
      return line + (event.on ? " fullscreen on" : " fullscreen off");
    case WindowEvent::Kind::kMinimise:
      return line + " minimise";
    case WindowEvent::Kind::kRestore:
      return line + " restore";
  }
  return line;
}

// The event of a window line's words after the frames, nothing when words[0] names no window event, or the reason
// the line is wrong (also no words at all).
inline std::optional<WindowEvent> ParseWindowEvent(const std::vector<std::string_view>& words, std::string& error) {
  if (words.empty()) {
    error = "a state, '-', 'capture' or a window event must follow the frames";
    return std::nullopt;
  }
  WindowEvent event;
  const std::string_view name = words[0];
  if (name == "window") {
    const auto side = [](std::string_view digits, uint32_t& value) {
      const auto [end, ec] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
      return !digits.empty() && ec == std::errc{} && end == digits.data() + digits.size() && value >= 1 &&
             value <= kMaxWindowSide;
    };
    const size_t x = words.size() == 2 ? words[1].find('x') : std::string_view::npos;
    if (x == std::string_view::npos || !side(words[1].substr(0, x), event.width) ||
        !side(words[1].substr(x + 1), event.height)) {
      error = "'window' takes one size <W>x<H>, each 1..65535";
    }
  } else if (name == "fullscreen") {
    event.kind = WindowEvent::Kind::kFullscreen;
    event.on = words.size() == 2 && words[1] == "on";
    if (words.size() != 2 || (!event.on && words[1] != "off")) {
      error = "'fullscreen' takes 'on' or 'off'";
    }
  } else if (name == "minimise" || name == "restore") {
    event.kind = name == "minimise" ? WindowEvent::Kind::kMinimise : WindowEvent::Kind::kRestore;
    if (words.size() != 1) {
      error = "'" + std::string(name) + "' stands alone on its line";
    }
  } else {
    return std::nullopt;
  }
  return event;
}

// The frames a line written at `swap` counts since the line written at `last_line_swap`; never below 0.
constexpr uint64_t FramesSince(uint64_t last_line_swap, uint64_t swap) {
  return swap > last_line_swap ? swap - last_line_swap : 0;
}

// The most frames one line may count (over two years at 60 Hz); more is a typo the replay would wait on forever.
inline constexpr uint64_t kMaxLineFrames = uint64_t{1} << 32;

struct RecordingLine {
  uint64_t frames = 0;  // since the previous line
  bool capture = false;
  std::optional<WindowEvent> window;  // a window line
  PadState state;       // for a state line
  uint32_t source_line = 0;
  std::string text;     // the line as FormatLine / FormatCaptureLine / FormatWindowLine writes it
};

struct ParseError {
  uint32_t line = 0;  // 0: the file as a whole
  std::string reason;
};

struct ParsedRecording {
  std::vector<RecordingLine> lines;
  uint32_t captures = 0;
  std::optional<ParseError> error;  // set: no lines
};

inline ParsedRecording ParseRecording(std::string_view text) {
  ParsedRecording out;
  const auto fail = [&out](uint32_t line, std::string reason) {
    out.lines.clear();
    out.captures = 0;
    out.error = ParseError{line, std::move(reason)};
    return out;
  };
  if (text.starts_with("\xEF\xBB\xBF")) {
    text.remove_prefix(3);  // a UTF-8 BOM, as editors write one
  }
  uint32_t number = 0;
  while (!text.empty()) {
    const size_t end = text.find('\n');
    std::string_view line = text.substr(0, end);
    text.remove_prefix(end == std::string_view::npos ? text.size() : end + 1);
    ++number;
    std::vector<std::string_view> words;
    for (size_t at = 0; at < line.size();) {
      const size_t from = line.find_first_not_of(" \t\r", at);
      if (from == std::string_view::npos) {
        break;
      }
      const size_t to = std::min(line.find_first_of(" \t\r", from), line.size());
      words.push_back(line.substr(from, to - from));
      at = to;
    }
    if (words.empty() || words[0].front() == '#') {
      continue;
    }
    RecordingLine entry;
    entry.source_line = number;
    const auto [frames_end, frames_error] =
        std::from_chars(words[0].data(), words[0].data() + words[0].size(), entry.frames);
    const bool digits_only = frames_end == words[0].data() + words[0].size();
    if (digits_only && (frames_error == std::errc::result_out_of_range || entry.frames > kMaxLineFrames)) {
      return fail(number, "frames too large: '" + std::string(words[0]) + "' (at most 4294967296)");
    }
    if (frames_error != std::errc{} || !digits_only) {
      return fail(number, "frames must be a whole number, not '" + std::string(words[0]) + "'");
    }
    if (words.size() < 2) {
      return fail(number, "a state, '-', 'capture' or a window event must follow the frames");
    }
    std::string window_error;
    entry.window = ParseWindowEvent(std::vector<std::string_view>(words.begin() + 1, words.end()), window_error);
    if (!window_error.empty()) {
      return fail(number, std::move(window_error));
    }
    if (entry.window) {
      entry.text = FormatWindowLine(entry.frames, *entry.window);
      out.lines.push_back(std::move(entry));
      continue;
    }
    if ((words[1] == "-" || words[1] == "capture") && words.size() > 2) {
      return fail(number, "'" + std::string(words[1]) + "' stands alone on its line");
    }
    entry.capture = words[1] == "capture";
    for (size_t i = 1; i < words.size() && !entry.capture && words[1] != "-"; ++i) {
      const std::string_view word = words[i];
      bool known = false;
      for (const auto& [name, bit] : kPadButtons) {
        if (word == name) {
          entry.state.buttons |= bit;
          known = true;
        }
      }
      const size_t eq = word.find('=');
      const std::string_view axis = word.substr(0, eq == std::string_view::npos ? word.size() : eq);
      const bool trigger = axis == "lt" || axis == "rt";
      const bool stick = axis == "lx" || axis == "ly" || axis == "rx" || axis == "ry";
      if (!known && eq != std::string_view::npos && (trigger || stick)) {
        int value = 0;
        const std::string_view digits = word.substr(eq + 1);
        const auto [value_end, value_error] = std::from_chars(digits.data(), digits.data() + digits.size(), value);
        if (digits.empty() || value_error != std::errc{} || value_end != digits.data() + digits.size() ||
            value < (trigger ? 0 : -32768) || value > (trigger ? 255 : 32767)) {
          return fail(number, "'" + std::string(word) + "' is out of range (sticks -32768..32767, triggers 0..255)");
        }
        known = true;
        int16_t* const sticks[] = {&entry.state.lx, &entry.state.ly, &entry.state.rx, &entry.state.ry};
        if (trigger) {
          (axis == "lt" ? entry.state.lt : entry.state.rt) = uint8_t(value);
        } else {
          *sticks[(axis[0] == 'r' ? 2 : 0) + (axis[1] == 'y' ? 1 : 0)] = int16_t(value);
        }
      }
      if (!known) {
        return fail(number, "unknown button or axis '" + std::string(word) + "'");
      }
    }
    entry.text = entry.capture ? FormatCaptureLine(entry.frames) : FormatLine(entry.frames, entry.state);
    out.captures += entry.capture ? 1 : 0;
    out.lines.push_back(std::move(entry));
  }
  if (out.lines.empty()) {
    return fail(0, "no input lines");
  }
  return out;
}

// Replay: which lines are due at a guest Swap count. Every line due by then applies (the Swaps may jump between two
// reads), each capture, window event and the end are reported once.
class ReplayCursor {
 public:
  struct Step {
    PadState state;               // the pad to hand to the game now
    std::vector<size_t> applied;  // the lines applied by this call (indexes)
    uint32_t captures = 0;        // capture lines among them
    std::vector<size_t> events;   // the capture and window lines among them, in file order
    bool ended = false;           // this call applied the last line
  };

  explicit ReplayCursor(std::vector<RecordingLine> lines) : lines_(std::move(lines)) {}

  Step Advance(uint64_t swap) {
    Step step;
    while (next_ < lines_.size() && DueAt(lines_[next_]) <= swap) {
      const RecordingLine& line = lines_[next_];
      due_ = DueAt(line);
      if (line.capture || line.window) {
        step.captures += line.capture ? 1 : 0;
        step.events.push_back(next_);
      } else {
        state_ = line.state;
      }
      step.applied.push_back(next_++);
      step.ended = next_ == lines_.size();
    }
    step.state = state_;
    return step;
  }

  size_t applied() const { return next_; }
  size_t size() const { return lines_.size(); }
  const RecordingLine& line(size_t index) const { return lines_[index]; }

 private:
  // The Swap `line` (the next one) is due at, saturating at the largest Swap instead of wrapping.
  uint64_t DueAt(const RecordingLine& line) const {
    return line.frames > UINT64_MAX - due_ ? UINT64_MAX : due_ + line.frames;
  }

  std::vector<RecordingLine> lines_;
  size_t next_ = 0;
  uint64_t due_ = 0;  // the Swap the last applied line was due at
  PadState state_;
};

}  // namespace futari::dev
