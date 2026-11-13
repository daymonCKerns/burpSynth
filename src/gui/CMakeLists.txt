# Plugin editor: one labelled slider per parameter, built in code with VSTGUI
# (the GUI library bundled with the VST3 SDK). No resource files or bitmaps.
# HelloWorldView.h/.mm are the old AppKit placeholder and are no longer built.
add_library(burp_gui STATIC
    Editor.cpp
    # The body cross-section: live parameters as shapes, gas moving through.
    BodyView.cpp
)

# Lets any code that links burp_gui write #include "gui/Editor.h".
target_include_directories(burp_gui PUBLIC ${PROJECT_SOURCE_DIR}/src)

# SYSTEM: the compiler treats the SDK and VSTGUI as system headers and hides
# their warnings, so -Wall -Wextra ... only report on our own code. The same
# folders also arrive through the sdk and vstgui_support targets below;
# listing them here as SYSTEM is what switches them from -I to -isystem.
target_include_directories(burp_gui SYSTEM PRIVATE
    ${vst3sdk_SOURCE_DIR}
    ${vst3sdk_SOURCE_DIR}/vstgui4
)

# sdk: EditController and the pluginterfaces headers; PUBLIC because Editor.h
#   includes pluginterfaces/gui/iplugview.h.
# vstgui_support: VSTGUI itself plus the SDK's VSTGUIEditor base class
#   (public.sdk/source/vst/vstguieditor.cpp is compiled into this target by
#   the SDK's SMTG_VstGuiSupport.cmake, so it is not listed here). It only
#   exists when the top-level CMakeLists.txt sets SMTG_ENABLE_VSTGUI_SUPPORT
#   ON. PRIVATE because Editor.h keeps VSTGUI types out of the plugin layer;
#   the plugin still links it, since a static library passes its link
#   dependencies on to whatever links it.
# burp_engine: paramTable() from audio_engine/Params.h, which the sliders are
#   built from.
# burpsynth_options: the shared C++ version and warning flags.
target_link_libraries(burp_gui
    PUBLIC sdk
    PRIVATE vstgui_support burp_engine burpsynth_options
)
