// Sphincter: the two-mass UES from Sphincter.h.
//
// Order within one sample:
//   1. opening areas a_1, a_2, a_min from the displacements
//   2. flow through the slit (implicit step, Bernoulli loss, jet noise)
//   3. pressure on each edge for the open/closed case in force
//   4. each mass: spring, coupling and collision forces, one semi-implicit step

#include "model/acoustics/Sphincter.h"

#include "model/math/Acoustics.h"
#include "model/math/Flow.h"

#include <algorithm>
#include <cmath>

namespace burp {

namespace {

struct EdgePressures {
    double lower; // P_1, Pa
    double upper; // P_2, Pa
};

// a_i = a_0 + 2 l x_i
double edgeArea(double restingArea, double slitLength, double displacement)
{
    return restingArea + 2.0 * slitLength * displacement;
}

// collision_i = c_i a_i / (2 l) if a_i < 0, else 0
double collisionForce(double collisionStiffness, double area, double slitLength)
{
    return area < 0.0 ? collisionStiffness * area / (2.0 * slitLength) : 0.0;
}

// r = 2 zeta sqrt(m k)
double dampingFromRatio(double ratio, double mass, double stiffness)
{
    return 2.0 * ratio * std::sqrt(mass * stiffness);
}

// Edge pressures (Steinecke & Herzel 1995), plus the closed-face pressure.
EdgePressures edgePressures(double pressureUp, double pressureDown, double lowerArea,
                            double upperArea)
{
    if (lowerArea < kClosedArea) {
        return { kClosedFaceFraction * pressureUp, pressureDown };
    }
    if (upperArea < kClosedArea) {
        return { pressureUp, pressureDown };
    }
    const double ratio = std::min(lowerArea, upperArea) / lowerArea;
    return { pressureDown + (pressureUp - pressureDown) * (1.0 - ratio * ratio), pressureDown };
}

} // namespace

void Sphincter::prepare(double sampleRate)
{
    const double dt = 1.0 / sampleRate;
    massScale_.setTime(kSmoothingTime, dt);
    dampingRatio_.setTime(kSmoothingTime, dt);
    restArea_.setTime(kSmoothingTime, dt);
    tension_.setTime(kSmoothingTime, dt);
    openness_.setTime(kSmoothingTime, dt);
    turbulence_.prepare(sampleRate);
    reset();
}

void Sphincter::setGas(double density, double viscosity)
{
    density_ = density;
    viscosity_ = viscosity;
}

void Sphincter::setNeighbourInertance(double neighbourInertance)
{
    neighbourInertance_ = neighbourInertance;
}

void Sphincter::setSettings(const SphincterSettings& settings)
{
    settings_ = settings;
}

void Sphincter::setTurbulenceGain(double gain)
{
    turbulence_.setGain(gain);
}

void Sphincter::reset()
{
    settings_.openness = 0.0;
    massScale_.reset(settings_.massScale);
    dampingRatio_.reset(settings_.dampingRatio);
    restArea_.reset(settings_.restArea);
    tension_.reset(settings_.tension);
    openness_.reset(0.0);
    turbulence_.reset();

    // Held-shut equilibrium with no pressure: k x + c (x + a_0 / (2 l)) = 0,
    // the same for both masses because c_i = 3 k_i.
    const double restPosition =
        -kCollisionFactor / (1.0 + kCollisionFactor) * kHeldShutArea / (2.0 * kSlitLength);
    lower_ = { restPosition, 0.0 };
    upper_ = { restPosition, 0.0 };
    flow_ = 0.0;
    minArea_ = edgeArea(kHeldShutArea, kSlitLength, restPosition);
}

double Sphincter::step(double dt, double pressureUp, double pressureDown)
{
    const double massScale = massScale_.next(settings_.massScale);
    const double dampingRatio = dampingRatio_.next(settings_.dampingRatio);
    const double restArea = restArea_.next(settings_.restArea);
    const double tension = std::max(tension_.next(settings_.tension), kMinTension);
    const double openness = openness_.next(settings_.openness);

    // m_i = massScale m_i0 / Q,  k_i = massScale k_i0 Q,  k_c likewise
    const double lowerMass = massScale * kLowerMass / tension;
    const double upperMass = massScale * kUpperMass / tension;
    const double lowerStiffness = massScale * kLowerStiffness * tension;
    const double upperStiffness = massScale * kUpperStiffness * tension;
    const double couplingStiffness = massScale * kCouplingStiffness * tension;

    // 1. Opening areas.
    const double restingArea = lerp(kHeldShutArea, restArea, openness);
    const double lowerArea = edgeArea(restingArea, kSlitLength, lower_.position);
    const double upperArea = edgeArea(restingArea, kSlitLength, upper_.position);
    minArea_ = std::min(lowerArea, upperArea);

    // 2. Flow: L dU/dt = P_up - P_down - R_bern U + p_noise
    if (minArea_ < kClosedArea) {
        flow_ = 0.0;
    } else {
        const double slitInertance =
            inertance(density_, kLowerDepth + kUpperDepth, minArea_) + neighbourInertance_;
        const double resistance = bernoulliResistance(density_, flow_, minArea_);
        const double noise = turbulence_.pressure(density_, viscosity_, flow_, minArea_);
        flow_ = stepInertance(flow_, slitInertance, resistance, pressureUp - pressureDown + noise,
                              dt);
    }

    // 3. Edge pressures.
    const EdgePressures pressures = edgePressures(pressureUp, pressureDown, lowerArea, upperArea);

    // 4. l d_i P_i - k_c (x_i - x_j) - collision_i, then each mass.
    const double separation = lower_.position - upper_.position;
    const double lowerForce = kSlitLength * kLowerDepth * pressures.lower
                              - couplingStiffness * separation
                              - collisionForce(kCollisionFactor * lowerStiffness, lowerArea,
                                               kSlitLength);
    const double upperForce = kSlitLength * kUpperDepth * pressures.upper
                              + couplingStiffness * separation
                              - collisionForce(kCollisionFactor * upperStiffness, upperArea,
                                               kSlitLength);

    // One damping constant for both masses, set by the lower mass's ratio.
    const double damping = dampingFromRatio(dampingRatio, lowerMass, lowerStiffness);
    stepMassSpring(lower_, lowerMass, damping, lowerStiffness, lowerForce, dt);
    stepMassSpring(upper_, upperMass, damping, upperStiffness, upperForce, dt);
    return flow_;
}

double Sphincter::flow() const
{
    return flow_;
}

double Sphincter::minArea() const
{
    return minArea_;
}

double Sphincter::lowerDisplacement() const
{
    return lower_.position;
}

} // namespace burp
