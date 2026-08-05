# futari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(futari VERSION 1.4.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# SDK resolution: default to the pinned submodule when REXSDK_DIR isn't set
# (the presets pass $env{REXSDK_DIR}, which lands here as "" when unset).
# Point REXSDK_DIR elsewhere to build against a live SDK checkout instead.
if(NOT REXSDK_DIR AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk/CMakeLists.txt")
    set(REXSDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/thirdparty/rexglue-sdk"
        CACHE PATH "Path to rexglue-sdk source tree" FORCE)
endif()

include(generated/rexglue.cmake)

# Sources
set(FUTARI_SOURCES
    src/main.cpp
)

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
