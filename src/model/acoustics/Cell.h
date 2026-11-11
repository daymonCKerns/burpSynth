#pragma once

// Pressure node: gas in a small volume.
//
//   C dP/dt = (flow in) - (flow out)
//
// One of the solver's two primitives (the other is Link). Links add their
// flow to netFlow during the flow half of a sample; step() then turns the
// net flow into a pressure change and clears the accumulator.
//
// Header-only: step() is defined inline in this file.

namespace burp {

struct Cell {
    double pressure = 0.0;   // Pa, gauge
    double compliance = 1.0; // m^3/Pa
    double netFlow = 0.0;    // m^3/s, inflow minus outflow this sample

    // P <- P + dt / C * netFlow;  netFlow <- 0
    void step(double dt);
};

inline void Cell::step(double dt)
{
    pressure += dt / compliance * netFlow;
    netFlow = 0.0;
}

} // namespace burp
