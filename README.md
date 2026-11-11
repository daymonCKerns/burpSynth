# burpSynth
Physical modeling synthesizer of human anatomy with the parameters and control 
tailored to burping/belching. Built as a monophonic VST3 instrument plugin for
macOS.

The model runs from the stomach up to the mouth and nose: the stomach pushes
gas through the lower esophageal sphincter, up the esophagus, and through the
upper esophageal sphincter, whose vibration is the sound source. The pharynx,
mouth and nasal tract then shape the sound. A note starts the belch reflex.

This is the first full implementation of the model, the engine, the plugin
parameters and a basic editor (one slider per parameter). The physical
constants are starting values that still need tuning by ear and with the
tests. The design is in `docs/implementation_plan.md`.

# Version
v0.0

# Building
## Requirements
- macOS 13+
- Full Xcode (from the App Store), set as the active toolchain. The VST3 SDK
  checks the Xcode version, so the command line tools alone are not enough:
  `sudo xcode-select -s /Applications/Xcode.app/Contents/Developer`
- CMake 3.31+ (`brew install cmake`). 3.31 is only needed for the presets
  file; the build itself works with 3.25+.
- The VST3 SDK and Eigen git submodules (below).

## VST3 SDK
The Steinberg VST3 SDK lives in `libs/vst3sdk` as a git submodule. After
cloning, fetch it (and its own submodules, including VSTGUI) with:

```sh
git submodule update --init --recursive
```

## Eigen
Eigen, a header-only linear algebra library used for the tube state arrays,
lives in `libs/eigen` as a git submodule. The command above fetches it after
cloning. To add it to a checkout that does not have it yet:

```sh
git submodule add https://gitlab.com/libeigen/eigen.git libs/eigen
```

## Build
Using presets:

```sh
cmake --preset debug
cmake --build --preset debug
```

| Preset | Use |
|---|---|
| `debug` | Day-to-day development, debug symbols and asserts on |
| `release` | Optimized; use for checking real-time performance |
| `release-universal` | Optimized, runs on both Apple Silicon and Intel Macs |
| `xcode` | Generates an Xcode project in `build/xcode` (build with `--preset xcode-debug`) |

Without presets:

```sh
cmake -S . -B build/debug -DCMAKE_BUILD_TYPE=Debug
cmake --build build/debug
```

## Output
The plugin bundle is written to `build/<preset>/VST3/<Debug|Release>/BurpSynth.vst3`.
Every build then signs it and copies it to `/Library/Audio/Plug-Ins/VST3/`, the
system-wide folder where DAWs look for plugins, replacing the previous copy.
After rebuilding, rescan plugins in the DAW (or restart it if BurpSynth was
already loaded).

The test programs and tools are written to `build/<preset>/bin/<Debug|Release>/`.

## Tests
After building, run every test with:

```sh
ctest --test-dir build/debug --output-on-failure
```

Each test is a plain program that prints what it measured and fails if a
check fails. The printed values (tube resonances, the sphincter's pitch at a
few tensions, a burp's peak level and pitch) are there for tuning the
physical constants, not only for pass or fail. Run one test directly, e.g.
`build/debug/bin/Debug/test_sphincter`, to see its output.

## Rendering a burp
`render_wav` plays one burp through the engine and writes it to a WAV file, to
listen without a DAW:

```sh
build/debug/bin/Debug/render_wav [output.wav] [note] [velocity] [hold seconds]
```

The defaults are `burp.wav`, note 45, velocity 0.8 and a 1 second hold. It
renders at 48 kHz: note on, hold, note off, then one more second for the tail.
It prints the peak level and the file path.

## Trying it
In a DAW rescan plugins, add BurpSynth to a MIDI track, and play notes.
- A note starts a burp at that pitch. Hold it to keep the burp going; release
  it to end the burp.
- BurpSynth plays one burp at a time. A note played while another is held
  glides to the new pitch with a pressure accent instead of starting a new
  burp. Releasing it glides back to the note still held.
- The mod wheel sets Abdominal Pressure (how hard the stomach pushes). The
  pitch wheel bends the pitch by up to the Bend Range parameter.
- The editor window has one slider per parameter. Every parameter can also be
  automated from the DAW.

## Build options
| Option | Default | Effect |
|---|---|---|
| `BURPSYNTH_BUNDLE_ID` | `com.burpsynth.vst3` | macOS bundle identifier |
| `BURPSYNTH_VST3_INSTALL_DIR` | `/Library/Audio/Plug-Ins/VST3` | Folder each build copies the plugin into |
| `BURPSYNTH_BUILD_TESTS` | `ON` | Build the unit tests in `tests/` and the tools in `tools/` |
| `SMTG_CREATE_PLUGIN_LINK` | `OFF` | Symlink the plugin into `~/Library/Audio/Plug-Ins/VST3/` (off; the copy above replaces it) |
| `SMTG_RUN_VST_VALIDATOR` | `OFF` | Run Steinberg's VST3 validator after each build |
| `SMTG_CREATE_MODULE_INFO` | `OFF` | Generate `moduleinfo.json` after each build |
| `SMTG_ENABLE_VSTGUI_SUPPORT` | `ON` | Build VSTGUI, the SDK's GUI library. The editor needs it, so it is always on |

Pass options at configure time, e.g. `cmake --preset debug -DBURPSYNTH_BUILD_TESTS=OFF`.
