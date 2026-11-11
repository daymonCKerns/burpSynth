#pragma once

// Valve: a short channel whose opening area is set by a controller. It does
// not vibrate. Used for the lower esophageal sphincter (LES) and the velum.
//
// A Link with area-dependent coefficients:
//   L dU/dt = P_up - P_down - (R_visc + R_bern) U
//   L      = rho l / A + neighbour half-cell inertances
//   R_visc = 8 pi mu l / A^2
//   R_bern = rho |U| / (2 A^2)        (jet loss, linearised on the last flow)
// Below kClosedArea the valve is shut: flow is forced to zero.
//
// The area ramps linearly to its target, one step per audio sample.

#include "model/math/Gas.h"
#include "model/math/Smooth.h"
#include "model/math/Units.h"

namespace burp {

class Valve {
public:
    void setChannelLength(double length); // m
    void setGas(double density, double viscosity);

    // Sum of the half-cell inertances of the two cells this valve joins.
    void setNeighbourInertance(double inertance);

    void setTargetArea(double area, int rampSamples); // m^2
    void reset();                                     // closed, no flow

    // Advances the area ramp and the flow. Returns the flow (m^3/s).
    double step(double dt, double pressureUp, double pressureDown);

    double flow() const;
    double area() const;

private:
    double channelLength_ = 0.01; // m
    double density_ = gasDensity(kAir, kAtmosphericPressure, kBodyTemperature);
    double viscosity_ = kViscosity;
    double neighbourInertance_ = 0.0; // kg/m^4
    Ramp opening_;                    // m^2
    double flow_ = 0.0;               // m^3/s
};

} // namespace burp
