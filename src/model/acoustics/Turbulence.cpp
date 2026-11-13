// Turbulence: the jet-noise pressure from Turbulence.h.
//
// Shut openings and zero gain return 0 without touching the generator.

#include "model/acoustics/Turbulence.h"
#include "model/math/Acoustics.h"
#include "model/math/Flow.h"

#include <cmath>
#include <numbers>

namespace burp {

void Turbulence::prepare(double sampleRate)
{
    // One-pole lowpasses: tau = 1 / (2 pi f)
    const double dt = 1.0 / sampleRate;
    const double upperTime = 1.0 / (2.0 * std::numbers::pi * kLowpassHz);
    const double lowerTime = 1.0 / (2.0 * std::numbers::pi * kHighpassHz);
    upper_.setTime(upperTime, dt);
    lower_.setTime(lowerTime, dt);

    // sigma^2 = V_hi + V_lo - 2 C  (Turbulence.h)
    const double aHi = onePoleCoefficient(upperTime, dt);
    const double aLo = onePoleCoefficient(lowerTime, dt);
    const double variance = (1.0 - aHi) / (1.0 + aHi) + (1.0 - aLo) / (1.0 + aLo)
                            - 2.0 * (1.0 - aHi) * (1.0 - aLo) / (1.0 - aHi * aLo);
    scale_ = 1.0 / std::sqrt(variance);
    reset();
}

void Turbulence::setGain(double gain)
{
    gain_ = gain;
}

void Turbulence::reset()
{
    upper_.reset(0.0);
    lower_.reset(0.0);
    normal_.reset();
}

double Turbulence::pressure(double density, double viscosity, double flow, double area)
{
    if (gain_ <= 0.0 || area < kClosedArea) {
        return 0.0;
    }

    // xi = (lowpass_hi(white) - lowpass_lo(white)) / sigma: band-limited, unit variance
    const double white = normal_(engine_);
    const double xi = scale_ * (upper_.next(white) - lower_.next(white));

    const double reynolds = reynoldsNumber(density, viscosity, flow, area);
    if (reynolds <= kCriticalReynolds) {
        return 0.0;
    }

    // p = gain kStrength q (1 - (Re_c / Re)^2) xi
    const double ratio = kCriticalReynolds / reynolds;
    return gain_ * kStrength * dynamicPressure(density, flow, area) * (1.0 - ratio * ratio) * xi;
}

} // namespace burp
