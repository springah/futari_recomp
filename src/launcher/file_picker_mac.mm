/**
 * @file        file_picker_mac.mm
 * @brief       macOS: NSOpenPanel, run modally on the main thread (the SDK's UI thread), so Poll() answers at once.
 *              Not built on this Windows seat (leg 6, macOS, compiles it); kept minimal.
 */
#include "launcher/file_picker.h"

#import <Cocoa/Cocoa.h>

#include <mutex>
#include <thread>

namespace futari::launcher {

struct FilePicker::Impl {
  std::mutex mutex;
  PickState state = PickState::kIdle;
  std::filesystem::path picked;
  std::string error;
  std::thread thread;  // unused here; the Windows picker's shape
};

void FilePicker::Open(void*, bool folder, const std::string& title, const std::string& extension) {
  if (!impl_) {
    impl_ = new Impl();
  }
  @autoreleasepool {
    NSOpenPanel* panel = [NSOpenPanel openPanel];
    panel.title = [NSString stringWithUTF8String:title.c_str()];
    panel.canChooseFiles = folder ? NO : YES;
    panel.canChooseDirectories = folder ? YES : NO;
    panel.allowsMultipleSelection = NO;
    if (!folder && !extension.empty()) {
      panel.allowedFileTypes = @[ [NSString stringWithUTF8String:extension.c_str()] ];
      panel.allowsOtherFileTypes = YES;
    }
    const NSModalResponse response = [panel runModal];
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (response == NSModalResponseOK && panel.URLs.count > 0) {
      impl_->picked = std::filesystem::path([[panel.URLs objectAtIndex:0] fileSystemRepresentation]);
      impl_->state = PickState::kPicked;
    } else {
      impl_->state = PickState::kCancelled;
    }
  }
}

}  // namespace futari::launcher
