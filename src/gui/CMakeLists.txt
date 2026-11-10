# Plugin editor. Placeholder UI drawn with macOS AppKit directly (hence the
# Objective-C++ .mm file) until a GUI library is chosen.
add_library(burp_gui STATIC
    HelloWorldView.mm
)

# Lets any code that links burp_gui write #include "gui/HelloWorldView.h".
target_include_directories(burp_gui PUBLIC ${PROJECT_SOURCE_DIR}/src)

# sdk: CPluginView base class; PUBLIC because HelloWorldView.h includes SDK
# headers. AppKit: macOS's UI framework (NSView, NSTextField).
target_link_libraries(burp_gui
    PUBLIC sdk "-framework AppKit"
    PRIVATE burpsynth_options
)

# ARC (Automatic Reference Counting): frees Objective-C objects automatically,
# so the .mm file needs no manual retain/release calls.
target_compile_options(burp_gui PRIVATE -fobjc-arc)
