// Reservoir: the stomach's gas-law and wall-balance solution from Reservoir.h.
//
// The positive root of the quadratic is used in its rationalised form,
//   V = 2 n R T / (b + sqrt(b^2 + 4 E n R T)),
// which equals (-b + sqrt(b^2 + 4 E n R T)) / (2 E), has no cancellation
// when E n R T << b^2, and reduces to V = n R T / b when E = 0.

#include "model/acoustics/Reservoir.h"
#include "model/math/Gas.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cmath>

namespace burp {

void Reservoir::setWallElastance(double elastance)
{
    elastance_ = elastance;
    solve();
}

void Reservoir::setExternalPressure(double externalPressure)
{
    externalPressure_ = externalPressure;
    solve();
}

void Reservoir::fill(double gasVolume)
{
    // n = P_atm V / (R T)
    moles_ = gasMoles(kAtmosphericPressure, gasVolume, kBodyTemperature);
    solve();
}

void Reservoir::reset()
{
    moles_ = 0.0;
    outflow_ = 0.0;
    solve();
}

double Reservoir::pressure() const
{
    // Linear fade to zero below the residual volume.
    if (volume_ < kResidualVolume) {
        return gasPressure_ * volume_ / kResidualVolume;
    }
    return gasPressure_;
}

double Reservoir::volume() const
{
    return volume_;
}

double Reservoir::moles() const
{
    return moles_;
}

void Reservoir::addOutflow(double flow)
{
    outflow_ += flow;
}

void Reservoir::step(double dt)
{
    // dn/dt = -U (P_atm + P) / (R T)
    moles_ -= outflow_ * (kAtmosphericPressure + gasPressure_) / (kGasConstant * kBodyTemperature)
              * dt;
    moles_ = std::max(moles_, 0.0);
    outflow_ = 0.0;
    solve();
}

void Reservoir::solve()
{
    // E V^2 + b V - n R T = 0
    const double b = kAtmosphericPressure + externalPressure_;
    const double nrt = moles_ * kGasConstant * kBodyTemperature;
    volume_ = 2.0 * nrt / (b + std::sqrt(b * b + 4.0 * elastance_ * nrt));

    // P = P_ext + E V
    gasPressure_ = externalPressure_ + elastance_ * volume_;
}

} // namespace burp
