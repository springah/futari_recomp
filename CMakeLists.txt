# futari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(futari VERSION 1.5.11 LANGUAGES CXX)

# macOS: the launcher's file picker is Objective-C++ (src/launcher/file_picker_mac.mm) and plume's Apple glue is too
# (thirdparty/plume/plume_apple.mm). Enabled here, at the root: a language enabled only inside plume's subdirectory
# leaves the root's .mm without a compile rule (CMAKE_OBJCXX_COMPILE_OBJECT unset at generate time). The same
# compiler as C/C++ (the presets' Homebrew LLVM), not whatever clang is first on the PATH.
if(APPLE)
    if(NOT CMAKE_OBJC_COMPILER AND CMAKE_C_COMPILER)
        set(CMAKE_OBJC_COMPILER "${CMAKE_C_COMPILER}")
    endif()
    if(NOT CMAKE_OBJCXX_COMPILER)
        set(CMAKE_OBJCXX_COMPILER "${CMAKE_CXX_COMPILER}")
    endif()
    enable_language(C OBJC OBJCXX)
endif()

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK resolution: the vendored tree at thirdparty/rexglue-sdk unless the
# configure line passes -DREXSDK_DIR=<path> to build against a separate
# checkout instead (the presets do not set it).
if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk"
        CACHE PATH "Path to rexglue-sdk source tree" FORCE)
endif()

# Keep the build machine's directory names out of the binaries. Assertions and
# log calls record source paths; map the tree's root to "." so they read
# "./src/main.cpp" rather than the full path of whoever built it. Before the
# SDK is added below, so that its targets get the flag too. Not on Windows: the
# clang-cl build records none (scanned at v1.5.11), and the flag would only
# force a full rebuild there.
if(NOT WIN32 AND CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    add_compile_options(
        "$<$<COMPILE_LANGUAGE:C,CXX,OBJC,OBJCXX>:-ffile-prefix-map=${CMAKE_SOURCE_DIR}=.>")
endif()

# generated/ is codegen output and is gitignored, so a fresh clone does not have
# it. Say so plainly -- the bare include() failure is an unhelpful place for a
# first-time builder to land.
if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/generated/rexglue.cmake")
    message(FATAL_ERROR
        "generated/rexglue.cmake is missing -- the recompiled sources have not "
        "been generated yet.\n"
        "Supply your own game dump in assets/ (see README: \"Game data\"), then "
        "run:\n"
        "    bash scripts/regen.sh\n"
        "and re-run this configure step.")
endif()

include(generated/rexglue.cmake)

# Sources
set(FUTARI_SOURCES
    src/main.cpp
    # The launcher (2026-11-03): first-run setup + settings inside the exe (docs/NOTES_launcher.md).
    src/launcher/game_source.cpp
    src/launcher/shader_pipeline.cpp
    src/launcher/launcher_dialog.cpp
    src/launcher/file_picker.cpp
    src/launcher/update_check.cpp
    # Dev aid (2026-11-04): futari_input_script, a scripted virtual gamepad for renderer gates.
    src/dev/input_script.cpp
)
if(APPLE)
    # The NSOpenPanel picker is Objective-C++ and the project() line names CXX only. OBJC too: with OBJCXX alone
    # enabled at this scope, CMake hands the SDK's SDL3 .m files to the Objective-C++ compiler (-std=c++23 fails).
    enable_language(OBJC OBJCXX)
    list(APPEND FUTARI_SOURCES src/launcher/file_picker_mac.mm)
endif()

if(WIN32)
    # Embed the application icon via a Windows resource script. The .rc is
    # generated with the absolute .ico path so the RC compiler resolves it
    # regardless of the build working directory.
    enable_language(RC)
    set(FUTARI_ICON_ABS "${CMAKE_CURRENT_SOURCE_DIR}/src/futari.ico")
    configure_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/src/futari.rc.in"
        "${CMAKE_CURRENT_BINARY_DIR}/futari.rc"
        @ONLY)
    add_executable(futari WIN32 ${FUTARI_SOURCES} "${CMAKE_CURRENT_BINARY_DIR}/futari.rc")
else()
    add_executable(futari ${FUTARI_SOURCES})
endif()

rexglue_setup_target(futari GPU_PLUGINS xenos)

# Same idea for the run-paths the linker records in the shared libraries: CMake
# would write the SDK's absolute output directory into every library that links
# the runtime. They are only ever loaded from beside the executable, so point
# them there instead.
if(UNIX)
    if(APPLE)
        set(_self_rpath "@loader_path")
    else()
        set(_self_rpath "$ORIGIN")
    endif()
    foreach(_lib rexruntime rexgpu-xenos
            futari_reco_normal futari_reco_abnormal futari_palm_normal futari_palm_abnormal
            futari_hirow_normal futari_hirow_abnormal futari_kiniro_normal futari_kiniro_abnormal)
        if(TARGET ${_lib})
            set_target_properties(${_lib} PROPERTIES
                BUILD_WITH_INSTALL_RPATH ON
                INSTALL_RPATH "${_self_rpath}"
                INSTALL_RPATH_USE_LINK_PATH OFF)
        endif()
    endforeach()
endif()

target_compile_definitions(futari PRIVATE FUTARI_VERSION_STRING="${PROJECT_VERSION}")
# The shader package stamp (src/launcher/shader_pipeline.cpp, vulkan_spirv.stamp): an identifier of the translator
# built into the exe, a hash of its sources (Renut's XenosRecomp + the in-exe entry point), so a package made by a
# different translator is redone. Computed here for every build, Renut or not; editing a hashed file re-configures.
file(GLOB _futari_translator_sources CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/reblue-XenosRecomp/XenosRecomp/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/reblue-XenosRecomp/XenosRecomp/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/renut_shim/renut_shader_translate_tool.cpp")
list(SORT _futari_translator_sources)
set(_futari_translator_digest "")
foreach(_f IN LISTS _futari_translator_sources)
    file(SHA256 "${_f}" _h)
    string(APPEND _futari_translator_digest "${_h}")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${_f}")
endforeach()
string(SHA256 _futari_translator_hash "${_futari_translator_digest}")
string(SUBSTRING "${_futari_translator_hash}" 0 12 _futari_translator_hash)
target_compile_definitions(futari PRIVATE FUTARI_SHADER_TRANSLATOR_HASH="${_futari_translator_hash}")
target_include_directories(futari PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    # The launcher's file dialogs (IFileOpenDialog) and its child processes.
    # The update check (WinHTTP) and opening the release page (ShellExecuteW).
    target_link_libraries(futari PRIVATE ole32 uuid winhttp shell32)
elseif(APPLE)
    # The launcher's NSOpenPanel (src/launcher/file_picker_mac.mm) and its file-type filter (UTType), and the update
    # check's HTTP client: the system libcurl (/usr/lib/libcurl.4.dylib, on every Mac).
    target_link_libraries(futari PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers" curl)
endif()

# Renut's Direct3D-level renderer (src/renut_engine, GPL-3; cmake/futari_renut_gpu.cmake), beside the SDK's xenos
# plugin, which stays the fallback: futari_renderer = "renut" selects it at boot. After rexglue_setup_target: it links
# into the futari target and, with FUTARI_RENUT_CORE_HOOKS, into the eight core targets dll_targets.cmake made.
option(FUTARI_RENUT_RENDERER "Renut's Direct3D-level renderer (futari_renderer = renut)" ON)
if(FUTARI_RENUT_RENDERER)
    include(cmake/futari_renut_gpu.cmake)
endif()
