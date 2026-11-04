# futari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(futari VERSION 1.5.5 LANGUAGES CXX)

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

target_compile_definitions(futari PRIVATE FUTARI_VERSION_STRING="${PROJECT_VERSION}")
target_include_directories(futari PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src")
if(WIN32)
    # The launcher's file dialogs (IFileOpenDialog) and its child processes.
    target_link_libraries(futari PRIVATE ole32 uuid)
elseif(APPLE)
    target_link_libraries(futari PRIVATE "-framework Cocoa")
endif()

# Renut's Direct3D-level renderer (src/renut_engine, GPL-3; cmake/futari_renut_gpu.cmake), beside the SDK's xenos
# plugin, which stays the fallback: futari_renderer = "renut" selects it at boot. After rexglue_setup_target: it links
# into the futari target and, with FUTARI_RENUT_CORE_HOOKS, into the eight core targets dll_targets.cmake made.
option(FUTARI_RENUT_RENDERER "Renut's Direct3D-level renderer (futari_renderer = renut)" ON)
if(FUTARI_RENUT_RENDERER)
    include(cmake/futari_renut_gpu.cmake)
endif()
