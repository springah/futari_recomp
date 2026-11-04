/**
 * @file    gpu/backend/native_window_apple.mm
 * @brief   macOS: the NSWindow and CAMetalLayer plume's Vulkan back end (over MoltenVK) presents to.
 *
 * futari_recomp (2026-11-04, leg 6 of docs/NOTES_renut_port_scoping.md). GPL-3.0-or-later, as the rest of this
 * directory. plume's Apple RenderWindow is { NSWindow *, CAMetalLayer * } (plume_render_interface_types.h): the window
 * for its CocoaWindow size/refresh-rate wrapper, the layer for vkCreateMetalSurfaceEXT. The SDK's macOS window
 * (src/ui/window_mac.cpp) makes an SDL Metal view at open, whose layer is a CAMetalLayer: that one is taken, so the
 * swapchain follows the view SDL already sizes with the window. When none is found (a different SDK window), one is
 * made with SDL_Metal_CreateView. AppKit views belong to the main thread; the work is sent there when called elsewhere.
 */
#import <Cocoa/Cocoa.h>
#import <QuartzCore/CAMetalLayer.h>

#include <SDL3/SDL_metal.h>
#include <SDL3/SDL_properties.h>
#include <SDL3/SDL_video.h>

namespace renut::gpu {

namespace {

CAMetalLayer *FindMetalLayer(NSView *view) {
  if (view == nil)
    return nil;
  if ([view.layer isKindOfClass:[CAMetalLayer class]])
    return (CAMetalLayer *)view.layer;
  for (NSView *child in view.subviews) {
    if (CAMetalLayer *layer = FindMetalLayer(child))
      return layer;
  }
  return nil;
}

} // namespace

bool AppleRenderWindow(SDL_Window *window, void **ns_window, void **metal_layer) {
  __block bool ok = false;
  void (^work)(void) = ^{
    @autoreleasepool {
      NSWindow *nswindow = (__bridge NSWindow *)SDL_GetPointerProperty(
          SDL_GetWindowProperties(window), SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, nullptr);
      if (nswindow == nil)
        return;
      CAMetalLayer *layer = FindMetalLayer(nswindow.contentView);
      if (layer == nil) {
        SDL_MetalView view = SDL_Metal_CreateView(window);  // kept for the window's lifetime
        layer = view ? (__bridge CAMetalLayer *)SDL_Metal_GetLayer(view) : nil;
      }
      if (layer == nil)
        return;
      *ns_window = (__bridge void *)nswindow;
      *metal_layer = (__bridge void *)layer;
      ok = true;
    }
  };
  if ([NSThread isMainThread])
    work();
  else
    dispatch_sync(dispatch_get_main_queue(), work);
  return ok;
}

} // namespace renut::gpu
