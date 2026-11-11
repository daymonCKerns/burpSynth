// Tests for Reservoir (the stomach).
//
//   Start      200 mL of gas with an elastance of 10 mmHg per 200 mL and no
//              external pressure sits close to 10 mmHg (slightly under,
//              because the gas is compressed below 200 mL).
//   Squeeze    Adding external pressure raises the pressure by about the
//              same amount.
//   Emptying   With a steady outflow the pressure and volume fall
//              monotonically and the pressure reaches zero when the gas is
//              gone; the moles never go negative.
//   Balance    At every step (P_atm + P) V = n R T to within rounding.

#include "Check.h"

#include "model/acoustics/Reservoir.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace burp {
namespace {

constexpr double kGasVolume = mlToM3(200.0);
constexpr double kElastance = mmHgToPa(10.0) / kGasVolume; // 10 mmHg per 200 mL

void testStart()
{
    Reservoir stomach;
    stomach.setWallElastance(kElastance);
    stomach.fill(kGasVolume);

    // With n R T = P_atm V0:  E V^2 + P_atm V - P_atm V0 = 0,  P = E V
    const double b = kAtmosphericPressure;
    const double v = (-b + std::sqrt(b * b + 4.0 * kElastance * b * kGasVolume)) / (2.0 * kElastance);
    const double expected = kElastance * v;

    std::printf("start: %.3f Pa (%.4f mmHg), closed form %.3f Pa, volume %.3f mL\n",
                stomach.pressure(), stomach.pressure() / mmHgToPa(1.0), expected,
                stomach.volume() * 1.0e6);
    BURP_CHECK_NEAR(stomach.pressure(), expected, 1.0e-6 * expected);
    BURP_CHECK(stomach.pressure() < mmHgToPa(10.0));
    BURP_CHECK_NEAR(stomach.pressure(), mmHgToPa(10.0), 0.05 * mmHgToPa(10.0));
}

void testSqueeze()
{
    Reservoir stomach;
    stomach.setWallElastance(kElastance);
    stomach.fill(kGasVolume);

    const double before = stomach.pressure();
    const double squeeze = mmHgToPa(20.0);
    stomach.setExternalPressure(squeeze);
    const double rise = stomach.pressure() - before;

    std::printf("squeeze: external %.1f Pa raises the pressure by %.1f Pa\n", squeeze, rise);
    BURP_CHECK_NEAR(rise, squeeze, 0.05 * squeeze);
}

void testEmptyingAndBalance()
{
    Reservoir stomach;
    stomach.setWallElastance(kElastance);
    stomach.fill(kGasVolume);

    constexpr double outflow = mlToM3(100.0); // m^3/s
    constexpr double dt = 1.0e-4;
    constexpr int steps = 40000; // 4 s

    double lastPressure = stomach.pressure();
    double lastVolume = stomach.volume();
    bool monotonic = true;
    bool nonNegative = true;
    double worstBalance = 0.0;
    int emptyStep = -1;

    for (int s = 0; s < steps; ++s) {
        stomach.addOutflow(outflow);
        stomach.step(dt);

        monotonic = monotonic && stomach.pressure() <= lastPressure && stomach.volume() <= lastVolume;
        nonNegative = nonNegative && stomach.moles() >= 0.0;

        // The reported pressure fades below the 1 mL residual volume, so the
        // gas law is checked above it.
        if (stomach.volume() >= mlToM3(1.0)) {
            const double nrt = stomach.moles() * kGasConstant * kBodyTemperature;
            const double balance =
                std::abs((kAtmosphericPressure + stomach.pressure()) * stomach.volume() - nrt) / nrt;
            worstBalance = std::max(worstBalance, balance);
        }

        if (emptyStep < 0 && stomach.moles() == 0.0) {
            emptyStep = s;
        }
        lastPressure = stomach.pressure();
        lastVolume = stomach.volume();
    }

    std::printf("emptying: gas gone after %.4f s, final pressure %g Pa\n", (emptyStep + 1) * dt,
                stomach.pressure());
    std::printf("balance: worst relative error %g\n", worstBalance);
    BURP_CHECK(monotonic);
    BURP_CHECK(nonNegative);
    BURP_CHECK(emptyStep >= 0);
    BURP_CHECK_NEAR(stomach.pressure(), 0.0, 1.0e-9);
    BURP_CHECK(worstBalance < 1.0e-9);
}

} // namespace
} // namespace burp

int main()
{
    burp::testStart();
    burp::testSqueeze();
    burp::testEmptyingAndBalance();
    return burp::test::failures();
}
