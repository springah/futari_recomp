// futari_recomp shim for futari-nx's "me/me_platform_compat.h" (banjo-nx, GPL-3; the original lives in
// cerroverb/futari-linux app/src/me/me_platform_compat.h). Written from scratch for futari_recomp (BSD-3-Clause).
//
// What the original did: on aarch64 it included <arm_neon.h>, elsewhere SIMDe's NEON aliases, and on the Switch the
// SDK's threading_switch.h thread helpers; off the Switch the helpers were no-ops returning true.
//
// What Renut uses from it here (src/renut_engine/display_sync.cpp, futari_renut_vblank.cpp, gpu/banjo_renut_record.cpp,
// gpu/banjo_renut_tuning.cpp): only the RexSwitch* thread helpers, and every call sits inside #if REX_PLATFORM_SWITCH,
// which futari_recomp's rex/platform.h defines 0 -- so these bodies are never compiled into a Windows or macOS build.
// They keep the original's off-Switch semantics (accepted, nothing done, "no core"). The NEON part is not needed:
// Renut's vectorised files (gpu/line_compare.h, gpu/render/vertex_format.cpp) include <arm_neon.h> themselves under
// #if defined(__aarch64__) and take their scalar path on x86-64.
#pragma once

#include <rex/platform.h>

#if REX_PLATFORM_SWITCH
#error "me_platform_compat.h shim: futari_recomp has no Switch build; use futari-nx's header there"
#endif

// Switch thread helpers (sdk/src/core/threading_switch.h in banjo-nx), as the original's non-Switch branch: the
// Switch's priority (0x2C render thread, 0x2D record worker, 0x3B game/background) and core model do not map onto a
// PC, where the host scheduler runs these threads at the default priority on any core.
inline bool RexSwitchSetCurrentThreadPriorityOk(int /*switch_priority*/) { return true; }
inline int RexSwitchCurrentCore() { return -1; }
inline bool RexSwitchSetCurrentThreadCore(int /*core*/) { return true; }
inline bool RexSwitchPinCurrentThreadToCore(int /*core*/) { return true; }
inline bool RexSwitchUnpinCurrentThread(int /*core*/) { return true; }
