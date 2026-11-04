# futari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(futari VERSION 1.5.5 LANGUAGES CXX)

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
)
if(APPLE)
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

target_compile_definitions(futari PRIVATE FUTARI_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(futari PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    # The launcher's file dialogs (IFileOpenDialog) and its child processes.
    target_link_libraries(futari PRIVATE ole32 uuid)
elseif(APPLE)
    # The launcher's NSOpenPanel (src/launcher/file_picker_mac.mm) and its file-type filter (UTType).
    target_link_libraries(futari PRIVATE "-framework Cocoa" "-framework UniformTypeIdentifiers")
endif()

# Renut's Direct3D-level renderer (src/renut_engine, GPL-3; cmake/futari_renut_gpu.cmake), beside the SDK's xenos
# plugin, which stays the fallback: futari_renderer = "renut" selects it at boot. After rexglue_setup_target: it links
# into the futari target and, with FUTARI_RENUT_CORE_HOOKS, into the eight core targets dll_targets.cmake made.
option(FUTARI_RENUT_RENDERER "Renut's Direct3D-level renderer (futari_renderer = renut)" ON)
if(FUTARI_RENUT_RENDERER)
    include(cmake/futari_renut_gpu.cmake)
endif()
