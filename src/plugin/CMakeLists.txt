# VST3 glue: the SDK-facing layer a DAW loads. It translates host calls
# (audio processing, parameter changes, state save/load) into calls on
# burp_engine. smtg_add_vst3plugin() comes from the VST3 SDK: it creates a
# shared library packaged as BurpSynth.vst3, the bundle format DAWs scan for.
smtg_add_vst3plugin(BurpSynth
    PluginIds.h
    PluginProcessor.h
    PluginProcessor.cpp
    PluginController.h
    PluginController.cpp
    PluginEntry.cpp
)

# Lets plugin files write #include "plugin/PluginIds.h".
target_include_directories(BurpSynth PRIVATE ${PROJECT_SOURCE_DIR}/src)

# sdk: the SDK's base classes (AudioEffect, EditController) and plugin factory.
# burp_engine brings burp_model with it (see audio_engine/CMakeLists.txt).
# burp_gui: the editor view the controller hands to the DAW.
target_link_libraries(BurpSynth PRIVATE
    sdk
    burp_engine
    burp_gui
    burpsynth_options
)

# Values the SDK writes into the bundle's Info.plist, which macOS and DAWs
# read to identify the plugin.
smtg_target_set_bundle(BurpSynth
    BUNDLE_IDENTIFIER "${BURPSYNTH_BUNDLE_ID}"
    COMPANY_NAME "BurpSynth"
)

# The SDK only fills in Info.plist when using the Xcode generator; with
# Makefiles CMake falls back to its app template (empty identifier, type APPL).
# Our template marks the bundle as a plugin (type BNDL) for every generator,
# and CMake substitutes the ${MACOSX_BUNDLE_*} values from these properties.
set_target_properties(BurpSynth PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${BURPSYNTH_BUNDLE_ID}"
    MACOSX_BUNDLE_BUNDLE_NAME "BurpSynth"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)

# Runs after every build (a custom target with no outputs is always out of
# date): sign the bundle, then copy it to where DAWs look for plugins.
#
# Signing: Apple Silicon refuses to load code whose signature doesn't match
# the bundle, and the signature covers Info.plist. The SDK only signs after a
# relink, but with Makefiles CMake rewrites Info.plist at configure time (e.g.
# after a version bump) without relinking, leaving a stale signature that
# macOS reports as "damaged". "--sign -" is an ad-hoc signature: valid on this
# Mac, not for distribution.
#
# Copying: the old copy is deleted first so files removed from the build don't
# linger, and so a DAW that has the old plugin loaded keeps its file intact
# instead of having it overwritten underneath it. ditto is macOS's
# bundle-aware copy and keeps the signature valid.
get_target_property(BURPSYNTH_BUNDLE_PATH BurpSynth SMTG_PLUGIN_PACKAGE_PATH)
set(BURPSYNTH_INSTALLED_PATH "${BURPSYNTH_VST3_INSTALL_DIR}/BurpSynth.vst3")
add_custom_target(BurpSynth_deploy ALL
    COMMAND codesign --force --sign - "${BURPSYNTH_BUNDLE_PATH}"
    COMMAND ${CMAKE_COMMAND} -E rm -rf "${BURPSYNTH_INSTALLED_PATH}"
    COMMAND ditto "${BURPSYNTH_BUNDLE_PATH}" "${BURPSYNTH_INSTALLED_PATH}"
    COMMENT "Signing BurpSynth.vst3 and copying it to ${BURPSYNTH_VST3_INSTALL_DIR}"
    VERBATIM
)
add_dependencies(BurpSynth_deploy BurpSynth)
