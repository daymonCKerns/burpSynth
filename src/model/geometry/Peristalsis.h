#pragma once

// Peristalsis: a ring of muscle contraction that travels up the esophagus
// and pushes the gas ahead of it (the third belch reflex).
//
// The wave narrows the tube around its centre:
//   factor(x) = 1 - depth * exp(-((x - centre) / width)^2)
// x and centre are normalised positions (0 = LES end, 1 = UES end). The
// esophagus area is multiplied by this factor; because the solver accounts
// for moving walls (dV/dt), the narrowing pumps gas.
//
// Header-only: the function is defined inline in this file.

#include <cmath>

namespace burp {

inline constexpr double kPeristalsisWidth = 0.15; // normalised half-width of the wave

// Area multiplier at `position`. depth 0..1 (1 would close the tube fully).
inline double peristalsisFactor(double position, double centre, double width, double depth)
{
    const double u = (position - centre) / width;
    return 1.0 - depth * std::exp(-u * u);
}

} // namespace burp
