#pragma once

// Steady-flow relations for gas passing through a constriction.
//
// Flow U (m^3/s) through an opening of area A has velocity v = U / A. The
// jet that forms past a constriction does not recover its pressure, so the
// whole dynamic pressure is lost (Bernoulli with jet separation).
//
// Header-only: every function is defined inline in this file.

#include <cmath>
#include <numbers>

namespace burp {

// q = 1/2 rho (U / A)^2   (Pa)
inline double dynamicPressure(double density, double flow, double area)
{
    const double velocity = flow / area;
    return 0.5 * density * velocity * velocity;
}

// The Bernoulli loss written as a resistance, so that dP = R U:
//   R = rho |U| / (2 A^2)   (Pa s/m^3)
// Using the previous step's |U| makes the loss linear within one step.
inline double bernoulliResistance(double density, double flow, double area)
{
    return density * std::abs(flow) / (2.0 * area * area);
}

// Re = rho v d / mu,  v = |U| / A,  d = sqrt(4 A / pi) (equivalent diameter)
inline double reynoldsNumber(double density, double viscosity, double flow, double area)
{
    const double velocity = std::abs(flow) / area;
    const double diameter = std::sqrt(4.0 * area / std::numbers::pi);
    return density * velocity * diameter / viscosity;
}

} // namespace burp
