//
// plume
//
// Copyright (c) 2024 renderbag and contributors. All rights reserved.
// Licensed under the MIT license. See LICENSE file for details.
//

#pragma once

#include <atomic>
#include <mutex>
#include "plume_render_interface_types.h"

namespace plume {
    RenderDeviceVendor getRenderDeviceVendor(uint64_t registryID);

    struct CocoaWindowAttributes {
        int x, y;
        int width, height;
    };

    class CocoaWindow {
        void* windowHandle;
        // futari_recomp (2026-11-20): the CAMetalLayer the surface is made on, or null (see the constructor).
        void* layerHandle;
        CocoaWindowAttributes cachedAttributes;
        std::atomic<int> cachedRefreshRate;
        mutable std::mutex attributesMutex;

        void updateWindowAttributesInternal(bool forceSync = false);
        void updateRefreshRateInternal(bool forceSync = false);
    public:
        // futari_recomp (2026-11-20): `layer`, when given, is the CAMetalLayer the surface is made on. The window's
        // size is then counted in that layer's pixels (its contentsScale), so a swapchain sized from it has the
        // surface's own extent whether the layer is 1x or 2x. Without it the size is as before (the window's backing
        // scale under PLUME_APPLE_RETINA_ENABLED, else points).
        CocoaWindow(void* window, void* layer = nullptr);
        ~CocoaWindow();

        // Get cached window attributes, may trigger async update
        void getWindowAttributes(CocoaWindowAttributes* attributes) const;

        // Get cached refresh rate, may trigger async update
        int getRefreshRate() const;

        // Toggle fullscreen
        void toggleFullscreen();
    };
}
