# Command-line tools for working on the sound without a DAW.

# render_wav: plays one burp through the engine and writes it to a WAV file.
# WavWriter.h sits next to render_wav.cpp, so its #include needs no extra path.
add_executable(render_wav render_wav.cpp)

# burp_engine brings burp_model and Eigen with it; burpsynth_options gives the
# same C++20 + warning settings as the plugin code.
target_link_libraries(render_wav PRIVATE burp_engine burpsynth_options)
