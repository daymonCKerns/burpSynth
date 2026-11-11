#pragma once

// Lumped acoustic elements of a tube section, and the grid rule that keeps
// the solver stable.
//
// A short section of tube (length l, area A) stores kinetic energy as an
// inertance and potential energy as a compliance. See J.O. Smith, "Physical
// Audio Signal Processing", Lumped Models; Flanagan, "Speech Analysis,
// Synthesis and Perception" for the radiation load.
//
// Header-only: every function is defined inline in this file.

#include <algorithm>
#include <cmath>
#include <numbers>

namespace burp {

// Courant number c dt / dx used for every tube. 0.5 keeps the leapfrog scheme
// stable for any area profile and for a cell with three links attached
// (docs/implementation_plan.md, section 3.2).
inline constexpr double kCourant = 0.5;

// Openings smaller than this (m^2) are treated as fully closed: no flow.
inline constexpr double kClosedArea = 1.0e-9;

// L = rho l / A   (kg/m^4)
inline double inertance(double density, double length, double area)
{
    return density * length / area;
}

// C = V / (rho c^2)   (m^3/Pa)
inline double compliance(double volume, double density, double soundSpeed)
{
    return volume / (density * soundSpeed * soundSpeed);
}

// Poiseuille resistance of a round section: R = 8 pi mu l / A^2   (Pa s/m^3)
inline double viscousResistance(double viscosity, double length, double area)
{
    return 8.0 * std::numbers::pi * viscosity * length / (area * area);
}

// Largest cell count with dx >= c dt / kCourant: floor(kCourant * length / (c dt)),
// never fewer than 2.
inline int cellCountForLength(double length, double soundSpeed, double dt)
{
    const int count = static_cast<int>(std::floor(kCourant * length / (soundSpeed * dt)));
    return std::max(2, count);
}

// Radiation from an opening of area A in a baffle (Flanagan), as a
// resistance in parallel with an inertance:
//   R = 128 rho c / (9 pi^2 A)      L = 8 rho / (3 pi sqrt(pi A))
inline double radiationResistance(double density, double soundSpeed, double area)
{
    return 128.0 * density * soundSpeed / (9.0 * std::numbers::pi * std::numbers::pi * area);
}

inline double radiationInertance(double density, double area)
{
    return 8.0 * density / (3.0 * std::numbers::pi * std::sqrt(std::numbers::pi * area));
}

// Equal temperament, A4 = MIDI 69 = 440 Hz:  f = 440 * 2^((s - 69) / 12)
inline double midiToHz(double semitones)
{
    return 440.0 * std::pow(2.0, (semitones - 69.0) / 12.0);
}

inline double hzToMidi(double hz)
{
    return 69.0 + 12.0 * std::log2(hz / 440.0);
}

} // namespace burp
