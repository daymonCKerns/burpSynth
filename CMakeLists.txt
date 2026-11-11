# Top-level build description. CMake always starts here, then pulls in the
# VST3 SDK, Eigen and each module's own CMakeLists.txt via add_subdirectory().

cmake_minimum_required(VERSION 3.25)

# Oldest macOS the plugin will load on. Must be set before project() to take
# effect, and before the VST3 SDK is added so it builds for the same target.
set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")

# OBJCXX (Objective-C++) is for VSTGUI's macOS backend, which the editor in
# src/gui uses. CMake wants a language enabled in the highest directory common
# to every target that uses it, so it is enabled here, not only inside the SDK.
project(BurpSynth VERSION 0.0.0 LANGUAGES C CXX OBJCXX)

# Default to a Debug build when no build type is given on the command line.
# Multi-config generators like Xcode choose per build instead, so skip them.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Writes build/<preset>/compile_commands.json so editors (clangd, VS Code)
# know the exact compiler flags and give accurate autocomplete and errors.
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Use standard C++20 (-std=c++20) rather than GNU extensions (-std=gnu++20).
set(CMAKE_CXX_EXTENSIONS OFF)

# A VST3 is a shared library loaded into a DAW alongside other plugins, so
# everything is built position independent with hidden symbols. This lets the
# static libraries below link into the plugin and stops our symbols from
# clashing with other plugins loaded in the same DAW.
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

# Reverse-DNS name macOS and DAWs use to identify the plugin bundle.
set(BURPSYNTH_BUNDLE_ID "com.burpsynth.vst3" CACHE STRING "macOS bundle identifier")

# Every build copies the signed plugin here (see src/plugin/CMakeLists.txt).
# This is the system-wide VST3 folder, writable by admin users without sudo.
set(BURPSYNTH_VST3_INSTALL_DIR "/Library/Audio/Plug-Ins/VST3" CACHE PATH
    "Folder each build copies BurpSynth.vst3 into")

# Builds the unit tests in tests/ (run with ctest) and the command-line tools
# in tools/ (render_wav). They need only the model and engine, not a DAW.
# Turn OFF to build just the plugin.
option(BURPSYNTH_BUILD_TESTS "Build the unit tests and command-line tools" ON)

# --- VST3 SDK ---------------------------------------------------------------
# Lives in libs/vst3sdk as a git submodule (see README for the command).
set(vst3sdk_SOURCE_DIR "${PROJECT_SOURCE_DIR}/libs/vst3sdk")
if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/CMakeLists.txt")
    message(FATAL_ERROR
        "VST3 SDK not found in libs/vst3sdk. Run: git submodule update --init --recursive")
endif()

# SDK switches. Set as cache entries before add_subdirectory() so the SDK's
# own option() calls keep these values instead of their defaults. Override any
# of them at configure time with -D<NAME>=ON/OFF.

# Skip Steinberg's example plugins and example hosts; they only slow the build.
set(SMTG_ENABLE_VST3_PLUGIN_EXAMPLES OFF CACHE BOOL "Build VST3 SDK example plugins")
set(SMTG_ENABLE_VST3_HOSTING_EXAMPLES OFF CACHE BOOL "Build VST3 SDK example hosts")

# VSTGUI is the SDK's bundled GUI library; the editor in src/gui is built with
# it. When this is ON the SDK adds libs/vst3sdk/vstgui4 itself and defines the
# vstgui_support target that src/gui links. FORCE because the plugin cannot
# build without it: it also replaces an OFF left in the cache of a build
# folder configured before the editor existed, and it means
# -DSMTG_ENABLE_VSTGUI_SUPPORT=OFF has no effect.
set(SMTG_ENABLE_VSTGUI_SUPPORT ON CACHE BOOL "Build VSTGUI (required by the editor)" FORCE)

# VSTGUI is a submodule of the SDK. Without it the SDK's own error is unclear.
if(NOT EXISTS "${vst3sdk_SOURCE_DIR}/vstgui4/vstgui/CMakeLists.txt")
    message(FATAL_ERROR
        "VSTGUI not found in libs/vst3sdk/vstgui4. Run: git submodule update --init --recursive")
endif()

# After each build the SDK can run its validator (a VST3 conformance test
# suite) and generate moduleinfo.json (class metadata DAWs read without
# loading the plugin). Off to keep builds fast; turn ON to check the plugin
# against the VST3 spec (a failing validator fails the build).
set(SMTG_RUN_VST_VALIDATOR OFF CACHE BOOL "Run the VST3 validator after building")
set(SMTG_CREATE_MODULE_INFO OFF CACHE BOOL "Generate moduleinfo.json after building")

# The SDK can symlink the built plugin into ~/Library/Audio/Plug-Ins/VST3.
# Off because each build copies it to BURPSYNTH_VST3_INSTALL_DIR instead, and
# two copies with the same plugin ID would confuse DAWs.
set(SMTG_CREATE_PLUGIN_LINK OFF CACHE BOOL "Symlink the plugin into the user VST3 folder")

add_subdirectory(${vst3sdk_SOURCE_DIR} ${PROJECT_BINARY_DIR}/vst3sdk)

# Defines the smtg_* CMake functions used in src/plugin/CMakeLists.txt.
smtg_enable_vst3_sdk()

# --- Eigen ------------------------------------------------------------------
# Header-only linear algebra library, used for the tube state arrays in
# src/model. Lives in libs/eigen as a git submodule (see README).
set(eigen_SOURCE_DIR "${PROJECT_SOURCE_DIR}/libs/eigen")
if(NOT EXISTS "${eigen_SOURCE_DIR}/Eigen/Core")
    message(FATAL_ERROR
        "Eigen not found in libs/eigen. To add it: "
        "git submodule add https://gitlab.com/libeigen/eigen.git libs/eigen "
        "(after a fresh clone: git submodule update --init --recursive)")
endif()

# Header-only, so an INTERFACE target: linking it only adds the include path
# to whatever links it.
add_library(burp_eigen INTERFACE)

# SYSTEM: the compiler treats Eigen as a system header and hides its warnings,
# so our -Wall -Wextra -Wshadow ... flags only report on our own code.
target_include_directories(burp_eigen SYSTEM INTERFACE "${eigen_SOURCE_DIR}")

# --- BurpSynth --------------------------------------------------------------
# Shared compiler settings. Every BurpSynth target links this privately so
# they all compile with the same language version and warnings.
add_library(burpsynth_options INTERFACE)
target_compile_features(burpsynth_options INTERFACE cxx_std_20)
target_compile_options(burpsynth_options INTERFACE
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    # Flags float math silently promoted to double, a common DSP slowdown.
    -Wdouble-promotion
    # With the Xcode generator the SDK turns on -ffast-math for every target.
    # Fast math lets the compiler assume no value is ever NaN or infinite,
    # which would remove the engine's check that resets a blown-up simulation.
    # This comes later on the command line, so it wins for BurpSynth's code.
    -fno-fast-math
)

# Host-agnostic code: no VST3 SDK dependency, so it can be unit tested alone.
add_subdirectory(src/model)
add_subdirectory(src/audio_engine)

# Plugin editor, then the VST3 SDK glue: the only code that talks to the DAW.
add_subdirectory(src/gui)
add_subdirectory(src/plugin)

# Unit tests and command-line tools. enable_testing() has to be called in this
# top-level file so `ctest --test-dir build/<preset>` finds the tests.
if(BURPSYNTH_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools)
endif()
