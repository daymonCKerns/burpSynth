// Tests for src/model/math: each helper against a value worked out by hand.
//
//   Gas        speed of sound in air at 37 C is 353 m/s (+-1); in carbon
//              dioxide about 275 m/s (+-3); a 50/50 mix lies between
//   Units      760 mmHg = 101325 Pa; 1 cm^2 = 1e-4 m^2; 1 mL = 1e-6 m^3
//   Acoustics  inertance, compliance and cell count for a known section;
//              MIDI 69 = 440 Hz, MIDI 57 = 220 Hz, and the inverse
//   Flow       dynamic pressure and Reynolds number for a known jet
//   Integrate  an undamped mass-spring rings at sqrt(k/m) / (2 pi) (+-1%);
//              an inertance with a constant pressure drop settles at dP / R
//   Smooth     a one-pole reaches 63% of a step after one time constant;
//              a ramp arrives exactly on its last step

#include "Check.h"
#include "Spectrum.h"

#include "model/math/Acoustics.h"
#include "model/math/Flow.h"
#include "model/math/Gas.h"
#include "model/math/Integrate.h"
#include "model/math/Smooth.h"
#include "model/math/Units.h"

#include <cmath>
#include <cstdio>
#include <numbers>
#include <vector>

namespace burp {
namespace {

void testGas()
{
    const double air = speedOfSound(kAir, kBodyTemperature);
    const double carbonDioxide = speedOfSound(kCarbonDioxide, kBodyTemperature);
    const double mixed = speedOfSound(mixGases(kAir, kCarbonDioxide, 0.5), kBodyTemperature);
    std::printf("speed of sound at 37 C: air %.2f m/s, CO2 %.2f m/s, 50/50 mix %.2f m/s\n", air,
                carbonDioxide, mixed);
    BURP_CHECK_NEAR(air, 353.0, 1.0);
    BURP_CHECK_NEAR(carbonDioxide, 275.0, 3.0);
    BURP_CHECK(mixed < air && mixed > carbonDioxide);
}

void testUnits()
{
    std::printf("760 mmHg = %.4f Pa, 1 cm^2 = %g m^2, 1 mL = %g m^3\n", mmHgToPa(760.0),
                cm2ToM2(1.0), mlToM3(1.0));
    BURP_CHECK_NEAR(mmHgToPa(760.0), kAtmosphericPressure, 0.1);
    BURP_CHECK_NEAR(cm2ToM2(1.0), 1.0e-4, 1.0e-15);
    BURP_CHECK_NEAR(mlToM3(1.0), 1.0e-6, 1.0e-18);
}

void testAcoustics()
{
    // A 1 cm long, 1 cm^2 section in gas with rho = 1.2 kg/m^3, c = 350 m/s:
    //   L = 1.2 * 0.01 / 1e-4 = 120 kg/m^4
    //   C = 1.47e-6 / (1.2 * 350^2) = 1e-11 m^3/Pa
    //   a 17 cm tube at 96 kHz: floor(0.5 * 0.17 / (350 / 96000)) = floor(23.3) = 23 cells
    const double density = 1.2;
    const double soundSpeed = 350.0;
    const double dt = 1.0 / 96000.0;
    const double sectionInertance = inertance(density, 0.01, 1.0e-4);
    const double sectionCompliance = compliance(1.47e-6, density, soundSpeed);
    const int cells = cellCountForLength(0.17, soundSpeed, dt);
    const int shortCells = cellCountForLength(0.001, soundSpeed, dt);
    std::printf("section: inertance %.6f kg/m^4, compliance %.6g m^3/Pa, cells %d (1 mm tube: %d)\n",
                sectionInertance, sectionCompliance, cells, shortCells);
    BURP_CHECK_NEAR(sectionInertance, 120.0, 1.0e-9);
    BURP_CHECK_NEAR(sectionCompliance, 1.0e-11, 1.0e-20);
    BURP_CHECK(cells == 23);
    BURP_CHECK(shortCells == 2);

    const double a4 = midiToHz(69.0);
    const double a3 = midiToHz(57.0);
    const double note440 = hzToMidi(440.0);
    const double note220 = hzToMidi(220.0);
    std::printf("MIDI 69 = %.6f Hz, MIDI 57 = %.6f Hz, 440 Hz = MIDI %.6f, 220 Hz = MIDI %.6f\n",
                a4, a3, note440, note220);
    BURP_CHECK_NEAR(a4, 440.0, 1.0e-9);
    BURP_CHECK_NEAR(a3, 220.0, 1.0e-9);
    BURP_CHECK_NEAR(note440, 69.0, 1.0e-9);
    BURP_CHECK_NEAR(note220, 57.0, 1.0e-9);
}

void testFlow()
{
    // A 19 m/s jet through a 5 mm round opening with rho = 1.2 kg/m^3:
    //   q  = 0.5 * 1.2 * 19^2 = 216.6 Pa
    //   Re = 1.2 * 19 * 0.005 / 1.9e-5 = 6000
    const double density = 1.2;
    const double diameter = 0.005;
    const double area = 0.25 * std::numbers::pi * diameter * diameter;
    const double flow = 19.0 * area;
    const double pressure = dynamicPressure(density, flow, area);
    const double reynolds = reynoldsNumber(density, kViscosity, flow, area);
    const double bernoulliDrop = bernoulliResistance(density, flow, area) * flow;
    std::printf("jet: dynamic pressure %.6f Pa, Bernoulli drop %.6f Pa, Reynolds %.4f\n",
                pressure, bernoulliDrop, reynolds);
    BURP_CHECK_NEAR(pressure, 216.6, 1.0e-9);
    BURP_CHECK_NEAR(bernoulliDrop, 216.6, 1.0e-9);
    BURP_CHECK_NEAR(reynolds, 6000.0, 1.0e-6);
}

void testIntegrate()
{
    const double sampleRate = 96000.0;
    const double dt = 1.0 / sampleRate;

    // m = 0.02 kg, k = 2000 N/m: f = sqrt(1e5) / (2 pi) = 50.33 Hz
    const double mass = 0.02;
    const double stiffness = 2000.0;
    const double expectedHz = std::sqrt(stiffness / mass) / (2.0 * std::numbers::pi);
    MassSpringState state;
    state.position = 1.0e-3;
    std::vector<double> positions;
    positions.reserve(96000);
    for (int i = 0; i < 96000; ++i) {
        stepMassSpring(state, mass, 0.0, stiffness, 0.0, dt);
        positions.push_back(state.position);
    }
    const double ringHz = test::zeroCrossingFrequency(positions, sampleRate);
    std::printf("mass-spring rings at %.4f Hz, expected %.4f Hz\n", ringHz, expectedHz);
    BURP_CHECK_NEAR(ringHz, expectedHz, 0.01 * expectedHz);

    // L = 1000 kg/m^4, R = 1e6 Pa s/m^3, dP = 100 Pa: U settles at 1e-4 m^3/s
    // after 0.1 s, which is 100 time constants L / R.
    const double linkInertance = 1000.0;
    const double resistance = 1.0e6;
    const double pressureDrop = 100.0;
    double flow = 0.0;
    for (int i = 0; i < 9600; ++i) {
        flow = stepInertance(flow, linkInertance, resistance, pressureDrop, dt);
    }
    std::printf("inertance settles at %.9g m^3/s, expected %.9g m^3/s\n", flow,
                pressureDrop / resistance);
    BURP_CHECK_NEAR(flow, pressureDrop / resistance, 1.0e-12);
}

void testSmooth()
{
    // tau = 15 ms stepped at 1 ms: after 15 steps y = 1 - exp(-1) = 0.632
    OnePole smoother;
    smoother.setTime(0.015, 0.001);
    smoother.reset(0.0);
    for (int i = 0; i < 15; ++i) {
        smoother.next(1.0);
    }
    std::printf("one-pole after one time constant: %.6f\n", smoother.value());
    BURP_CHECK_NEAR(smoother.value(), 1.0 - std::exp(-1.0), 1.0e-6);

    // 0.3 / 7 is not exact in binary, so summing seven increments would drift.
    Ramp ramp;
    ramp.reset(0.0);
    ramp.setTarget(0.3, 7);
    const double step = ramp.increment();
    for (int i = 0; i < 6; ++i) {
        ramp.next();
    }
    const double beforeLast = ramp.value();
    ramp.next();
    std::printf("ramp: increment %.17g, after 6 steps %.17g, after 7 steps %.17g\n", step,
                beforeLast, ramp.value());
    BURP_CHECK_NEAR(step, 0.3 / 7.0, 1.0e-15);
    BURP_CHECK(beforeLast < 0.3);
    BURP_CHECK(ramp.value() == 0.3);
    BURP_CHECK(ramp.increment() == 0.0);
}

} // namespace
} // namespace burp

int main()
{
    burp::testGas();
    burp::testUnits();
    burp::testAcoustics();
    burp::testFlow();
    burp::testIntegrate();
    burp::testSmooth();
    return burp::test::failures();
}
