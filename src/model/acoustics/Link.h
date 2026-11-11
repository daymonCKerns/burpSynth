#pragma once

// Flow branch: gas moving between two pressure nodes.
//
//   L dU/dt = P_up - P_down - R U
//
// One of the solver's two primitives (the other is Cell). Positive flow is
// toward the mouth. BurpModel also uses a bare Link as the join between the
// pharynx and the mouth.
//
// Header-only: step() is defined inline in this file.

#include "model/math/Integrate.h"

namespace burp {

struct Link {
    double flow = 0.0;       // m^3/s
    double inertance = 1.0;  // kg/m^4
    double resistance = 0.0; // Pa s/m^3

    // One implicit step (stepInertance in math/Integrate.h). Returns the flow.
    double step(double dt, double pressureUp, double pressureDown);
};

inline double Link::step(double dt, double pressureUp, double pressureDown)
{
    flow = stepInertance(flow, inertance, resistance, pressureUp - pressureDown, dt);
    return flow;
}

} // namespace burp
