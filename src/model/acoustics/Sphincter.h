#pragma once

// Upper esophageal sphincter (UES): the vibrating valve that makes the sound.
//
// Two coupled masses, a lower edge (1, esophagus side) and an upper edge
// (2, pharynx side), after the symmetric two-mass vocal-fold model of
// Steinecke & Herzel, J. Acoust. Soc. Am. 97 (1995). Two masses are needed
// because the phase lag between the edges is what feeds energy into the
// motion; one mass driven by Bernoulli pressure alone cannot sustain itself.
//
// Geometry. x_i is the displacement of mass i, l the slit length:
//   a_i   = a_0 + 2 l x_i                     opening area at edge i
//   a_min = min(a_1, a_2)
//   a_0   = lerp(kHeldShutArea, restArea, openness)
// openness comes from the reflex: 0 = muscle tone holds it shut, 1 = released.
//
// Motion, for i = 1, 2 and j the other mass:
//   m_i x_i'' + r_i x_i' + k_i x_i + k_c (x_i - x_j) + collision_i = l d_i P_i
//   collision_i = c_i a_i / (2 l)  if a_i < 0, else 0      (c_i = 3 k_i)
//
// Pressure on each edge, with dP = P_up - P_down:
//   both open:            P_1 = P_down + dP (1 - (a_min / a_1)^2),  P_2 = P_down
//   only edge 2 closed:   P_1 = P_up,                               P_2 = P_down
//   edge 1 closed:        P_1 = kClosedFaceFraction * P_up,         P_2 = P_down
// The last line is an addition to Steinecke & Herzel: pressure on the
// upstream face lets enough pressure blow a lightly closed sphincter open.
//
// Flow through the slit (a Link with Bernoulli loss and jet noise):
//   L dU/dt = P_up - P_down - R_bern U + p_noise
//   L = rho (d_1 + d_2) / a_min + neighbour half-cell inertances
//   R_bern = rho |U| / (2 a_min^2)
// Shut (flow forced to zero) when a_min < kClosedArea.
//
// Reference constants (Steinecke & Herzel's vocal-fold values, in SI):
//   m_1 = 1.25e-4 kg   m_2 = 2.5e-5 kg
//   k_1 = 80 N/m       k_2 = 8 N/m       k_c = 25 N/m
//   d_1 = 2.5e-3 m     d_2 = 5e-4 m      l = 1.4e-2 m
// Scaled by the settings, with Q the tension factor of Ishizaka & Flanagan:
//   m_i = massScale * m_i0 / Q
//   k_i = massScale * k_i0 * Q        (k_c likewise)
//   r_1 = r_2 = 2 * dampingRatio * sqrt(m_1 k_1)
// Both masses share one damping constant, as in Steinecke & Herzel
// (r = 0.02 N s/m, a ratio of 0.1 for the lower mass). The light upper mass
// is therefore heavily damped (ratio about 0.7), which keeps it from ringing
// on its own.
// The natural frequency is then Q * kSphincterReferenceHz whatever the mass
// scale. Raising massScale makes the tissue harder to move, which is the same
// as lowering the driving pressure.
//
// Settings are smoothed inside (a few ms) so control-rate updates do not step.

#include "model/acoustics/Turbulence.h"
#include "model/math/Integrate.h"
#include "model/math/Smooth.h"
#include "model/math/Units.h"

namespace burp {

inline constexpr double kSphincterReferenceHz = 125.0; // natural frequency at Q = 1
inline constexpr double kHeldShutArea = -2.0e-5;       // m^2, rest area while held shut
inline constexpr double kClosedFaceFraction = 0.5;

struct SphincterSettings {
    double massScale = 3.0;
    double dampingRatio = 0.12;
    double restArea = 5.0e-6; // m^2 when released; slightly negative = pressed
    double tension = 1.0;     // Q
    double openness = 0.0;    // 0..1 from the reflex
};

class Sphincter {
public:
    void prepare(double sampleRate);
    void setGas(double density, double viscosity);

    // Sum of the half-cell inertances of the esophagus outlet and pharynx inlet.
    void setNeighbourInertance(double inertance);

    void setSettings(const SphincterSettings& settings); // control rate
    void setTurbulenceGain(double gain);                 // 0..1
    void reset();                                        // at rest, held shut, no flow

    // One sample: flow from the current opening, then the masses.
    // Returns the flow (m^3/s).
    double step(double dt, double pressureUp, double pressureDown);

    double flow() const;
    double minArea() const;           // a_min, m^2 (may be negative when shut)
    double lowerDisplacement() const; // x_1, m; drives the pitch tracker

private:
    // Steinecke & Herzel (1995) reference values.
    static constexpr double kLowerMass = 1.25e-4;       // m_1, kg
    static constexpr double kUpperMass = 2.5e-5;        // m_2, kg
    static constexpr double kLowerStiffness = 80.0;     // k_1, N/m
    static constexpr double kUpperStiffness = 8.0;      // k_2, N/m
    static constexpr double kCouplingStiffness = 25.0;  // k_c, N/m
    static constexpr double kLowerDepth = 2.5e-3;       // d_1, m
    static constexpr double kUpperDepth = 5.0e-4;       // d_2, m
    static constexpr double kSlitLength = 1.4e-2;       // l, m
    static constexpr double kCollisionFactor = 3.0;     // c_i / k_i

    static constexpr double kSmoothingTime = 0.003; // s
    static constexpr double kMinTension = 0.01;     // floor on Q

    SphincterSettings settings_;
    OnePole massScale_;
    OnePole dampingRatio_;
    OnePole restArea_;
    OnePole tension_;
    OnePole openness_;

    Turbulence turbulence_;
    MassSpringState lower_;
    MassSpringState upper_;

    double density_ = 1.14; // kg/m^3, air at 37 C until setGas()
    double viscosity_ = kViscosity;
    double neighbourInertance_ = 0.0;
    double flow_ = 0.0;
    double minArea_ = 0.0;
};

} // namespace burp
