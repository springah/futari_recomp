// futari_recomp shim for futari-nx's "futari_enhancements.h" (GPL-3; the original lives in cerroverb/futari-linux
// app/src/futari_enhancements.h). Written from scratch for futari_recomp (BSD-3-Clause).
//
// Not one of the five headers the scoping document lists, but src/renut_engine/futari_renut_shim.cpp:10 includes it
// and its EnhancementActive (lines 76-80) calls futari::enhancements::GroupOn(Group).
//
// In futari-nx a group master (futari_cpu_enhancements / futari_noncpu_enhancements, both cvars) gates every banjo_*
// enhancement of that group. futari_recomp has none of futari-nx's CPU natives or codegen passes, and its renderer
// baseline is Renut's own path, so both masters answer OFF: every EnhancementActive(...) in banjo_renut_tuning.cpp,
// banjo_renut_record.cpp and banjo_renut_select.* is false regardless of the enhancement's own key, which is the state
// each enhancement's A/B "off" half already exercises (banjo_renut_tuning.h: *On() = Latch().x && g_ab_off != N).
// Flip kGroupOn to opt the banjo_* enhancements back in (each still needs its own banjo_* key).
#pragma once

namespace futari::enhancements {

enum class Group { kCpu, kNonCpu };

// Both group masters on this build.
constexpr bool kGroupOn = false;

// At startup, after the config is read and before anything reads the keys. Nothing to apply here: there is no
// enhancement table (futari-nx's futari_enhancements.cpp) on this build.
inline void Apply() {}

// The group master.
inline bool GroupOn(Group /*group*/) { return kGroupOn; }

}  // namespace futari::enhancements
