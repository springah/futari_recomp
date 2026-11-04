/**
 * @file    gpu/backend/native_window.h
 * @brief   The game window, found from the platform rather than the SDK.
 */
#pragma once

#include <cstdint>

#include <plume_render_interface.h>

namespace renut::gpu {

// Windows: the HWND of this process's game window (the SDK's SDL window).
// Linux: the SDL window itself (plume is built with PLUME_SDL_VULKAN_ENABLED;
// its surface, Wayland or X11, is made in native_window.cpp).
// futari-linux (2026-10-28): the Linux form ported from reNut
// (github.com/masterspike52/reNut 688ff73, gpu/backend/native_window.h).
bool GetNativeRenderWindow(plume::RenderWindow &out);

// The game window's client area, in pixels.
bool GetRenderWindowPixelSize(uint32_t &width, uint32_t &height);

} // namespace renut::gpu
