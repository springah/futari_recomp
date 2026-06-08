# futari - ReXGlue Recompiled Project
#
# SDK boilerplate lives in generated/rexglue.cmake.

cmake_minimum_required(VERSION 3.25)
project(futari LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

rexglue_setup_target(futari)
