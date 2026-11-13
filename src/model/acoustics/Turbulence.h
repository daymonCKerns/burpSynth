#pragma once

// Turbulence noise: the hiss a jet makes once it is fast enough.
//
// Above a critical Reynolds number the jet past a constriction becomes
// turbulent. The noise is modelled as a random pressure in series with the
// link, scaled by the jet's dynamic pressure:
//   p_noise = gain * kStrength * q * max(0, 1 - (Re_c / Re)^2) * xi
//   q  = 1/2 rho (U / A)^2,   Re = rho v d / mu   (math/Flow.h)
//   xi = band-limited Gaussian noise with unit variance:
//        xi = (lowpass_hi(white) - lowpass_lo(white)) / sigma
//        two one-pole lowpasses of the same white noise (from <random>),
//        at kLowpassHz and kHighpassHz; their difference is a bandpass.
//        sigma^2 = V_hi + V_lo - 2 C,  V = (1 - a) / (1 + a),
//        C = (1 - a_hi)(1 - a_lo) / (1 - a_hi a_lo)
// Re_c = 1800.
//
// Jet noise falls off above a few kHz. Unfiltered white noise did not: the
// radiated sound (the derivative of the lip flow) tilted it further toward
// the treble, where it rang the tract's high resonances into a whistle
// (a peak at 13 kHz) and swamped the voice.
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
    // Set by measurement (tools/scenarios/breathiness_*): at Breathiness 0.3 the
    // noise sits about 5 dB under the voice, at 1 a few dB over it.
    static constexpr double kStrength = 0.15;
    static constexpr double kHighpassHz = 300.0;
    static constexpr double kLowpassHz = 2000.0;

    double gain_ = 0.0;
    double scale_ = 1.0; // 1 / sigma
    OnePole upper_;      // lowpass at kLowpassHz
    OnePole lower_;      // lowpass at kHighpassHz
    std::minstd_rand engine_;
    std::normal_distribution<double> normal_ { 0.0, 1.0 };
};

} // namespace burp
