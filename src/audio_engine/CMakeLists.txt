# DSP entry point that renders audio. It owns no audio device and knows
# nothing about VST3: the plugin processor in src/plugin calls into it.
# NoteStack.h is header-only and needs no entry here.
add_library(burp_engine STATIC
    # Parameter table: IDs, names, units, ranges and defaults.
    Params.cpp
    # Brings the oversampled model output down to the host sample rate.
    Decimator.cpp
    # Converts parameter values to the model's SI-unit controls.
    ControlMapping.cpp
    # Note handling, rendering and output protection.
    Engine.cpp
)

# Lets any code that links burp_engine write #include "audio_engine/Engine.h".
target_include_directories(burp_engine PUBLIC ${PROJECT_SOURCE_DIR}/src)

# PUBLIC: Engine.h includes model headers, so anything linking the engine
# gets burp_model (and through it Eigen) too. PRIVATE: compiler settings stay
# local to this target.
target_link_libraries(burp_engine
    PUBLIC burp_model
    PRIVATE burpsynth_options
)
