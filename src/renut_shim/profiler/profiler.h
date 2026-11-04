// futari_recomp shim for futari-nx's "profiler/profiler.h" (futari-nx 2026-10-19, GPL-3; the original lives in
// cerroverb/futari-linux app/src/profiler/profiler.h). Written from scratch for futari_recomp (BSD-3-Clause).
//
// banjo-nx's on-screen profiler window (ported from UnleashedRecomp) took three rows from Renut's frame statistics
// (src/renut_engine/perf/frame_stats.cpp:87-99). futari-nx already had no such window and made these no-ops; so does
// futari_recomp. The same numbers reach the "[renut]" 10 s report through banjo::renut_gpu::Stats*.
#pragma once

namespace banjo::profiler {

inline void SetGpuFrameMs(double /*ms*/) {}
inline void SetRenderThreadMs(double /*ms*/) {}
inline void RecordHostPresent() {}

}  // namespace banjo::profiler
