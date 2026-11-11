#pragma once

// Time steps for the two differential equations the model is built on.
//
// Both treat the loss term implicitly, so a very large damping or resistance
// slows the state down but can never make it blow up. See J.O. Smith,
// "Physical Audio Signal Processing", Semi-Implicit Methods.
//
// Header-only: every function is defined inline in this file.

namespace burp {

struct MassSpringState {
    double position = 0.0; // m
    double velocity = 0.0; // m/s
};

// One step of  m x'' + r x' + k x = F  (semi-implicit Euler):
//   v <- (v + dt (F - k x) / m) / (1 + dt r / m)
//   x <- x + dt v
inline void stepMassSpring(MassSpringState& state, double mass, double damping, double stiffness,
                           double force, double dt)
{
    state.velocity = (state.velocity + dt * (force - stiffness * state.position) / mass)
                     / (1.0 + dt * damping / mass);
    state.position += dt * state.velocity;
}

// One step of  L dU/dt = dP - R U:
//   U <- (U + dt dP / L) / (1 + dt R / L)
inline double stepInertance(double flow, double inertance, double resistance, double pressureDrop,
                            double dt)
{
    return (flow + dt * pressureDrop / inertance) / (1.0 + dt * resistance / inertance);
}

} // namespace burp
