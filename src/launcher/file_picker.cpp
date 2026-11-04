/**
 * @file        file_picker.cpp
 * @brief       Windows: IFileOpenDialog on its own thread. macOS: Open() is file_picker_mac.mm (NSOpenPanel); the
 *              state, Poll() and the destructor here serve both. Other platforms without a native file dialog: always
 *              kFailed.
 */
#include "launcher/file_picker.h"
#include "launcher/file_picker_impl.h"

#include <atomic>
#include <mutex>
#include <thread>

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shobjidl.h>
#endif

namespace futari::launcher {


FilePicker::~FilePicker() {
#ifdef __APPLE__
  if (impl_) {
    std::unique_lock<std::mutex> lock(impl_->mutex);
    if (impl_->state == PickState::kPending) {
      // The open panel's completion handler still holds impl_ and runs later on the main thread, which is this
      // thread: it cannot be waited for here. Leave it the (small) state to write into.
      return;
    }
  }
#endif
  if (impl_) {
    if (impl_->thread.joinable()) {
      impl_->thread.join();  // the dialog is modal to its own thread; the user closing it ends it
    }
    delete impl_;
  }
}

bool FilePicker::pending() const { return impl_ && impl_->state == PickState::kPending; }

PickState FilePicker::Poll(std::filesystem::path& picked, std::string& error) {
  if (!impl_) {
    return PickState::kIdle;
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  const PickState state = impl_->state;
  if (state == PickState::kPicked) {
    picked = impl_->picked;
  } else if (state == PickState::kFailed) {
    error = impl_->error;
  }
  if (state != PickState::kPending && state != PickState::kIdle) {
    impl_->state = PickState::kIdle;  // answered once
    if (impl_->thread.joinable()) {
      impl_->thread.join();
    }
  }
  return state;
}

#ifdef _WIN32
void FilePicker::Open(void* parent, bool folder, const std::string& title, const std::string& extension) {
  if (!impl_) {
    impl_ = new Impl();
  }
  if (impl_->state == PickState::kPending) {
    return;
  }
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  impl_->state = PickState::kPending;
  Impl* impl = impl_;
  const HWND owner = static_cast<HWND>(parent);
  impl->thread = std::thread([impl, owner, folder, title, extension] {
    std::filesystem::path result;
    std::string error;
    bool picked = false;
    const HRESULT init = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
    IFileOpenDialog* dialog = nullptr;
    HRESULT hr = CoCreateInstance(CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&dialog));
    if (SUCCEEDED(hr)) {
      DWORD options = 0;
      dialog->GetOptions(&options);
      options |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST | FOS_FILEMUSTEXIST;
      if (folder) {
        options |= FOS_PICKFOLDERS;
      }
      dialog->SetOptions(options);
      const std::wstring wtitle(title.begin(), title.end());
      dialog->SetTitle(wtitle.c_str());
      std::wstring pattern;
      std::wstring label;
      if (!folder && !extension.empty()) {
        pattern = L"*." + std::wstring(extension.begin(), extension.end());
        label = L"Xbox 360 disc image (" + pattern + L")";
        COMDLG_FILTERSPEC filters[] = {{label.c_str(), pattern.c_str()}, {L"All files", L"*.*"}};
        dialog->SetFileTypes(2, filters);
      }
      hr = dialog->Show(owner);
      if (SUCCEEDED(hr)) {
        IShellItem* item = nullptr;
        if (SUCCEEDED(dialog->GetResult(&item))) {
          PWSTR path = nullptr;
          if (SUCCEEDED(item->GetDisplayName(SIGDN_FILESYSPATH, &path))) {
            result = path;
            picked = true;
            CoTaskMemFree(path);
          }
          item->Release();
        }
      } else if (hr != HRESULT_FROM_WIN32(ERROR_CANCELLED)) {
        error = "the file dialog failed (HRESULT " + std::to_string(uint32_t(hr)) + ")";
      }
      dialog->Release();
    } else {
      error = "the file dialog is not available (HRESULT " + std::to_string(uint32_t(hr)) + ")";
    }
    if (SUCCEEDED(init)) {
      CoUninitialize();
    }
    std::lock_guard<std::mutex> lock(impl->mutex);
    if (picked) {
      impl->picked = result;
      impl->state = PickState::kPicked;
    } else if (!error.empty()) {
      impl->error = error;
      impl->state = PickState::kFailed;
    } else {
      impl->state = PickState::kCancelled;
    }
  });
}
#elif !defined(__APPLE__)
void FilePicker::Open(void*, bool, const std::string&, const std::string&) {
  if (!impl_) {
    impl_ = new Impl();
  }
  std::lock_guard<std::mutex> lock(impl_->mutex);
  impl_->error = "no file dialog on this platform: type the path instead";
  impl_->state = PickState::kFailed;
}
#endif

}  // namespace futari::launcher
