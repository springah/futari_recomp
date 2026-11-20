// Dev aid: see input_recording_driver.h.
#include "dev/input_recording_driver.h"

#include <rex/frame_latency.h>
#include <rex/logging.h>

#include <utility>

namespace futari::dev {

using rex::X_RESULT;  // the X_ERROR_* macros expand to an unqualified X_RESULT
using rex::X_STATUS;
using rex::input::X_INPUT_CAPABILITIES;
using rex::input::X_INPUT_KEYSTROKE;
using rex::input::X_INPUT_STATE;
using rex::input::X_INPUT_VIBRATION;

InputRecordingDriver::InputRecordingDriver(std::unique_ptr<rex::input::InputSystem> inner)
    : InputDriver(nullptr, 0), inner_(std::move(inner)) {}

InputRecordingDriver::~InputRecordingDriver() { inner_->Shutdown(); }

std::unique_ptr<InputRecordingDriver> InputRecordingDriver::Record(std::unique_ptr<rex::input::InputSystem> inner,
                                                                   std::ofstream out) {
  std::unique_ptr<InputRecordingDriver> driver(new InputRecordingDriver(std::move(inner)));
  driver->out_ = std::move(out);
  // Flushed at once, like every line, so a killed or crashed run still leaves a recording file that says what it is.
  driver->out_ << "# futari input recording: <guest Swaps since the previous line> <buttons and axes | - | capture>\n"
               << std::flush;
  return driver;
}

std::unique_ptr<InputRecordingDriver> InputRecordingDriver::Replay(std::unique_ptr<rex::input::InputSystem> inner,
                                                                   std::vector<RecordingLine> lines) {
  std::unique_ptr<InputRecordingDriver> driver(new InputRecordingDriver(std::move(inner)));
  driver->cursor_.emplace(std::move(lines));
  return driver;
}

void InputRecordingDriver::SetHooks(std::function<void()> on_capture,
                                    std::function<void(const WindowEvent&)> on_window, std::function<void()> on_end) {
  std::lock_guard lock(mutex_);
  on_capture_ = std::move(on_capture);
  on_window_ = std::move(on_window);
  on_end_ = std::move(on_end);
}

void InputRecordingDriver::WriteLocked(const std::string& line, uint64_t swap) {
  out_ << line << '\n' << std::flush;  // flushed per line, so a crash keeps what came before it
  last_line_swap_ = swap;
}

void InputRecordingDriver::NoteCapture() {
  std::lock_guard lock(mutex_);
  if (!cursor_ && !finished_) {
    const uint64_t swap = rex::latency::SwapCount();
    WriteLocked(FormatCaptureLine(FramesSince(last_line_swap_, swap)), swap);
  }
}

void InputRecordingDriver::Finish() {
  std::lock_guard lock(mutex_);
  if (std::exchange(finished_, true)) {
    return;
  }
  if (cursor_) {
    if (cursor_->applied() < cursor_->size()) {
      REXLOG_INFO("Input replay: stopped at line {} of {}", cursor_->applied(), cursor_->size());
    }
    return;
  }
  const uint64_t swap = rex::latency::SwapCount();
  WriteLocked(FormatLine(FramesSince(last_line_swap_, swap), PadState{}), swap);
  out_.close();
  if (!out_) {
    REXLOG_ERROR("Input record: the recording could not be written in full");
  }
}

X_STATUS InputRecordingDriver::Setup() { return inner_->Setup(); }

void InputRecordingDriver::OnWindowAvailable(rex::ui::Window* window) { inner_->AttachWindow(window); }

X_RESULT InputRecordingDriver::GetCapabilities(uint32_t user_index, uint32_t flags, X_INPUT_CAPABILITIES* out_caps) {
  if (cursor_ && user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return inner_->GetCapabilities(user_index, flags, out_caps);
}

X_RESULT InputRecordingDriver::SetState(uint32_t user_index, X_INPUT_VIBRATION* vibration) {
  if (cursor_ && user_index != 0) {
    return X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return inner_->SetState(user_index, vibration);
}

X_RESULT InputRecordingDriver::GetKeystroke(uint32_t user_index, uint32_t flags, X_INPUT_KEYSTROKE* out_keystroke) {
  if (cursor_) {
    return user_index == 0 ? X_ERROR_EMPTY : X_ERROR_DEVICE_NOT_CONNECTED;
  }
  return inner_->GetKeystroke(user_index, flags, out_keystroke);
}

// The Swap count is read under the lock, so the lines (and the replay's due Swaps) follow it in order across the
// threads that poll and the F9 capture.
X_RESULT InputRecordingDriver::GetState(uint32_t user_index, X_INPUT_STATE* out_state) {
  if (cursor_) {
    // Replay: the recorded pad, whatever the real input, focus or overlays do.
    if (user_index != 0) {
      return X_ERROR_DEVICE_NOT_CONNECTED;
    }
    std::function<void()> on_capture;
    std::function<void(const WindowEvent&)> on_window;
    std::function<void()> on_end;
    ReplayCursor::Step step;
    std::vector<std::optional<WindowEvent>> events;  // nothing: a capture
    uint32_t packet = 0;
    {
      std::lock_guard lock(mutex_);
      if (!finished_) {
        const uint64_t swap = rex::latency::SwapCount();
        step = cursor_->Advance(swap);
        for (const size_t index : step.applied) {
          REXLOG_INFO("Input replay: frame {} line {}: {}", swap, index + 1, cursor_->line(index).text);
        }
        if (!(step.state == replayed_)) {
          replayed_ = step.state;
          ++packet_;
        }
        for (const size_t index : step.events) {
          events.push_back(cursor_->line(index).window);
        }
        on_capture = on_capture_;
        on_window = on_window_;
        on_end = on_end_;
      }
      step.state = replayed_;
      packet = packet_;
    }
    for (const std::optional<WindowEvent>& event : events) {
      if (event && on_window) {
        on_window(*event);
      } else if (!event && on_capture) {
        on_capture();
      }
    }
    if (step.ended) {
      REXLOG_INFO("Input replay: finished");
      if (on_end) {
        on_end();
      }
    }
    if (out_state) {
      out_state->packet_number = packet;
      out_state->gamepad.buttons = step.state.buttons;
      out_state->gamepad.left_trigger = step.state.lt;
      out_state->gamepad.right_trigger = step.state.rt;
      out_state->gamepad.thumb_lx = step.state.lx;
      out_state->gamepad.thumb_ly = step.state.ly;
      out_state->gamepad.thumb_rx = step.state.rx;
      out_state->gamepad.thumb_ry = step.state.ry;
    }
    return X_ERROR_SUCCESS;
  }

  // Recording: the inner system's merged pad (SDL or XInput plus the keyboard as pad 0), zero while an overlay holds
  // the input (the outer system's active callback reaches this driver only, not the inner ones).
  X_INPUT_STATE state = {};
  const X_RESULT result = inner_->GetState(user_index, &state);
  if (result == X_ERROR_SUCCESS && !is_active()) {
    state.gamepad = {};
  }
  if (user_index == 0) {
    PadState pad;
    if (result == X_ERROR_SUCCESS) {
      pad.buttons = state.gamepad.buttons;
      pad.lt = state.gamepad.left_trigger;
      pad.rt = state.gamepad.right_trigger;
      pad.lx = state.gamepad.thumb_lx;
      pad.ly = state.gamepad.thumb_ly;
      pad.rx = state.gamepad.thumb_rx;
      pad.ry = state.gamepad.thumb_ry;
    }
    pad = Quantise(pad);
    std::lock_guard lock(mutex_);
    if (!finished_ && !(pad == recorded_)) {
      recorded_ = pad;
      const uint64_t swap = rex::latency::SwapCount();
      WriteLocked(FormatLine(FramesSince(last_line_swap_, swap), pad), swap);
    }
  }
  if (out_state && result == X_ERROR_SUCCESS) {
    *out_state = state;
  }
  return result;
}

}  // namespace futari::dev
