# Physics simulation of the stomach-to-mouth tract. Pure C++ with no VST3 SDK
# dependency (Eigen is its only library) so it can be unit tested without the
# SDK or a DAW. Headers-only parts (math/, Cell.h, Link.h, AreaFunction.h,
# Peristalsis.h, TubeLaw.h) need no entry here.
add_library(burp_model STATIC
    # Wires the parts together and runs the per-sample and control updates.
    BurpModel.cpp

    # Acoustic parts: tubes, stomach, valves (LES, velum), the upper sphincter
    # (UES), lip and nostril radiation, and jet noise.
    acoustics/Tube.cpp
    acoustics/Reservoir.cpp
    acoustics/Valve.cpp
    acoustics/Radiation.cpp
    acoustics/Turbulence.cpp
    acoustics/Sphincter.cpp

    # Rest shapes of each tract section, and jaw/tongue/lip articulation.
    geometry/Anatomy.cpp
    geometry/Articulation.cpp

    # Belch reflex timeline and pitch control of the UES.
    control/Reflex.cpp
    control/PitchControl.cpp
)

# Lets any code that links burp_model write #include "model/BurpModel.h".
target_include_directories(burp_model PUBLIC ${PROJECT_SOURCE_DIR}/src)

# PUBLIC burp_eigen: Tube.h includes Eigen, so anything including model
# headers needs Eigen's include path too. PRIVATE burpsynth_options: shared
# C++20 + warning settings from the top-level CMakeLists.txt.
target_link_libraries(burp_model
    PUBLIC burp_eigen
    PRIVATE burpsynth_options
)
