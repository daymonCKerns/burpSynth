// End-to-end tests for BurpModel at 96 kHz with default controls.
//
//   Silent idle   Before any trigger the output is exactly zero.
//   Burp          trigger(note 45, velocity 0.8), render 1 s: every sample is
//                 finite, the peak is above 1e-4 Pa (audible), and the
//                 stomach has lost gas.
//   Release       release(), render 1.5 s: the last 100 ms are silent and
//                 the model is asleep.
//   Legato        During a held note, changeNote an octave down lowers the
//                 measured pitch.
//   Short note    A 100 ms note, then release: the model falls asleep within
//                 1 s. Prints the peak level every 50 ms after the release.
//   Trace         Not a check. Prints the stomach, esophagus and pharynx
//                 pressures, the UES opening and the LES and UES flows through
//                 a held burp, to find what causes loud bursts in the output.
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
    burp::BurpControls controls;
    controls.reflex.sustainStrength = 0.0; // no refill, so the stomach must empty
    model.setControls(controls);
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

// With the default sustain strength a held note must still sound after 4 s.
void testSustain()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.trigger(kNote, kVelocity);
    const std::vector<double> held = renderSeconds(model, 4.0);
    const double late = peak(lastSeconds(held, 0.5));
    std::printf("sustain: peak in the last 0.5 s of 4 s %.3g Pa, stomach %.1f Pa\n", late,
                model.stomachPressure());
    BURP_CHECK(allFinite(held));
    BURP_CHECK(late > 1.0e-3);
}

void testLegato()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.trigger(kNote, kVelocity);
    renderSeconds(model, 0.4);
    const double highHz = model.measuredHz();
    std::printf("legato: note %.0f  %.1f Hz measured, %.1f Hz target\n", kNote, highHz, model.targetHz());

    // An octave, not two: note 21 (27.5 Hz) is below what the UES can play.
    model.changeNote(kNote - 12.0, 0.0);
    renderSeconds(model, 0.6);
    const double lowHz = model.measuredHz();
    std::printf("legato: note %.0f  %.1f Hz measured, %.1f Hz target\n", kNote - 12.0, lowHz,
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

constexpr int kChunk = 96;            // 1 ms at 96 kHz
constexpr double kAudiblePa = 1.0e-3; // the tail counts as gone below this (-66 dB re 2 Pa)

void testShortNote()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.trigger(kNote, kVelocity);
    renderSeconds(model, 0.1);
    model.release();

    std::printf("\nshort note: held 100 ms, released; peak Pa in each 50 ms after the release\n");
    std::vector<double> chunk(kChunk);
    double windowPeak = 0.0;
    double lastAudibleMs = 0.0;
    double asleepMs = -1.0;
    bool finite = true;
    for (int ms = 1; ms <= 2000; ++ms) {
        model.render(chunk.data(), kChunk);
        finite = finite && allFinite(chunk);
        const double chunkPeak = peak(chunk);
        windowPeak = std::max(windowPeak, chunkPeak);
        if (chunkPeak > kAudiblePa) {
            lastAudibleMs = ms;
        }
        if (asleepMs < 0.0 && model.isAsleep()) {
            asleepMs = ms;
        }
        if (ms % 50 == 0 && ms <= 1000) {
            std::printf("  %4d ms  %.4g\n", ms, windowPeak);
            windowPeak = 0.0;
        }
    }
    std::printf("short note: last sample above %g Pa at %.0f ms after the release, asleep at %.0f ms\n",
                kAudiblePa, lastAudibleMs, asleepMs);
    BURP_CHECK(finite);
    BURP_CHECK(asleepMs > 0.0 && asleepMs < 1000.0);
}

void traceBurp()
{
    burp::BurpModel model;
    model.prepare(kSampleRate);
    model.trigger(kNote, kVelocity);

    std::printf("\ntrace: note %.0f, velocity %.1f, held; every 10 ms, and every 2 ms from 490 to 520 ms\n",
                kNote, kVelocity);
    std::printf("    ms  peak Pa  stomach Pa  esophagus Pa  pharynx Pa  UES mm2  UES mL/s  LES mL/s\n");
    std::vector<double> chunk(kChunk);
    double rowPeak = 0.0;
    bool finite = true;
    for (int ms = 1; ms <= 800; ++ms) {
        model.render(chunk.data(), kChunk);
        finite = finite && allFinite(chunk);
        rowPeak = std::max(rowPeak, peak(chunk));
        if (ms % 10 == 0 || (ms >= 490 && ms <= 520 && ms % 2 == 0)) {
            std::printf("%6d  %7.3g  %10.1f  %12.1f  %10.1f  %7.3f  %8.1f  %8.1f\n", ms, rowPeak,
                        model.stomachPressure(), model.esophagusPressure(), model.pharynxPressure(),
                        model.uesArea() * 1.0e6, model.uesFlow() * 1.0e6, model.lesFlow() * 1.0e6);
            rowPeak = 0.0;
        }
    }
    BURP_CHECK(finite);
}

} // namespace

int main()
{
    testSilentIdle();
    testBurpAndRelease();
    testSustain();
    testLegato();
    testExtremes();
    testShortNote();
    traceBurp();
    return burp::test::failures();
}
