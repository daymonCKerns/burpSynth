#pragma once

// Turbulence noise: the hiss a jet makes once it is fast enough.
//
// Above a critical Reynolds number the jet past a constriction becomes
// turbulent. The noise is modelled as a random pressure in series with the
// link, scaled by the jet's dynamic pressure:
//   p_noise = gain * kStrength * q * max(0, 1 - (Re_c / Re)^2) * xi
//   q  = 1/2 rho (U / A)^2,   Re = rho v d / mu   (math/Flow.h)
//   xi = white Gaussian noise with its lowest frequencies removed (one-pole
//        highpass near 300 Hz), from <random>
// Re_c = 1800.
//
// This is the breathy part of a burp. `gain` is the Breathiness parameter.

#include "model/math/Smooth.h"

#include <random>

namespace burp {

inline constexpr double kCriticalReynolds = 1800.0;

class Turbulence {
public:
    void prepare(double sampleRate);
    void setGain(double gain); // 0..1
    void reset();

    // Noise pressure (Pa) for a link carrying `flow` through `area`.
    // Advances the noise generator by one sample.
    double pressure(double density, double viscosity, double flow, double area);

private:
    static constexpr double kStrength = 0.5;
    static constexpr double kHighpassHz = 300.0;

    double gain_ = 0.0;
    OnePole lowpass_; // xi = white - lowpass(white)
    std::minstd_rand engine_;
    std::normal_distribution<double> normal_ { 0.0, 1.0 };
};

} // namespace burp
