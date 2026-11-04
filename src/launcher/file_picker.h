/**
 * @file        file_picker.h
 * @brief       The OS's own open-file / open-folder dialog for the launcher. futari_recomp (BSD 3-Clause), 2026-11-03.
 *
 * Windows: IFileOpenDialog on a thread of its own (file_picker.cpp). macOS: NSOpenPanel on the main thread
 * (file_picker_mac.mm). The SDK's SDL3 is built without its dialog subsystem and lives inside the runtime library,
 * so the launcher goes to the OS directly. Poll() answers on the UI thread; a picker that is still open answers
 * kPending.
 */
#pragma once

#include <filesystem>
#include <string>

namespace futari::launcher {

enum class PickState { kIdle, kPending, kPicked, kCancelled, kFailed };

class FilePicker {
 public:
  // `parent` is the game window's native handle (HWND on Windows), or nullptr. `extension` without the dot ("iso")
  // filters a file dialog; `folder` asks for a folder instead.
  void Open(void* parent, bool folder, const std::string& title, const std::string& extension);
  PickState Poll(std::filesystem::path& picked, std::string& error);
  bool pending() const;

  ~FilePicker();

 private:
  struct Impl;
  Impl* impl_ = nullptr;
};

}  // namespace futari::launcher
