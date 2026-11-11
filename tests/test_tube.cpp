// Tests for the linear acoustics: Tube, Valve, Radiation and Turbulence.
//
//   Resonance  A uniform 17 cm tube in air, rigid walls, closed at the inlet
//              and held at zero pressure at the outlet (a Link from the
//              outlet cell to P = 0 that includes the outlet half-cell
//              inertance). After a short flow pulse into the inlet, the
//              outlet flow rings near c / 4L * (1, 3, 5) = 519, 1557 and
//              2596 Hz. Check each peak within 4% and print what was found.
//   Stability  The same tube with a strongly varying area profile stays
//              finite for one second.
//   Pumping    Narrowing a closed tube's areas raises its pressure (the
//              moving-wall term).
//   Valve      Shut, it passes no flow; open with a steady pressure drop,
//              the flow settles near A sqrt(2 dP / rho).
//   Radiation  Stays finite as the area goes from open to closed; passes no
//              flow when closed.
//   Turbulence Silent below the critical Reynolds number, noisy above it.

#include "Check.h"
#include "Spectrum.h"

#include "model/acoustics/Link.h"
#include "model/acoustics/Radiation.h"
#include "model/acoustics/Tube.h"
#include "model/acoustics/Turbulence.h"
#include "model/acoustics/Valve.h"
#include "model/geometry/AreaFunction.h"
#include "model/math/Acoustics.h"
#include "model/math/Flow.h"
#include "model/math/Gas.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

namespace burp {
namespace {

constexpr double kSampleRate = 96000.0;
constexpr double kDt = 1.0 / kSampleRate;
constexpr int kControlSamples = 96; // 1 ms
constexpr int kPulseSamples = 8;
constexpr double kPulseFlow = 1.0e-4; // m^3/s, peak

double airDensity()
{
    return gasDensity(kAir, kAtmosphericPressure, kBodyTemperature);
}

double airSoundSpeed()
{
    return speedOfSound(kAir, kBodyTemperature);
}

// Raised-cosine flow pulse over the first kPulseSamples samples.
double pulse(int sample)
{
    if (sample >= kPulseSamples) {
        return 0.0;
    }
    const double phase = 2.0 * std::numbers::pi * (sample + 0.5) / kPulseSamples;
    return kPulseFlow * 0.5 * (1.0 - std::cos(phase));
}

void prepareAirTube(Tube& tube, double tubeLength, const AreaFunction& shape, double wallScale)
{
    tube.prepare(0.3, kSampleRate);
    tube.setGas(airDensity(), airSoundSpeed(), kViscosity);
    tube.setWallScale(wallScale);
    tube.configure(tubeLength, shape);
}

bool allPressuresFinite(const Tube& tube)
{
    for (int i = 0; i < tube.cellCount(); ++i) {
        if (!std::isfinite(tube.pressures()(i))) {
            return false;
        }
    }
    return true;
}

// One sample of a tube with a pulse at the inlet and an outlet link to P = 0,
// in the model's order: links, route flows, cells. Returns the outlet flow.
double stepOpenTube(Tube& tube, Link& outlet, int sample)
{
    const double outletFlow = outlet.step(kDt, tube.outletPressure(), 0.0);
    tube.stepLinks(kDt);
    tube.addOutletFlow(outletFlow);
    tube.addInletFlow(pulse(sample));
    tube.stepCells(kDt);
    return outletFlow;
}

void testResonance()
{
    AreaFunction shape;
    shape.fill(4, cm2ToM2(3.0));
    Tube tube;
    prepareAirTube(tube, 0.17, shape, 0.0);

    Link outlet;
    outlet.inertance = tube.outletHalfInertance();

    const int samples = static_cast<int>(0.25 * kSampleRate);
    std::vector<double> outletFlow;
    outletFlow.reserve(static_cast<std::size_t>(samples));
    bool finite = true;
    for (int s = 0; s < samples; ++s) {
        const double u = stepOpenTube(tube, outlet, s);
        outletFlow.push_back(u);
        finite = finite && std::isfinite(u) && std::isfinite(tube.outletPressure());
    }
    BURP_CHECK(finite);

    std::printf("resonance: %d cells, dx = %.3f mm, c = %.1f m/s\n", tube.cellCount(),
                tube.cellLength() * 1.0e3, airSoundSpeed());
    for (int mode = 1; mode <= 3; ++mode) {
        // f_n = (2n - 1) c / (4 L)
        const double expected = (2 * mode - 1) * airSoundSpeed() / (4.0 * tube.length());
        const double found =
            test::peakFrequency(outletFlow, kSampleRate, 0.9 * expected, 1.1 * expected, 1.0);
        std::printf("  mode %d: expected %.1f Hz, found %.1f Hz (%+.2f%%)\n", mode, expected, found,
                    100.0 * (found - expected) / expected);
        BURP_CHECK_NEAR(found, expected, 0.04 * expected);
    }
}

void testStability()
{
    // Alternating narrow and wide sections, and the same profile reversed.
    AreaFunction profile;
    AreaFunction reversed;
    profile.fill(16, cm2ToM2(6.0));
    reversed.fill(16, cm2ToM2(6.0));
    for (std::size_t i = 0; i < 16; ++i) {
        if (i % 2 == 0) {
            profile.area[i] = cm2ToM2(0.1);
        } else {
            reversed.area[i] = cm2ToM2(0.1);
        }
    }

    Tube tube;
    prepareAirTube(tube, 0.17, profile, 1.0);
    Link outlet;

    // One second, swapping between the profiles every 10 ms so the moving
    // walls are exercised too.
    const int samples = static_cast<int>(kSampleRate);
    double peakPressure = 0.0;
    bool finite = true;
    for (int s = 0; s < samples; ++s) {
        if (s % kControlSamples == 0) {
            const bool useReversed = (s / (10 * kControlSamples)) % 2 == 1;
            tube.setTargetShape(useReversed ? reversed : profile, kControlSamples);
            outlet.inertance = tube.outletHalfInertance();
        }
        stepOpenTube(tube, outlet, s);
        peakPressure = std::max(peakPressure, std::abs(tube.outletPressure()));
        finite = finite && std::isfinite(tube.outletPressure());
    }
    finite = finite && allPressuresFinite(tube);

    std::printf("stability: %d cells, peak outlet pressure %g Pa, final inlet pressure %g Pa\n",
                tube.cellCount(), peakPressure, tube.inletPressure());
    BURP_CHECK(finite);
}

void testPumping()
{
    AreaFunction wide;
    AreaFunction narrow;
    wide.fill(4, cm2ToM2(2.0));
    narrow.fill(4, cm2ToM2(1.0));

    Tube tube;
    prepareAirTube(tube, 0.1, wide, 0.0);

    const int rampSamples = 10 * kControlSamples;
    tube.setTargetShape(narrow, rampSamples);
    for (int s = 0; s < rampSamples; ++s) {
        tube.stepLinks(kDt);
        tube.stepCells(kDt);
    }

    double mean = 0.0;
    for (int i = 0; i < tube.cellCount(); ++i) {
        mean += tube.pressures()(i);
    }
    mean /= tube.cellCount();

    // C dP/dt = -dx dA/dt with C = A dx / (rho c^2) gives dP = -rho c^2 dA / A,
    // so halving the area raises the pressure by rho c^2 ln 2.
    const double c = airSoundSpeed();
    const double expected = airDensity() * c * c * std::numbers::ln2;
    std::printf("pumping: halving the area gives %.1f Pa, linear theory %.1f Pa\n", mean, expected);
    BURP_CHECK(mean > 0.0);
    BURP_CHECK_NEAR(mean, expected, 0.05 * expected);
    BURP_CHECK(allPressuresFinite(tube));
}

void testValve()
{
    const double rho = airDensity();
    const double pressureDrop = 1000.0;

    Valve valve;
    valve.setGas(rho, kViscosity);
    valve.setNeighbourInertance(20.0);
    valve.reset();

    double shutFlow = 0.0;
    for (int s = 0; s < 1000; ++s) {
        shutFlow = std::max(shutFlow, std::abs(valve.step(kDt, pressureDrop, 0.0)));
    }
    std::printf("valve: shut flow %g m^3/s\n", shutFlow);
    BURP_CHECK(shutFlow == 0.0);

    const double openArea = cm2ToM2(0.5);
    valve.setTargetArea(openArea, kControlSamples);
    const int samples = static_cast<int>(0.2 * kSampleRate);
    for (int s = 0; s < samples; ++s) {
        valve.step(kDt, pressureDrop, 0.0);
    }

    // Bernoulli jet: dP = 1/2 rho (U / A)^2
    const double expected = openArea * std::sqrt(2.0 * pressureDrop / rho);
    std::printf("valve: open flow %g m^3/s, Bernoulli %g m^3/s\n", valve.flow(), expected);
    BURP_CHECK_NEAR(valve.flow(), expected, 0.03 * expected);
}

void testRadiation()
{
    const double rho = airDensity();
    const double openArea = cm2ToM2(3.0);

    Radiation lips;
    lips.setGas(rho, airSoundSpeed());
    lips.setNeighbourInertance(inertance(rho, 0.5 * 7.4e-3, openArea));
    lips.reset();

    // Closes over 100 ms while driven by a 500 Hz pressure. The area falls as
    // the square of the remaining time, so the last samples cross the tiny
    // areas just above kClosedArea.
    const int closingSamples = static_cast<int>(0.1 * kSampleRate);
    double peakFlow = 0.0;
    bool finite = true;
    for (int s = 0; s < closingSamples; ++s) {
        const double remaining = 1.0 - static_cast<double>(s + 1) / closingSamples;
        lips.setArea(openArea * remaining * remaining);
        const double drive = 200.0 * std::sin(2.0 * std::numbers::pi * 500.0 * s * kDt);
        lips.step(kDt, drive);
        peakFlow = std::max(peakFlow, std::abs(lips.flow()));
        finite = finite && std::isfinite(lips.flow()) && std::isfinite(lips.flowDerivative());
    }
    std::printf("radiation: peak flow while closing %g m^3/s\n", peakFlow);
    BURP_CHECK(finite);

    double closedFlow = 0.0;
    lips.setArea(0.0);
    for (int s = 0; s < 1000; ++s) {
        lips.step(kDt, 200.0);
        closedFlow = std::max(closedFlow, std::abs(lips.flow()) + std::abs(lips.flowDerivative()));
    }
    std::printf("radiation: closed flow %g m^3/s\n", closedFlow);
    BURP_CHECK(closedFlow == 0.0);
}

void testTurbulence()
{
    const double rho = airDensity();
    const double area = cm2ToM2(1.0);
    const double slowFlow = 1.0e-4; // m^3/s
    const double fastFlow = 2.0e-3; // m^3/s
    const double slowReynolds = reynoldsNumber(rho, kViscosity, slowFlow, area);
    const double fastReynolds = reynoldsNumber(rho, kViscosity, fastFlow, area);
    BURP_CHECK(slowReynolds < kCriticalReynolds);
    BURP_CHECK(fastReynolds > kCriticalReynolds);

    Turbulence noise;
    noise.prepare(kSampleRate);
    noise.setGain(1.0);
    noise.reset();

    constexpr int samples = 9600;
    double quietPeak = 0.0;
    for (int s = 0; s < samples; ++s) {
        quietPeak = std::max(quietPeak, std::abs(noise.pressure(rho, kViscosity, slowFlow, area)));
    }

    double sumSquares = 0.0;
    bool finite = true;
    for (int s = 0; s < samples; ++s) {
        const double p = noise.pressure(rho, kViscosity, fastFlow, area);
        sumSquares += p * p;
        finite = finite && std::isfinite(p);
    }
    const double rms = std::sqrt(sumSquares / samples);

    std::printf("turbulence: Re %.0f gives peak %g Pa; Re %.0f gives rms %g Pa\n", slowReynolds,
                quietPeak, fastReynolds, rms);
    BURP_CHECK(quietPeak == 0.0);
    BURP_CHECK(finite);
    BURP_CHECK(rms > 1.0);
}

} // namespace
} // namespace burp

int main()
{
    burp::testResonance();
    burp::testStability();
    burp::testPumping();
    burp::testValve();
    burp::testRadiation();
    burp::testTurbulence();
    return burp::test::failures();
}
