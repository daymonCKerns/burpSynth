#pragma once

// Interpolation and smoothing of control values, so that parameter changes
// reach the physics without steps.
//
// Header-only: every function is defined inline in this file.

#include <algorithm>
#include <cmath>

namespace burp {

// a + t (b - a)
inline double lerp(double a, double b, double t)
{
    return a + t * (b - a);
}

// 3t^2 - 2t^3 with t clamped to 0..1
inline double smoothstep(double t)
{
    const double x = std::clamp(t, 0.0, 1.0);
    return x * x * (3.0 - 2.0 * x);
}

// One-pole lowpass coefficient for a time constant tau: a = exp(-dt / tau).
// tau <= 0 gives 0 (no smoothing).
inline double onePoleCoefficient(double timeConstant, double dt)
{
    if (timeConstant <= 0.0) {
        return 0.0;
    }
    return std::exp(-dt / timeConstant);
}

// Exponential approach to a target:  y <- target + a (y - target)
class OnePole {
public:
    void setTime(double timeConstant, double dt);
    void reset(double value);
    double next(double target);
    double value() const;

private:
    double coefficient_ = 0.0;
    double value_ = 0.0;
};

inline void OnePole::setTime(double timeConstant, double dt)
{
    coefficient_ = onePoleCoefficient(timeConstant, dt);
}

inline void OnePole::reset(double initial)
{
    value_ = initial;
}

inline double OnePole::next(double target)
{
    value_ = target + coefficient_ * (value_ - target);
    return value_;
}

inline double OnePole::value() const
{
    return value_;
}

// Straight line to a target over a fixed number of steps.
class Ramp {
public:
    void reset(double value);
    void setTarget(double target, int steps); // steps <= 0 jumps
    double next();                            // advances one step
    double value() const;
    double increment() const;                 // change per step while ramping, else 0

private:
    double value_ = 0.0;
    double target_ = 0.0;
    double increment_ = 0.0;
    int remaining_ = 0;
};

inline void Ramp::reset(double initial)
{
    value_ = initial;
    target_ = initial;
    increment_ = 0.0;
    remaining_ = 0;
}

// increment = (target - value) / steps
inline void Ramp::setTarget(double target, int steps)
{
    target_ = target;
    if (steps <= 0) {
        value_ = target;
        increment_ = 0.0;
        remaining_ = 0;
        return;
    }
    increment_ = (target - value_) / static_cast<double>(steps);
    remaining_ = steps;
}

// The last step assigns the target so rounding in the increment cannot drift.
inline double Ramp::next()
{
    if (remaining_ > 0) {
        --remaining_;
        if (remaining_ == 0) {
            value_ = target_;
            increment_ = 0.0;
        } else {
            value_ += increment_;
        }
    }
    return value_;
}

inline double Ramp::value() const
{
    return value_;
}

inline double Ramp::increment() const
{
    return increment_;
}

} // namespace burp
