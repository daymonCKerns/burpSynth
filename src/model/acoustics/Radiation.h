#pragma once

// Radiation: how sound leaves an opening (lips or nostrils) into open air.
//
// The air outside loads the opening like a resistance R_r in parallel with
// an inertance L_r (Flanagan; values from math/Acoustics.h). With U the flow
// through the opening and i the part of it passing through L_r:
//   P_rad     = R_r (U - i)
//   L_r di/dt = P_rad
//   L dU/dt   = P_cell - P_rad         L = half-cell inertance of the tube end
// Both states are stepped together with backward Euler (a 2x2 linear system
// solved in closed form), because R_r / L_r grows without limit as the
// opening closes and an explicit step would blow up.
//
// Below kClosedArea the opening is shut: no flow.
//
// The sound pressure heard at distance r is rho / (4 pi r) * dU/dt, so the
// model's output is built from flowDerivative().

#include "model/math/Gas.h"
#include "model/math/Units.h"

namespace burp {

class Radiation {
public:
    void setGas(double density, double soundSpeed);
    void setArea(double area); // m^2, control rate

    // Half-cell inertance of the tube end this opening terminates.
    void setNeighbourInertance(double inertance);

    void reset();

    // One sample. `pressure` is the tube's outlet pressure. Returns the flow
    // leaving through the opening (m^3/s).
    double step(double dt, double pressure);

    double flow() const;
    double flowDerivative() const; // dU/dt over the last step, m^3/s^2

private:
    // R_r and L_r for the current area and gas.
    void updateLoad();

    static constexpr double kMinInertance = 1.0e-6; // kg/m^4, floor for L

    double density_ = gasDensity(kAir, kAtmosphericPressure, kBodyTemperature);
    double soundSpeed_ = speedOfSound(kAir, kBodyTemperature);
    double area_ = 0.0;                         // m^2
    double loadResistance_ = 0.0;               // R_r, Pa s/m^3
    double loadInertance_ = 1.0;                // L_r, kg/m^4
    double neighbourInertance_ = kMinInertance; // L, kg/m^4
    double flow_ = 0.0;                         // U, m^3/s
    double inductorFlow_ = 0.0;                 // i, m^3/s
    double flowDerivative_ = 0.0;               // m^3/s^2
};

} // namespace burp
