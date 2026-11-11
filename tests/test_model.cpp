// End-to-end tests for BurpModel at 96 kHz with default controls.
//
//   Silent idle   Before any trigger the output is exactly zero.
//   Burp          trigger(note 45, velocity 0.8), render 1 s: every sample is
//                 finite, the peak is above 1e-4 Pa (audible), and the
//                 stomach has lost gas.
//   Release       release(), render 1.5 s: the last 100 ms are silent and
//                 the model is asleep.
//   Legato        During a held note, changeNote two octaves down lowers the
//                 measured pitch.
//   Extremes      Finite output with every 0..1 control at 0 and at 1, with
//                 abdominal pressure at 100 mmHg, and with the lips closed.
//
// Prints the peak level, the stomach pressure before and after, and the
// measured pitch of the held note next to the target, as a tuning aid.

#include "Check.h"
#include "Spectrum.h"
#include "model/BurpControls.h"
#include "model/BurpModel.h"
#include "model/math/Units.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace {

constexpr double kSampleRate = 96000.0;
constexpr double kNote = 45.0;
constexpr double kVelocity = 0.8;

std::vector<double> renderSeconds(burp::BurpModel& model, double seconds)
{
    std::vector<double> buffer(static_cast<std::size_t>(std::lround(seconds * kSampleRate)));
    model.render(buffer.data(), static_cast<int>(buffer.size()));
    return buffer;
}

std::vector<double> lastSeconds(const std::vector<double>& signal, double seconds)
{
    const std::size_t count
        = std::min(signal.size(), static_cast<std::size_t>(std::lround(seconds * kSampleRate)));
    return std::vector<double>(signal.end() - static_cast<std::ptrdiff_t>(count), signal.end());
}

double peak(const std::vector<double>& signal)
{
    double largest = 0.0;
    for (const double sample : signal) {
        largest = std::max(largest, std::abs(sample));
    }
    return largest;
}

bool allFinite(const std::vector<double>& signal)
{
    return std::all_of(signal.begin(), signal.end(), [](double sample) { return std::isfinite(sample); });
}

void testSilentIdle()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    const std::vector<double> out = renderSeconds(model, 0.1);
    BURP_CHECK(model.isAsleep());
    BURP_CHECK(allFinite(out));
    BURP_CHECK(peak(out) == 0.0);
}

void testBurpAndRelease()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.trigger(kNote, kVelocity);
    const double molesBefore = model.stomachMoles();
    const double pressureBefore = model.stomachPressure();

    const std::vector<double> held = renderSeconds(model, 1.0);
    std::printf("burp: peak %.3g Pa, stomach %.1f Pa -> %.1f Pa\n", peak(held), pressureBefore,
                model.stomachPressure());
    std::printf("burp: pitch %.1f Hz measured, %.1f Hz target, output zero crossings %.1f Hz\n",
                model.measuredHz(), model.targetHz(),
                burp::test::zeroCrossingFrequency(lastSeconds(held, 0.2), kSampleRate));
    BURP_CHECK(allFinite(held));
    BURP_CHECK(peak(held) > 1.0e-4);
    BURP_CHECK(model.stomachMoles() < molesBefore);

    model.release();
    const std::vector<double> tail = renderSeconds(model, 1.5);
    BURP_CHECK(allFinite(tail));
    BURP_CHECK(peak(lastSeconds(tail, 0.1)) == 0.0);
    BURP_CHECK(model.isAsleep());
}

void testLegato()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.trigger(kNote, kVelocity);
    renderSeconds(model, 0.4);
    const double highHz = model.measuredHz();
    std::printf("legato: note %.0f  %.1f Hz measured, %.1f Hz target\n", kNote, highHz, model.targetHz());

    model.changeNote(kNote - 24.0, 0.0);
    renderSeconds(model, 0.6);
    const double lowHz = model.measuredHz();
    std::printf("legato: note %.0f  %.1f Hz measured, %.1f Hz target\n", kNote - 24.0, lowHz,
                model.targetHz());

    BURP_CHECK(highHz > 0.0);
    BURP_CHECK(lowHz > 0.0);
    BURP_CHECK(lowHz < highHz);
}

// Every control whose range is 0..1, set to `value`.
burp::BurpControls unitControls(double value)
{
    burp::BurpControls controls;
    controls.mouth.jawOpening = value;
    controls.mouth.tonguePosition = value;
    controls.mouth.tongueHeight = value;
    controls.reflex.peristalsisStrength = value;
    controls.roughness = value;
    controls.breathiness = value;
    controls.wallSoftness = value;
    controls.gasMix = value;
    controls.esophagusStiffness = value;
    return controls;
}

bool rendersFinite(const burp::BurpControls& controls)
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.setControls(controls);
    model.trigger(kNote, 1.0);
    const std::vector<double> held = renderSeconds(model, 0.5);
    model.release();
    const std::vector<double> tail = renderSeconds(model, 0.3);
    return allFinite(held) && allFinite(tail);
}

void testExtremes()
{
    BURP_CHECK(rendersFinite(unitControls(0.0)));
    BURP_CHECK(rendersFinite(unitControls(1.0)));

    burp::BurpControls highPressure;
    highPressure.abdominalPressure = burp::mmHgToPa(100.0);
    BURP_CHECK(rendersFinite(highPressure));

    burp::BurpControls lipsClosed;
    lipsClosed.mouth.lipArea = 0.0;
    BURP_CHECK(rendersFinite(lipsClosed));
}

} // namespace

int main()
{
    testSilentIdle();
    testBurpAndRelease();
    testLegato();
    testExtremes();
    return burp::test::failures();
}
