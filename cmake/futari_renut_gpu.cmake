# futari_recomp (2026-11-03, branch renderer/renut): Renut's Direct3D-level renderer (github.com/masterspike52/reNut by
# reNut and masterspike52, through banjo-nx's adaptation and cerroverb/futari-linux's port to this game, GPL-3) as
# Futari's second renderer beside the SDK's xenos plugin, chosen at boot with futari_renderer = "renut"
# (src/renut_engine/gpu/banjo_renut_select.h). No PM4 ring and no EDRAM model: Futari's Direct3D runs natively through
# plume (Vulkan and D3D12 on the PC). Derived from futari-linux's app/cmake/futari_renut_gpu.cmake (kept beside this
# file as futari_renut_gpu.cmake.upstream): there the CMake root is app/ and the game repo is one level up; here the
# CMake root is the repo root, so every "${CMAKE_SOURCE_DIR}/.." became "${CMAKE_SOURCE_DIR}", and the Switch
# (deko3d / uam / NVK) blocks are gone, this tree builds for Windows and macOS only. Its hooks name Futari's Direct3D
# functions (src/renut_engine/futari_d3d_functions.txt; tools/renut/renut_futari_hooks.py writes the sites in
# default.xex and the eight cores).
#
# An OBJECT library, not a static one: its hooks replace the generated code's weak sub_X definitions, and a static
# archive's members are only pulled in for undefined symbols, so the hooks would silently not be linked.
#
# Included from the root CMakeLists.txt after rexglue_setup_target(futari ...): the futari target and the eight core
# targets (generated/default/dll_targets.cmake) exist by then.

if(FUTARI_CENSUS_DIR)
    message(FATAL_ERROR "FUTARI_RENUT_RENDERER and a Direct3D census build (FUTARI_CENSUS_DIR) hook the same functions: "
                        "configure the census build with -DFUTARI_RENUT_RENDERER=OFF")
endif()

# ---------------------------------------------------------------------------------------------------------------------
# Leg-2 options.
# ---------------------------------------------------------------------------------------------------------------------
# ON (this leg): a shader_cache.cpp with EMPTY tables, generated in the build tree from
# cmake/renut_shader_cache_stub.cpp.in, so the renderer compiles and links without game data (no dump, no XenosRecomp,
# no Python). Every shader the game creates then misses the cache and goes to Renut's runtime capture
# (renut_shader_capture_dir). OFF: gpu.cmake's real pipeline, unchanged: tools/renut/renut_dump_shaders.py's dump ->
# Renut's XenosRecomp (an ExternalProject) -> shader_cache.cpp with every translated shader. Leg 4 moves that pipeline
# to extract time (docs/NOTES_renut_port_scoping.md section 7): translated game shaders in the binary would break the
# no-bundled-game-data rule.
option(FUTARI_RENUT_STUB_SHADER_CACHE "Renut: an empty shader cache (no dump / XenosRecomp run); the real pipeline when OFF" ON)
# OFF (this leg): src/renut_engine/futari_renut_core_hooks.cpp is futari-linux's output of
# tools/renut/renut_futari_hooks.py and has not been regenerated for this tree yet (leg 3 does, and turns this on).
option(FUTARI_RENUT_CORE_HOOKS "Renut: compile the per-core forwarding hooks (futari_renut_core_hooks.cpp) into the eight cores" ON)

# ---------------------------------------------------------------------------------------------------------------------
# Tools. Python only runs in the real pipeline (the dump and the early-alpha pass); gpu.cmake's own check for the
# xxhash module is skipped while the cache is overridden (its "futari_recomp:" edit next to that check).
# ---------------------------------------------------------------------------------------------------------------------
if(NOT FUTARI_RENUT_STUB_SHADER_CACHE)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    set(PYTHON_EXECUTABLE "${Python3_EXECUTABLE}")
endif()
# gpu.cmake's early-alpha pass runs tools/renut_early_alpha.py; the Python tools live in tools/renut/ here.
set(RENUT_TOOLS_DIR "${CMAKE_SOURCE_DIR}/tools/renut")

# The shader containers the shader cache is built from: tools/renut/renut_dump_shaders.py's dump of default.xex and
# the eight cores (out/renut_shader_dump; Renut's XenosRecomp takes its containers as they are). A normal variable, so
# it shadows gpu.cmake's cache entry (CMP0126 NEW). Renut's runtime captures (renut_shader_capture_dir) are added to
# it by the build. The game's assets (default.xex) are in assets/ here, gpu.cmake's own RENUT_ASSETS_DIR default.
set(RENUT_SHADER_DUMP_DIR "${CMAKE_SOURCE_DIR}/out/renut_shader_dump")
if(NOT FUTARI_RENUT_STUB_SHADER_CACHE AND NOT EXISTS "${RENUT_SHADER_DUMP_DIR}/manifest.csv")
    message(FATAL_ERROR "No ${RENUT_SHADER_DUMP_DIR}/manifest.csv: run tools/renut/renut_dump_shaders.py first, "
                        "or configure with -DFUTARI_RENUT_STUB_SHADER_CACHE=ON (an empty shader cache)")
endif()

# ---------------------------------------------------------------------------------------------------------------------
# DXC for the host shaders (gpu/shaders/host/*.hlsl -> SPIR-V, and DXIL on Windows). gpu.cmake reads FUTARI_DXC and
# falls back to the dxc.exe vendored with XenosRecomp (thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/bin/x64),
# which is NOT in this repository (the dxc-bin binaries are not checked in). Searched, in order: that folder, the
# developer's XenosRecomp checkouts beside this repo (projects/_tools, where the xenos pipeline's DXC lives, 1.8.2407),
# then the Windows SDK's dxc.exe. -DFUTARI_DXC=<path> overrides.
# ---------------------------------------------------------------------------------------------------------------------
if(CMAKE_HOST_WIN32)
    file(GLOB _futari_winsdk_dxc_dirs "C:/Program Files (x86)/Windows Kits/10/bin/10.*/x64")
    list(SORT _futari_winsdk_dxc_dirs ORDER DESCENDING)
    find_program(FUTARI_DXC NAMES dxc.exe dxc
        HINTS "${CMAKE_SOURCE_DIR}/thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/bin/x64"
              "${CMAKE_SOURCE_DIR}/../_tools/XenosRecomp/thirdparty/dxc-bin/bin/x64"
              "${CMAKE_SOURCE_DIR}/../_tools/XenosRecomp-main/thirdparty/dxc-bin/bin/x64"
        PATHS ${_futari_winsdk_dxc_dirs}
        DOC "DXC for Renut's host shaders (empty on other hosts: XenosRecomp's dxc-bin, or dxc on the PATH)"
        NO_DEFAULT_PATH)
    if(NOT FUTARI_DXC)
        find_program(FUTARI_DXC NAMES dxc.exe dxc DOC "DXC for Renut's host shaders")
    endif()
    if(NOT FUTARI_DXC)
        message(FATAL_ERROR "No dxc.exe for Renut's host shaders: put XenosRecomp's dxc-bin under "
                            "thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/bin/x64, install the Windows SDK, "
                            "or configure with -DFUTARI_DXC=<path to dxc.exe>")
    endif()
    message(STATUS "futari: Renut's host shaders with DXC ${FUTARI_DXC}")
    unset(_futari_winsdk_dxc_dirs)
endif()

# Renut's XenosRecomp is built as an ExternalProject (gpu.cmake, the real pipeline only): it gets this build's whole
# host toolchain, not only the compiler paths. On the PC, clang for the MSVC ABI.
set(BANJO_RENUT_XENOS_RECOMP_ARGS "")
if(FUTARI_DXC AND NOT DXC_SYSTEM_TOOL)
    set(DXC_SYSTEM_TOOL "${FUTARI_DXC}")  # the DXC of the host shaders (gpu.cmake) for XenosRecomp too
endif()
# DXC_SYSTEM_TOOL / DXC_SYSTEM_LIB: a DirectXShaderCompiler outside the PATH and the library folders, for Linux
# (thirdparty/reblue-XenosRecomp/thirdparty/dxc-bin/CMakeLists.txt). On Windows that CMakeLists reads only its own
# bin/x64 and lib/x64 folders, so the real pipeline there needs dxc-bin's Windows binaries in place.
foreach(_futari_var CMAKE_C_COMPILER_TARGET CMAKE_CXX_COMPILER_TARGET CMAKE_LINKER_TYPE CMAKE_C_FLAGS CMAKE_CXX_FLAGS
                    CMAKE_AR CMAKE_RC_COMPILER CMAKE_MT CMAKE_MAKE_PROGRAM DXC_SYSTEM_TOOL DXC_SYSTEM_LIB)
    if(DEFINED ${_futari_var} AND NOT "${${_futari_var}}" STREQUAL "")
        list(APPEND BANJO_RENUT_XENOS_RECOMP_ARGS "-D${_futari_var}=${${_futari_var}}")
    endif()
endforeach()

# Renut's SDK exports o1heap as rex::o1heap; this SDK (thirdparty/rexglue-sdk/thirdparty/CMakeLists.txt) names it o1heap.
if(TARGET o1heap AND NOT TARGET rex::o1heap)
    add_library(rex::o1heap ALIAS o1heap)
endif()

# Dear ImGui for Renut's overlay: gpu.cmake compiles a copy of its own into the executable (see its comment on why the
# SDK's context cannot be shared). This tree has no thirdparty/imgui; the SDK's copy is the same 1.92.5
# (thirdparty/rexglue-sdk/thirdparty/imgui, IMGUI_VERSION_NUM 19250), so gpu.cmake's sources come from there
# (its "futari_recomp:" guard on RENUT_IMGUI_DIR).
set(RENUT_IMGUI_DIR "${REXSDK_DIR}/thirdparty/imgui")
foreach(_futari_imgui_src imgui.cpp imgui_draw.cpp imgui_tables.cpp imgui_widgets.cpp)
    if(NOT EXISTS "${RENUT_IMGUI_DIR}/${_futari_imgui_src}")
        message(FATAL_ERROR "No ${RENUT_IMGUI_DIR}/${_futari_imgui_src}: Renut's overlay needs the SDK's Dear ImGui sources")
    endif()
endforeach()

# ---------------------------------------------------------------------------------------------------------------------
# The stub shader cache (FUTARI_RENUT_STUB_SHADER_CACHE). gpu.cmake's renut_xenos_shader_cache() hands over to
# banjo_renut_shader_cache_override() when that command exists (the same seam futari-linux's prebuilt and the Switch
# caches use). The stub defines every symbol gpu/shaders/shader_cache.h declares, with zero entries: the tables hold
# one never-indexed zero element each (an empty array is not valid C++, XenosRecomp pads its own tables the same way),
# the compressed blobs are one byte with size 0, and g_shaderCacheLayout is kShaderCacheLayout so shader_library.cpp's
# layout check passes. configure_file only rewrites the output when its content changes.
# ---------------------------------------------------------------------------------------------------------------------
if(FUTARI_RENUT_STUB_SHADER_CACHE)
    function(banjo_renut_shader_cache_override TARGET)
        set(cache_cpp "${CMAKE_BINARY_DIR}/renut_gpu/shader_cache.cpp")
        configure_file("${CMAKE_SOURCE_DIR}/cmake/renut_shader_cache_stub.cpp.in" "${cache_cpp}" @ONLY)
        target_sources(${TARGET} PRIVATE "${cache_cpp}")
        set_source_files_properties("${cache_cpp}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
        message(STATUS "futari: Renut's shader cache is the EMPTY stub (FUTARI_RENUT_STUB_SHADER_CACHE): ${cache_cpp}")
    endfunction()
endif()

# plume (thirdparty/plume, added by gpu.cmake): Vulkan always, D3D12 on Windows (its CMakeLists keys the backends on
# WIN32), Metal on macOS. No SDL surface (PLUME_SDL_VULKAN_ENABLED is Linux-only there) and no deko3d (Switch).
include("${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/gpu.cmake")

add_library(futari_renut_gpu OBJECT
    "${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/banjo_renut_select.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/banjo_renut_tuning.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/banjo_renut_checks.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/gpu/banjo_renut_record.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/display_sync.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/perf/frame_stats.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/futari_renut_shim.cpp"
    "${CMAKE_SOURCE_DIR}/src/renut_engine/futari_renut_vblank.cpp"
    # This tree's stand-ins for futari-nx's cvars (futari_vblank_*, futari_late_input) and Switch perf counters.
    "${CMAKE_SOURCE_DIR}/src/renut_shim/renut_shim_stubs.cpp")
target_include_directories(futari_renut_gpu PRIVATE
    "${CMAKE_SOURCE_DIR}"
    "${CMAKE_SOURCE_DIR}/src"
    # The five futari-nx headers Renut includes (me/me_platform_compat.h, cpu/banjo_enhancements.h,
    # native/futari_native.h, profiler/profiler.h, me/masseffect/banjo_pmull_fingerprint.h), as this tree's shims.
    "${CMAKE_SOURCE_DIR}/src/renut_shim"
    "${REXGLUE_ENTRYPOINT_INCLUDE_DIR}"
    # banjo_renut_tuning.cpp: the SDK's thread helpers (through me/me_platform_compat.h).
    "${REXSDK_DIR}/src/core")
target_compile_definitions(futari_renut_gpu PRIVATE FUTARI_RENUT_RENDERER=1)
target_link_libraries(futari_renut_gpu PRIVATE rex::runtime)
if(COMMAND rexglue_apply_target_settings)
    rexglue_apply_target_settings(futari_renut_gpu)
endif()
renut_gpu_setup(futari_renut_gpu)
# The renderer's own code without the game code's -ffp-contract / aliasing rules: it is host code (as in banjo-nx).
target_link_libraries(futari PRIVATE futari_renut_gpu)
target_compile_definitions(futari PRIVATE FUTARI_RENUT_RENDERER=1)

# The cores' forwarding (generated by tools/renut/renut_futari_hooks.py) and their own hooks' hand-over. The
# FUTARI_RENUT_RENDERER define goes on those sources only: on the whole core target it would change the command line of
# every generated core file. FUTARI_CORE_<name> (which core the hooks file is compiled into) has to be a target-wide
# define, the one per-core thing about an otherwise shared source; futari-nx's dll_targets template sets it.
if(FUTARI_RENUT_CORE_HOOKS)
    set(_futari_core_hooks "${CMAKE_SOURCE_DIR}/src/renut_engine/futari_renut_core_hooks.cpp")
    foreach(_core palm_normal palm_abnormal reco_normal reco_abnormal hirow_normal hirow_abnormal kiniro_normal kiniro_abnormal)
        if(TARGET futari_${_core})
            target_sources(futari_${_core} PRIVATE "${_futari_core_hooks}")
            target_compile_definitions(futari_${_core} PRIVATE FUTARI_CORE_${_core})
        endif()
    endforeach()
    set_source_files_properties("${_futari_core_hooks}" PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
    set_property(SOURCE "${_futari_core_hooks}" APPEND PROPERTY COMPILE_DEFINITIONS FUTARI_RENUT_RENDERER=1)
    # futari-nx's CPU natives that yield to Renut (src/native/futari_native_core.cpp); this tree has none (section 2 of
    # docs/NOTES_renut_port_scoping.md: Renut hooks those addresses directly here).
    if(EXISTS "${CMAKE_SOURCE_DIR}/src/native/futari_native_core.cpp")
        set_property(SOURCE "${CMAKE_SOURCE_DIR}/src/native/futari_native_core.cpp" APPEND PROPERTY
                     COMPILE_DEFINITIONS FUTARI_RENUT_RENDERER=1)
    endif()
    unset(_futari_core_hooks)
endif()
