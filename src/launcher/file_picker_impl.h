/**
 * @file        file_picker_impl.h
 * @brief       FilePicker's private state, shared by file_picker.cpp (Poll, the destructor, the Windows Open) and
 *              file_picker_mac.mm (the macOS Open). One definition, so the two files cannot disagree about it.
 *              futari_recomp (BSD 3-Clause), 2026-11-04.
 */
#pragma once

#include <filesystem>
#include <mutex>
#include <string>
#include <thread>

#include "launcher/file_picker.h"

namespace futari::launcher {

struct FilePicker::Impl {
  std::mutex mutex;
  PickState state = PickState::kIdle;
  std::filesystem::path picked;
  std::string error;
  std::thread thread;  // Windows: the dialog's own thread. macOS: unused (the panel runs on the main thread).
};

}  // namespace futari::launcher
