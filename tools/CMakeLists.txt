# Command-line tools for working on the sound without a DAW.

# render_wav: plays one burp through the engine and writes it to a WAV file.
# WavWriter.h sits next to render_wav.cpp, so its #include needs no extra path.
add_executable(render_wav render_wav.cpp)

# burp_engine brings burp_model and Eigen with it; burpsynth_options gives the
# same C++20 + warning settings as the plugin code.
target_link_libraries(render_wav PRIVATE burp_engine burpsynth_options)

# run_scenario: plays scripted scenarios (tools/scenarios/*.txt) through the
# engine and writes a WAV, a 1 ms trace (CSV) and a summary for each, for
# finding and comparing sound problems. Same dependencies as render_wav.
add_executable(run_scenario run_scenario.cpp)
target_link_libraries(run_scenario PRIVATE burp_engine burpsynth_options)
