// Turbulence: the jet-noise pressure from Turbulence.h.
//
// Shut openings and zero gain return 0 without touching the generator.

#include "model/acoustics/Turbulence.h"
#include "model/math/Acoustics.h"
#include "model/math/Flow.h"

#include <numbers>

namespace burp {

void Turbulence::prepare(double sampleRate)
{
    // One-pole lowpass at 300 Hz: tau = 1 / (2 pi f)
    lowpass_.setTime(1.0 / (2.0 * std::numbers::pi * kHighpassHz), 1.0 / sampleRate);
    reset();
}

void Turbulence::setGain(double gain)
{
    gain_ = gain;
}

void Turbulence::reset()
{
    lowpass_.reset(0.0);
    normal_.reset();
}

double Turbulence::pressure(double density, double viscosity, double flow, double area)
{
    if (gain_ <= 0.0 || area < kClosedArea) {
        return 0.0;
    }

    // xi = white - lowpass(white): highpassed Gaussian noise
    const double white = normal_(engine_);
    const double xi = white - lowpass_.next(white);

    const double reynolds = reynoldsNumber(density, viscosity, flow, area);
    if (reynolds <= kCriticalReynolds) {
        return 0.0;
    }

    // p = gain kStrength q (1 - (Re_c / Re)^2) xi
    const double ratio = kCriticalReynolds / reynolds;
    return gain_ * kStrength * dynamicPressure(density, flow, area) * (1.0 - ratio * ratio) * xi;
}

} // namespace burp
