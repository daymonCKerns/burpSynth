#pragma once

// Ideal-gas relations for the gas in the tract.
//
// Burp gas is mostly swallowed air; fizzy drinks add carbon dioxide, which is
// heavier and slows sound from about 353 m/s to about 275 m/s at 37 C.
//
// Header-only: every function is defined inline in this file.

#include "model/math/Units.h"

#include <cmath>

namespace burp {

struct Gas {
    double molarMass; // kg/mol
    double gamma;     // ratio of heat capacities cp/cv
};

inline constexpr Gas kAir { 0.028964, 1.400 };
inline constexpr Gas kCarbonDioxide { 0.044010, 1.289 };

// Mixture of two ideal gases, fractionB being the mole fraction of b (0..1).
// Molar mass mixes linearly. Gamma comes from the mixed molar heat capacities:
//   cv_i = R / (gamma_i - 1),  cv = sum x_i cv_i,  gamma = 1 + R / cv
inline Gas mixGases(const Gas& a, const Gas& b, double fractionB)
{
    const double fractionA = 1.0 - fractionB;
    const double cvA = kGasConstant / (a.gamma - 1.0);
    const double cvB = kGasConstant / (b.gamma - 1.0);
    const double cv = fractionA * cvA + fractionB * cvB;
    return { fractionA * a.molarMass + fractionB * b.molarMass, 1.0 + kGasConstant / cv };
}

// c = sqrt(gamma R T / M)
inline double speedOfSound(const Gas& gas, double temperature)
{
    return std::sqrt(gas.gamma * kGasConstant * temperature / gas.molarMass);
}

// rho = P M / (R T), P absolute
inline double gasDensity(const Gas& gas, double absolutePressure, double temperature)
{
    return absolutePressure * gas.molarMass / (kGasConstant * temperature);
}

// n = P V / (R T), P absolute
inline double gasMoles(double absolutePressure, double volume, double temperature)
{
    return absolutePressure * volume / (kGasConstant * temperature);
}

} // namespace burp
