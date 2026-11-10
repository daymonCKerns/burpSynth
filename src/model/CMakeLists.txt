# Physics simulation of the stomach-to-mouth tract. Pure C++ with no
# dependencies so it can be unit tested without the VST3 SDK or a DAW.
add_library(burp_model STATIC
    BurpModel.cpp
)

# Lets any code that links burp_model write #include "model/BurpModel.h".
target_include_directories(burp_model PUBLIC ${PROJECT_SOURCE_DIR}/src)

# Shared C++20 + warning settings from the top-level CMakeLists.txt.
target_link_libraries(burp_model PRIVATE burpsynth_options)
