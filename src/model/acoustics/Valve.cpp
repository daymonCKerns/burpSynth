// Valve: the controlled opening from Valve.h (LES and velum).

#include "model/acoustics/Valve.h"
#include "model/math/Acoustics.h"
#include "model/math/Flow.h"
#include "model/math/Integrate.h"

namespace burp {

void Valve::setChannelLength(double length)
{
    channelLength_ = length;
}

void Valve::setGas(double density, double viscosity)
{
    density_ = density;
    viscosity_ = viscosity;
}

void Valve::setNeighbourInertance(double inertance)
{
    neighbourInertance_ = inertance;
}

void Valve::setTargetArea(double target, int rampSamples)
{
    opening_.setTarget(target, rampSamples);
}

void Valve::reset()
{
    opening_.reset(0.0);
    flow_ = 0.0;
}

double Valve::step(double dt, double pressureUp, double pressureDown)
{
    const double a = opening_.next();
    if (a < kClosedArea) {
        flow_ = 0.0;
        return 0.0;
    }

    // L = rho l / A + neighbour half-cells
    const double totalInertance = inertance(density_, channelLength_, a) + neighbourInertance_;
    // R = 8 pi mu l / A^2 + rho |U| / (2 A^2)
    const double totalResistance =
        viscousResistance(viscosity_, channelLength_, a) + bernoulliResistance(density_, flow_, a);

    flow_ = stepInertance(flow_, totalInertance, totalResistance, pressureUp - pressureDown, dt);
    return flow_;
}

double Valve::flow() const
{
    return flow_;
}

double Valve::area() const
{
    return opening_.value();
}

} // namespace burp
