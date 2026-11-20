// banjo-nx (2026-10-17): the presented frame of Renut's renderer, for the test captures (the PNGs, the short video
// and the streamed recording of src/native/nfsmw_nativo_captura.cpp), which take it from the SDK presenter's
// CaptureGuestOutput with banjo-nx's own renderer. Implemented in backend/present.cpp (tools/renut_patches).
#pragma once

#include <vector>

#include <rex/ui/presenter.h>

namespace renut::gpu {

// The next Present scans the guest image out once more into an offscreen target of the frame's size and reads it
// back, waiting for the GPU (a test tool: it costs a GPU sync per capture). Works while the window is minimized.
// Returns false when no frame was presented within 'timeout_ms' or the renderer is not running.
// futari_recomp (2026-11-10): with 'output', the same Present also scans the frame out at the output (swapchain) size,
// as it presents it (bars included, the overlays not), into another offscreen target: *output is that image, or empty
// (width 0) when the window is minimized or it failed.
// futari_recomp (2026-11-15): with 'source', the same Present also reads back the gameplay image the frame's blit
// sampled (futari_screen.h), at its own size: *source is that image, or empty (width 0) when the frame drew no blit or
// it failed (logged either way). When that image is stored as 16-bit floats, *source is its 8-bit view (clamped,
// rounded) and *source_exact its values as float R G B per texel, rows top to bottom; otherwise *source_exact is empty.
bool CaptureFrame(rex::ui::RawImage &out, int timeout_ms = 1000, rex::ui::RawImage *output = nullptr,
                  rex::ui::RawImage *source = nullptr, std::vector<float> *source_exact = nullptr);

} // namespace renut::gpu
