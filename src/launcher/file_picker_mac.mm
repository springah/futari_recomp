/**
 * @file        file_picker_mac.mm
 * @brief       macOS: NSOpenPanel for the launcher. futari_recomp (BSD 3-Clause), 2026-11-04 (leg 6).
 *
 * AppKit panels belong to the main thread, which on macOS is SDL's and the SDK's UI thread: the thread that draws the
 * launcher's ImGui frame and pumps the events. A modal -runModal from inside that frame would stop the frame (and the
 * swapchain) until the user answers, so the panel is opened NON-modally with -beginWithCompletionHandler:, and the
 * dialog polls (Poll() in file_picker.cpp) once a frame as it does the Windows picker's thread. The completion handler
 * runs on the main thread from the run loop SDL pumps every frame. Open() may be called from any thread: the panel is
 * created on the main queue either way.
 */
#include "launcher/file_picker.h"
#include "launcher/file_picker_impl.h"

#import <Cocoa/Cocoa.h>
#import <UniformTypeIdentifiers/UniformTypeIdentifiers.h>

#include <string>

namespace futari::launcher {

void FilePicker::Open(void*, bool folder, const std::string& title, const std::string& extension) {
  if (!impl_) {
    impl_ = new Impl();
  }
  {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (impl_->state == PickState::kPending) {
      return;  // one panel at a time
    }
    impl_->state = PickState::kPending;
  }
  Impl* impl = impl_;
  NSString* ns_title = [NSString stringWithUTF8String:title.c_str()];
  NSString* ns_ext = extension.empty() ? nil : [NSString stringWithUTF8String:extension.c_str()];
  void (^show)(void) = ^{
    @autoreleasepool {
      NSOpenPanel* panel = [NSOpenPanel openPanel];
      panel.title = ns_title;
      panel.message = ns_title;  // the title bar is hidden on recent macOS; the message line is not
      panel.canChooseFiles = folder ? NO : YES;
      panel.canChooseDirectories = folder ? YES : NO;
      panel.allowsMultipleSelection = NO;
      panel.canCreateDirectories = NO;
      panel.resolvesAliases = YES;
      if (!folder && ns_ext != nil) {
        UTType* type = [UTType typeWithFilenameExtension:ns_ext];
        if (type != nil) {
          panel.allowedContentTypes = @[ type ];
        }
      }
      [NSApp activateIgnoringOtherApps:YES];
      [panel beginWithCompletionHandler:^(NSModalResponse response) {
        std::lock_guard<std::mutex> lock(impl->mutex);
        if (response == NSModalResponseOK && panel.URLs.count > 0) {
          impl->picked = std::filesystem::path([[panel.URLs objectAtIndex:0] fileSystemRepresentation]);
          impl->state = PickState::kPicked;
        } else {
          impl->state = PickState::kCancelled;
        }
      }];
    }
  };
  if ([NSThread isMainThread]) {
    show();
  } else {
    dispatch_async(dispatch_get_main_queue(), show);
  }
}

}  // namespace futari::launcher
