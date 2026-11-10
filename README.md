# burpSynth
Physical modeling synthesizer of human anatomy with the parameters and control 
tailored to burping/belching. Built as a VST3 instrument plugin for macOS.

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

## VST3 SDK
The Steinberg VST3 SDK lives in `libs/vst3sdk` as a git submodule. After
cloning, fetch it (and its own submodules) with:

```sh
git submodule update --init --recursive
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

## Trying it
BurpSynth currently passes audio straight through and its window shows
"hello world". It registers as an audio effect, so in a DAW rescan plugins,
then add BurpSynth as an effect on an audio track and open its editor.

## Build options
| Option | Default | Effect |
|---|---|---|
| `BURPSYNTH_BUNDLE_ID` | `com.burpsynth.vst3` | macOS bundle identifier |
| `BURPSYNTH_VST3_INSTALL_DIR` | `/Library/Audio/Plug-Ins/VST3` | Folder each build copies the plugin into |
| `SMTG_CREATE_PLUGIN_LINK` | `OFF` | Symlink the plugin into `~/Library/Audio/Plug-Ins/VST3/` (off; the copy above replaces it) |
| `SMTG_RUN_VST_VALIDATOR` | `OFF` | Run Steinberg's VST3 validator after each build |
| `SMTG_CREATE_MODULE_INFO` | `OFF` | Generate `moduleinfo.json` after each build |
| `SMTG_ENABLE_VSTGUI_SUPPORT` | `OFF` | Build VSTGUI, the SDK's GUI library |

Pass options at configure time, e.g. `cmake --preset debug -DSMTG_CREATE_PLUGIN_LINK=OFF`.
