// Dev aid: futari_dev_input_record / futari_dev_input_replay (src/main.cpp). An input driver that owns the SDK's default
// input system and either writes user 0's pad to a file as the game reads it, or hands the game a recorded file
// instead of the real input, timed in guest Swaps. The format and the cursor are in input_recording.h.
#pragma once

#include "dev/input_recording.h"

#include <rex/input/input_driver.h>
#include <rex/input/input_system.h>

#include <cstdint>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace futari::dev {

class InputRecordingDriver final : public rex::input::InputDriver {
 public:
  // Records user 0 of `inner` into `out` (open), a line on each change.
  static std::unique_ptr<InputRecordingDriver> Record(std::unique_ptr<rex::input::InputSystem> inner,
                                                      std::ofstream out);
  // Replays `lines` as user 0; users 1-3 are not connected.
  static std::unique_ptr<InputRecordingDriver> Replay(std::unique_ptr<rex::input::InputSystem> inner,
                                                      std::vector<RecordingLine> lines);
  ~InputRecordingDriver() override;

  // Replay: what a capture line, a window line and the end of the file do (called on the guest thread; captures and
  // window events in file order).
  void SetHooks(std::function<void()> on_capture, std::function<void(const WindowEvent&)> on_window,
                std::function<void()> on_end);
  // Recording: the F9 capture key was pressed.
  void NoteCapture();
  // When the window closes: the recording gets its last line; a replay that did not reach its end says where it
  // stopped. The driver does nothing after this.
  void Finish();

  rex::X_STATUS Setup() override;
  rex::X_RESULT GetCapabilities(uint32_t user_index, uint32_t flags,
                                rex::input::X_INPUT_CAPABILITIES* out_caps) override;
  rex::X_RESULT GetState(uint32_t user_index, rex::input::X_INPUT_STATE* out_state) override;
  rex::X_RESULT SetState(uint32_t user_index, rex::input::X_INPUT_VIBRATION* vibration) override;
  rex::X_RESULT GetKeystroke(uint32_t user_index, uint32_t flags,
                             rex::input::X_INPUT_KEYSTROKE* out_keystroke) override;
  void OnWindowAvailable(rex::ui::Window* window) override;

 private:
  explicit InputRecordingDriver(std::unique_ptr<rex::input::InputSystem> inner);
  void WriteLocked(const std::string& line, uint64_t swap);

  std::unique_ptr<rex::input::InputSystem> inner_;
  std::mutex mutex_;
  bool finished_ = false;
  // Recording.
  std::ofstream out_;
  PadState recorded_;
  uint64_t last_line_swap_ = 0;
  // Replay.
  std::optional<ReplayCursor> cursor_;
  PadState replayed_;
  uint32_t packet_ = 0;
  std::function<void()> on_capture_;
  std::function<void(const WindowEvent&)> on_window_;
  std::function<void()> on_end_;
};

}  // namespace futari::dev
