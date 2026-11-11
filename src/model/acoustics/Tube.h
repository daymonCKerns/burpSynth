#pragma once

// 1D tube: a chain of cells with a link between each neighbouring pair.
// Used for the esophagus, pharynx, mouth and nasal cavity.
//
// For N cells of length dx and areas A_i:
//   cell i:      C_i dP_i/dt = U_(i-1) - U_i - dV_i/dt - wallFlow_i
//                C_i = A_i dx / (rho c^2),   dV_i/dt = dx dA_i/dt
//   link i:      L_i dU_i/dt = P_i - P_(i+1) - R_i U_i        (between cells i, i+1)
//                L_i = rho (dx/2) (1/A_i + 1/A_(i+1))
//                R_i = viscous resistance of the two half-cells
//   wall i:      m y'' + r y' + k y = P_i,  wallFlow_i = scale * S_i * y'
//                S_i = 2 sqrt(pi A_i) dx
//                m = 15 kg/m^2, r = 16000 Pa s/m, k = 3e6 Pa/m (per unit wall area)
//
// The state lives in Eigen arrays, so each update is one whole-array
// expression. Arrays are allocated once in prepare() for the largest cell
// count; configure() chooses how many are in use.
//
// The first and last cells are the tube's ends. Connectors (valves, the
// sphincter, radiation) read the end pressures, add their flow with
// addInletFlow/addOutletFlow, and must include the end's half-cell inertance
// in their own inertance.
//
// Per sample, BurpModel calls stepLinks() on every tube, routes connector
// flows, then calls stepCells() on every tube.

#include "model/geometry/AreaFunction.h"
#include "model/math/Gas.h"
#include "model/math/Units.h"

#include <Eigen/Core>

namespace burp {

class Tube {
public:
    // Allocates for a tube up to maxLength at this sample rate. Not real-time safe.
    void prepare(double maxLength, double sampleRate);

    // Sets the length, re-grids (cell count from cellCountForLength with the
    // current speed of sound), takes `shape` as the current areas and zeroes
    // all state. Call only while silent. Real-time safe.
    void configure(double length, const AreaFunction& shape);

    void setGas(double density, double soundSpeed, double viscosity);

    // Wall yielding scale: 0 = rigid, 1 = nominal soft tissue.
    void setWallScale(double scale);

    // Control rate: areas move in a straight line to `shape` over rampSamples
    // audio samples. Areas are floored at a small positive value.
    void setTargetShape(const AreaFunction& shape, int rampSamples);

    // Zeroes pressures, flows and wall motion; keeps the geometry.
    void reset();

    // Audio rate.
    void stepLinks(double dt); // interior flows from pressures
    void stepCells(double dt); // pressures from net flows; advances the area ramp

    double inletPressure() const;
    double outletPressure() const;
    void addInletFlow(double flow);  // flow entering the first cell
    void addOutletFlow(double flow); // flow leaving the last cell
    double inletArea() const;
    double outletArea() const;
    double inletHalfInertance() const;  // rho (dx/2) / A_first
    double outletHalfInertance() const; // rho (dx/2) / A_last

    int cellCount() const;
    double cellLength() const;
    double length() const;

    // Sized to the capacity; only the first cellCount() entries are in use.
    const Eigen::ArrayXd& pressures() const;
    const Eigen::ArrayXd& areas() const;

private:
    // Floored shape sampled at the cell centres (i + 0.5) / N into areaTarget_.
    void sampleTargets(const AreaFunction& shape);

    // Compliance, link inertance and resistance, and wall area from the areas.
    void updateCoefficients();

    static constexpr double kMinArea = 1.0e-8;         // m^2
    static constexpr double kWallMass = 15.0;          // kg/m^2
    static constexpr double kWallDamping = 16000.0;    // Pa s/m
    static constexpr double kWallStiffness = 3.0e6;    // Pa/m

    double sampleRate_ = 96000.0;
    int capacity_ = 0;
    int count_ = 0;
    double length_ = 0.0;
    double dx_ = 0.0;

    double density_ = gasDensity(kAir, kAtmosphericPressure, kBodyTemperature);
    double soundSpeed_ = speedOfSound(kAir, kBodyTemperature);
    double viscosity_ = kViscosity;
    double wallScale_ = 1.0;

    int rampRemaining_ = 0; // samples left in the area ramp

    // Per cell.
    Eigen::ArrayXd pressure_;     // P_i, Pa
    Eigen::ArrayXd netFlow_;      // inflow minus outflow this sample, m^3/s
    Eigen::ArrayXd area_;         // A_i, m^2
    Eigen::ArrayXd areaTarget_;   // m^2
    Eigen::ArrayXd areaStep_;     // ramp increment per sample, m^2
    Eigen::ArrayXd compliance_;   // C_i, m^3/Pa
    Eigen::ArrayXd wallArea_;     // S_i, m^2
    Eigen::ArrayXd wallVelocity_; // y'_i, m/s
    Eigen::ArrayXd wallPosition_; // y_i, m

    // Per interior link (first cellCount() - 1 entries).
    Eigen::ArrayXd flow_;         // U_i, m^3/s
    Eigen::ArrayXd inertance_;    // L_i, kg/m^4
    Eigen::ArrayXd resistance_;   // R_i, Pa s/m^3
};

} // namespace burp
