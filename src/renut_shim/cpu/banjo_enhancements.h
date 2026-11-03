// futari_recomp shim for futari-nx's "cpu/banjo_enhancements.h" (futari-nx 2026-10-19, GPL-3; the original lives in
// cerroverb/futari-linux app/src/cpu/banjo_enhancements.h). Written from scratch for futari_recomp (BSD-3-Clause).
//
// The interface banjo-nx's renderer code (src/renut_engine/gpu/banjo_renut_*.cpp) asks of the enhancement switches.
// The declarations are kept exactly as the original's because src/renut_engine/futari_renut_shim.cpp DEFINES both
// functions (lines 76-84): EnhancementActive = own key AND futari::enhancements::GroupOn(group), and
// CodegenChecksReport forwards to futari::native::CodegenChecksReport. The answers on this build come from the two
// other shims: futari_enhancements.h (GroupOn -> false: every banjo_* enhancement off, Renut on its plain paths) and
// native/futari_native.h (CodegenChecksReport -> no-op: futari_recomp has no post-codegen self-checks).
#pragma once

namespace banjo {

enum class EnhancementGroup { kCpu, kNonCpu };

// Own key AND the group's master switch. Defined in src/renut_engine/futari_renut_shim.cpp.
bool EnhancementActive(EnhancementGroup group, bool own_key);

// The generated code's self-checks, reported with the renderer's 10 s report. Defined in
// src/renut_engine/futari_renut_shim.cpp (a no-op through native/futari_native.h here).
void CodegenChecksReport(int seconds);

}  // namespace banjo
