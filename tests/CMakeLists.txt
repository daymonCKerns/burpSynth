# Unit tests. Each is a plain command-line program built from one .cpp file:
# it prints what it measured and returns the number of failed checks, which
# CTest reports as pass (0) or fail. Run them all with
# `ctest --test-dir build/<preset> --output-on-failure`.
set(BURPSYNTH_TESTS
    test_math
    test_tube
    test_reservoir
    test_sphincter
    test_geometry
    test_model
    test_engine
)

foreach(test_name IN LISTS BURPSYNTH_TESTS)
    add_executable(${test_name} ${test_name}.cpp)

    # Lets tests write #include "Check.h" and "Spectrum.h" (shared helpers in
    # this folder).
    target_include_directories(${test_name} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # burp_engine brings burp_model and Eigen with it; burpsynth_options gives
    # the same C++20 + warning settings as the plugin code.
    target_link_libraries(${test_name} PRIVATE burp_engine burpsynth_options)

    # Registers the program with CTest under its own name.
    add_test(NAME ${test_name} COMMAND ${test_name})
endforeach()
