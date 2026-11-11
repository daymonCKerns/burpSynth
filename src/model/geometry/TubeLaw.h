#pragma once

// Tube law: how far a collapsible tube opens for a given pressure inside it.
//
// The esophagus is flat at rest and is pushed open by the gas. The open
// fraction of its full area rises smoothly with the pressure across the wall:
//   fraction = collapsed + (1 - collapsed) * smoothstep(P / P_open)
// collapsed is the fraction left open at zero pressure (never 0, so gas can
// always seep in and start the inflation); P_open is the pressure at which
// the tube is fully round. A simplification of the tube laws in Shapiro,
// "Steady flow in collapsible tubes", J. Biomech. Eng. 99 (1977).
//
// Evaluated at control rate on a slow average of each cell's pressure.
//
// Header-only: the function is defined inline in this file.

#include "model/math/Smooth.h"

namespace burp {

inline constexpr double kEsophagusOpeningPressure = 500.0; // Pa

// Fraction (collapsedFraction..1) of the open area at `pressure` (Pa, gauge).
inline double tubeLawOpenFraction(double pressure, double openingPressure, double collapsedFraction)
{
    return collapsedFraction + (1.0 - collapsedFraction) * smoothstep(pressure / openingPressure);
}

} // namespace burp
