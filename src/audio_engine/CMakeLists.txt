# DSP entry point that renders audio. It owns no audio device and knows
# nothing about VST3: the plugin processor in src/plugin calls into it.
add_library(burp_engine STATIC
    Engine.cpp
)

# Lets any code that links burp_engine write #include "audio_engine/Engine.h".
target_include_directories(burp_engine PUBLIC ${PROJECT_SOURCE_DIR}/src)

# PUBLIC: Engine.h will include model headers, so anything linking the engine
# gets burp_model too. PRIVATE: compiler settings stay local to this target.
target_link_libraries(burp_engine
    PUBLIC burp_model
    PRIVATE burpsynth_options
)
