# Summary
BurpSynth is a C++ VST3 instrument plugin. It is a physical modeling synth
which is a model of the human gastrointestinal system from the stomach up to the
mouth with the parameters and control tailored to produce burping and belching
sounds. It runs inside a DAW; there is no standalone app. Notes and parameter
automation come from the DAW.

# Directory Structure
/src - contains all source code for the:
    model - where the geometry and physics simulation are constructed and run.
        Pure C++, no VST3 SDK or host dependencies.
    audio_engine - host-agnostic DSP entry point that drives the model.
        No VST3 SDK dependencies.
    plugin - VST3 SDK glue (factory, processor, controller). The only code
        that talks to the DAW.
    gui - plugin editor. Currently a placeholder AppKit view (Objective-C++);
        GUI library not yet chosen
/libs - Where the dependencies will be held
    vst3sdk - Steinberg VST3 SDK (git submodule)
/assets - visual assets for GUI

# Tech Specs
Language - C++ primarily
Plugin format - VST3 only for now (AU possible later)
Plugin SDK - Steinberg VST3 SDK, used directly
Framework - No app frameworks like JUCE
Target OS - macOS 13+
C++ standard - C++20
Compiler - Apple Clang
Build system - CMake (see README)

# Convention
Don't ever build or run tests that is my job
Ask before running terminal commands
