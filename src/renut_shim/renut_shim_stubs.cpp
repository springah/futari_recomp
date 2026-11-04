/**
 * @file        renut_shim_stubs.cpp
 * @brief       Link-time pieces the Renut renderer takes from futari-nx's tree, provided here instead.
 *
 * futari_recomp (BSD 3-Clause). The renderer in ../renut_engine declares these (REXCVAR_DECLARE, extern "C") and
 * futari-nx defines them in files this port does not vendor: src/me/me_native_system.cpp (the futari_vblank_* cvars),
 * src/native/futari_native_frame.cpp (futari_late_input) and src/me/me_pc_stubs.cpp (the Switch perf counters).
 * Defaults and help texts follow futari-linux @ deca8dd (2026-11-01); the vblank thread that reads them is
 * ../renut_engine/futari_renut_vblank.cpp.
 */

#include <cstdint>

#include <rex/cvar.h>

// ---- futari-nx src/me/me_native_system.cpp ---------------------------------------------------------------------

REXCVAR_DEFINE_BOOL(futari_vblank_sleep_exact, true, "Futari",
                    "Native renderer: the vblank thread sleeps until the exact next vblank instead of 1 ms steps")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// Input lag (futari-nx 2026-10-16). The game starts each frame at its vertical blank: it reads the controller, runs
// the frame and Swaps. With a free-running 60 Hz timer as the vblank, the finished image waited 0 to 16.7 ms for the
// display's next refresh, a wait that slid with the drift between the two clocks. With this, the guest's vblank
// follows the display's refresh (Vulkan: when the presents are shown, VK_KHR_present_wait).
REXCVAR_DEFINE_BOOL(futari_vblank_display_sync, true, "Futari",
                    "The game's vertical blank follows the display's own refresh (Vulkan: when the presents are "
                    "shown, VK_KHR_present_wait) instead of a 60 Hz timer of its own (input lag, judder); "
                    "false = the timer")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// RetroArch's "frame delay": with the vblank locked to the display, start the frame this long after the refresh.
REXCVAR_DEFINE_INT32(futari_vblank_delay_us, 0, "Futari",
                     "With futari_vblank_display_sync and futari_vblank_phase_auto off: the game's vertical "
                     "blank comes this many microseconds after the display's refresh (0 = at the refresh)")
    .range(0, 12000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
// The phase found by measurement instead: each refresh the vblank thread looks at whether the newest image was
// finished in time; after 3 s of images that were, the guest's vblank moves 0.25 ms later, and back on a miss.
REXCVAR_DEFINE_BOOL(futari_vblank_phase_auto, true, "Futari",
                    "With futari_vblank_display_sync: find the latest guest vblank after the display's refresh "
                    "at which the image is still finished for the next refresh (less input lag); false = "
                    "futari_vblank_delay_us")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);
REXCVAR_DEFINE_INT32(futari_vblank_phase_max_us, 9000, "Futari",
                     "futari_vblank_phase_auto: the latest it may put the guest's vblank after the refresh")
    .range(0, 14000)
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// ---- futari-nx src/native/futari_native_frame.cpp --------------------------------------------------------------

// The first controller read after each Swap waits by the time the frame can spare (the SDK's
// SetFrameReadDelayFunction, installed by ../renut_engine/gpu/backend/render_thread.cpp).
REXCVAR_DEFINE_BOOL(futari_late_input, true, "Futari",
                    "Input lag: the game reads its controllers as late as its frame allows (its first read after "
                    "each Swap waits by the time the frame can spare); false = right after the Swap")
    .lifecycle(rex::cvar::Lifecycle::kInitOnly);

// ---- futari-nx src/me/me_pc_stubs.cpp --------------------------------------------------------------------------

// The Switch profiler counters the renderer reports to (perf/frame_stats.cpp) have nothing to feed on the PC.
extern "C" void RexSwitchPerfCount(unsigned) {}
extern "C" void RexSwitchPerfHitch(uint64_t, uint64_t) {}
