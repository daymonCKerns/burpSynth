// Tests for the sound source: Sphincter and PitchControl.
//
// The sphincter is run on its own between a constant upstream pressure and
// zero downstream pressure, at 96 kHz, fully released (openness 1).
//
//   Held shut    With openness 0 and 2 kPa upstream, no flow passes.
//   Oscillates   Released, with the default settings and 2.5 kPa upstream,
//                the lower mass keeps swinging after one second and the flow
//                is pulsed (it returns near zero every cycle or varies by
//                more than half its mean).
//   Pitch table  For Q = 0.5, 1 and 2, print the measured frequency next to
//                Q * kSphincterReferenceHz. Check each is within a factor of
//                1.5 of it and that frequency rises with Q.
//   Finite       Nothing becomes NaN or infinite over the pressure range
//                0 to 13 kPa and the mass-scale range 0.5 to 8.
//   PitchControl startNote jumps and changeNote glides at the set rate; fed
//                a sine at a known frequency, measuredHz matches it (+-1%);
//                the correction moves toward the target, stays within
//                +-kMaxCorrection, and freezes after the settle time.
//
// The printed pitch table is the tuning aid for the UES constants.

#include "Check.h"
#include "Spectrum.h"

#include "model/acoustics/Sphincter.h"
#include "model/control/PitchControl.h"
#include "model/math/Acoustics.h"
#include "model/math/Gas.h"
#include "model/math/Units.h"

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

constexpr double kSampleRate = 96000.0;
constexpr double kStep = 1.0 / kSampleRate;
constexpr double kBreathiness = 0.3;      // Breathiness default
constexpr double kMinSwing = 1.0e-5;      // m, x_1 peak to peak that counts as oscillating
constexpr int kSamplesPerTick = 96;       // 1 ms control tick at 96 kHz
constexpr double kTick = 0.001;           // s
constexpr double kSineAmplitude = 1.0e-4; // m, stand-in for the lower-mass swing

// What one sphincter run produced over its measured window.
struct Run {
    std::vector<double> displacement; // x_1, m
    std::vector<double> flow;         // m^3/s
    int closedSamples = 0;            // samples with a_min < kClosedArea
    bool finite = true;
};

struct Range {
    double min = 0.0;
    double max = 0.0;
    double mean = 0.0;
};

burp::SphincterSettings released(double tension, double massScale)
{
    burp::SphincterSettings settings;
    settings.tension = tension;
    settings.massScale = massScale;
    settings.openness = 1.0;
    return settings;
}

// Runs the sphincter between pressureUp and zero for `seconds` and records
// the last `measured` seconds.
Run runSphincter(const burp::SphincterSettings& settings, double pressureUp, double seconds,
                 double measured)
{
    const double density =
        burp::gasDensity(burp::kAir, burp::kAtmosphericPressure, burp::kBodyTemperature);

    burp::Sphincter sphincter;
    sphincter.prepare(kSampleRate);
    sphincter.setGas(density, burp::kViscosity);
    // Esophagus outlet and pharynx inlet: half of a 3 cm^2 cell 7 mm long each.
    sphincter.setNeighbourInertance(2.0 * burp::inertance(density, 0.0035, 3.0e-4));
    sphincter.setTurbulenceGain(kBreathiness);
    sphincter.reset();
    sphincter.setSettings(settings);

    const int total = static_cast<int>(seconds * kSampleRate);
    const int start = total - static_cast<int>(measured * kSampleRate);

    Run run;
    run.displacement.reserve(static_cast<std::size_t>(total - start));
    run.flow.reserve(static_cast<std::size_t>(total - start));
    for (int i = 0; i < total; ++i) {
        const double u = sphincter.step(kStep, pressureUp, 0.0);
        const double x = sphincter.lowerDisplacement();
        if (!std::isfinite(u) || !std::isfinite(x)) {
            run.finite = false;
            break;
        }
        if (i >= start) {
            run.displacement.push_back(x);
            run.flow.push_back(u);
            if (sphincter.minArea() < burp::kClosedArea) {
                ++run.closedSamples;
            }
        }
    }
    return run;
}

Range rangeOf(const std::vector<double>& values)
{
    Range range;
    if (values.empty()) {
        return range;
    }
    range.min = values.front();
    range.max = values.front();
    double sum = 0.0;
    for (const double value : values) {
        range.min = std::fmin(range.min, value);
        range.max = std::fmax(range.max, value);
        sum += value;
    }
    range.mean = sum / static_cast<double>(values.size());
    return range;
}

double swingOf(const Run& run)
{
    const Range range = rangeOf(run.displacement);
    return range.max - range.min;
}

double closedFraction(const Run& run)
{
    if (run.flow.empty()) {
        return 0.0;
    }
    return static_cast<double>(run.closedSamples) / static_cast<double>(run.flow.size());
}

// Feeds `seconds` of a sine at `hz` to the tracker, ticking it every
// millisecond. `phase` (cycles) carries over between calls.
void feedSine(burp::PitchControl& pitch, double hz, double seconds, double& phase)
{
    const int samples = static_cast<int>(seconds * kSampleRate);
    for (int i = 0; i < samples; ++i) {
        pitch.observe(kSineAmplitude * std::sin(2.0 * std::numbers::pi * phase));
        phase += hz / kSampleRate;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        if ((i + 1) % kSamplesPerTick == 0) {
            pitch.tick(kTick);
        }
    }
}

void testHeldShut()
{
    const Run run = runSphincter(burp::SphincterSettings {}, 2000.0, 1.0, 0.5);
    const Range flow = rangeOf(run.flow);
    std::printf("Held shut, 2 kPa: flow %g .. %g mL/s\n", flow.min * 1.0e6, flow.max * 1.0e6);
    BURP_CHECK(run.finite);
    BURP_CHECK(flow.min == 0.0 && flow.max == 0.0);
}

void testOscillates()
{
    const Run run = runSphincter(released(1.0, 3.0), 2500.0, 1.5, 0.5);
    const Range flow = rangeOf(run.flow);
    const double swing = swingOf(run);
    const double hz = burp::test::zeroCrossingFrequency(run.displacement, kSampleRate);

    std::printf("\nReleased, 2.5 kPa, default settings (last 0.5 s of 1.5 s)\n");
    std::printf("  frequency   %9.1f Hz\n", hz);
    std::printf("  x1 swing    %9.3f mm peak to peak\n", swing * 1.0e3);
    std::printf("  flow min    %9.1f mL/s\n", flow.min * 1.0e6);
    std::printf("  flow mean   %9.1f mL/s\n", flow.mean * 1.0e6);
    std::printf("  flow max    %9.1f mL/s\n", flow.max * 1.0e6);
    std::printf("  closed      %9.1f %% of the time\n", closedFraction(run) * 100.0);

    BURP_CHECK(run.finite);
    BURP_CHECK(swing > kMinSwing);
    BURP_CHECK(hz > 0.0);
    BURP_CHECK(flow.min < 0.1 * flow.max || flow.max - flow.min > 0.5 * flow.mean);
}

void testPitchTable()
{
    std::printf("\nPitch table (2.5 kPa, mass scale 3, damping ratio 0.12)\n");
    std::printf("     Q   expected Hz   measured Hz   ratio   swing mm   closed %%\n");

    const double tensions[] = { 0.5, 1.0, 2.0 };
    double previousHz = 0.0;
    for (const double tension : tensions) {
        const Run run = runSphincter(released(tension, 3.0), 2500.0, 1.5, 0.5);
        const double expected = tension * burp::kSphincterReferenceHz;
        const double hz = burp::test::zeroCrossingFrequency(run.displacement, kSampleRate);
        const double swing = swingOf(run);
        std::printf("%6.2f %13.1f %13.1f %7.3f %10.3f %10.1f\n", tension, expected, hz,
                    hz / expected, swing * 1.0e3, closedFraction(run) * 100.0);

        BURP_CHECK(run.finite);
        BURP_CHECK(swing > kMinSwing);
        BURP_CHECK(hz > expected / 1.5 && hz < expected * 1.5);
        BURP_CHECK(hz > previousHz);
        previousHz = hz;
    }
}

void testFinite()
{
    const double pressures[] = { 0.0, 1000.0, 2500.0, 5000.0, 9000.0, 13000.0 };
    const double massScales[] = { 0.5, 1.0, 3.0, 8.0 };

    std::printf("\nSustained oscillation by pressure and mass scale, Q = 1 (Hz, - = none)\n");
    std::printf("%6s", "kPa");
    for (const double massScale : massScales) {
        std::printf("    x%-4.1f", massScale);
    }
    std::printf("\n");

    for (const double pressure : pressures) {
        std::printf("%6.1f", pressure * 1.0e-3);
        for (const double massScale : massScales) {
            const Run run = runSphincter(released(1.0, massScale), pressure, 0.5, 0.25);
            BURP_CHECK(run.finite);
            if (!run.finite) {
                std::printf("%9s", "blowup");
            } else if (swingOf(run) > kMinSwing) {
                std::printf("%9.1f",
                            burp::test::zeroCrossingFrequency(run.displacement, kSampleRate));
            } else {
                std::printf("%9s", "-");
            }
        }
        std::printf("\n");
    }
}

void testGlide()
{
    burp::PitchControl pitch;
    pitch.prepare(kSampleRate);
    burp::PitchSettings settings;
    settings.glissandoRate = 60.0;
    pitch.setSettings(settings);

    pitch.startNote(60.0);
    pitch.tick(kTick);
    BURP_CHECK_NEAR(pitch.targetHz(), burp::midiToHz(60.0), 1.0e-9);

    // 100 ms at 60 st/s is 6 semitones of the way from 60 to 72.
    pitch.changeNote(72.0);
    for (int i = 0; i < 100; ++i) {
        pitch.tick(kTick);
    }
    const double halfwayHz = pitch.targetHz();
    BURP_CHECK_NEAR(halfwayHz, burp::midiToHz(66.0), 1.0e-6 * burp::midiToHz(66.0));

    for (int i = 0; i < 200; ++i) {
        pitch.tick(kTick);
    }
    const double tension = pitch.tick(kTick);
    BURP_CHECK_NEAR(pitch.targetHz(), burp::midiToHz(72.0), 1.0e-9);
    BURP_CHECK_NEAR(tension, burp::midiToHz(72.0) / burp::kSphincterReferenceHz, 1.0e-12);

    std::printf("\nGlide 60 -> 72 at 60 st/s: %.2f Hz after 100 ms (expect %.2f), Q %.4f at the end\n",
                halfwayHz, burp::midiToHz(66.0), tension);
}

void testMeasurement()
{
    burp::PitchControl pitch;
    pitch.prepare(kSampleRate);
    pitch.startNote(burp::hzToMidi(157.0));

    double phase = 0.0;
    feedSine(pitch, 157.0, 0.5, phase);
    std::printf("Sine at 157 Hz: measured %.2f Hz\n", pitch.measuredHz());
    BURP_CHECK_NEAR(pitch.measuredHz(), 157.0, 0.01 * 157.0);
}

void testCorrection()
{
    // Closed loop: a stand-in sphincter that sounds at 0.9 Q kSphincterReferenceHz.
    // The correction should settle at 12 log2(1 / 0.9) semitones.
    constexpr double kPlantGain = 0.9;
    const double expected = -12.0 * std::log2(kPlantGain);

    burp::PitchControl pitch;
    pitch.prepare(kSampleRate);
    burp::PitchSettings settings;
    settings.settleTime = 0.5;
    pitch.setSettings(settings);
    pitch.startNote(50.0);

    double tension = pitch.tick(0.0);
    double phase = 0.0;
    const int samples = static_cast<int>(0.4 * kSampleRate);
    for (int i = 0; i < samples; ++i) {
        pitch.observe(kSineAmplitude * std::sin(2.0 * std::numbers::pi * phase));
        phase += kPlantGain * tension * burp::kSphincterReferenceHz / kSampleRate;
        if (phase >= 1.0) {
            phase -= 1.0;
        }
        if ((i + 1) % kSamplesPerTick == 0) {
            tension = pitch.tick(kTick);
        }
    }
    std::printf("Closed loop, plant 10%% flat: correction %.3f st (expect %.3f), "
                "measured %.2f Hz, target %.2f Hz\n",
                pitch.correctionSemitones(), expected, pitch.measuredHz(), pitch.targetHz());
    BURP_CHECK_NEAR(pitch.correctionSemitones(), expected, 0.05);
    BURP_CHECK_NEAR(pitch.measuredHz(), pitch.targetHz(), 0.01 * pitch.targetHz());

    // 6 semitones flat, always correcting: the correction runs into its clamp.
    burp::PitchControl clamped;
    clamped.prepare(kSampleRate);
    settings.alwaysCorrect = true;
    clamped.setSettings(settings);
    clamped.startNote(50.0);
    phase = 0.0;
    feedSine(clamped, burp::midiToHz(44.0), 0.5, phase);
    std::printf("6 st flat, always correcting: correction %.3f st (clamp %.1f)\n",
                clamped.correctionSemitones(), burp::kMaxCorrection);
    BURP_CHECK(clamped.correctionSemitones() <= burp::kMaxCorrection);
    BURP_CHECK_NEAR(clamped.correctionSemitones(), burp::kMaxCorrection, 1.0e-12);

    // An octave below the target is a subharmonic: ignored.
    burp::PitchControl ignored;
    ignored.prepare(kSampleRate);
    ignored.setSettings(settings);
    ignored.startNote(50.0);
    phase = 0.0;
    feedSine(ignored, burp::midiToHz(38.0), 0.5, phase);
    std::printf("12 st flat: correction %.3f st (ignored)\n", ignored.correctionSemitones());
    BURP_CHECK(ignored.correctionSemitones() == 0.0);
}

void testFreeze()
{
    burp::PitchControl pitch;
    pitch.prepare(kSampleRate);
    burp::PitchSettings settings;
    settings.settleTime = 0.05;
    pitch.setSettings(settings);
    pitch.startNote(50.0);

    // 1 semitone flat: the correction rises while the window is open, then holds.
    double phase = 0.0;
    feedSine(pitch, burp::midiToHz(49.0), 0.1, phase);
    const double atClose = pitch.correctionSemitones();
    feedSine(pitch, burp::midiToHz(49.0), 0.4, phase);
    const double later = pitch.correctionSemitones();
    std::printf("Settle 50 ms, 1 st flat: correction %.3f st at 0.1 s, %.3f st at 0.5 s\n",
                atClose, later);
    BURP_CHECK(atClose > 0.0 && atClose < burp::kMaxCorrection);
    BURP_CHECK(later == atClose);
}

} // namespace

int main()
{
    testHeldShut();
    testOscillates();
    testPitchTable();
    testFinite();
    testGlide();
    testMeasurement();
    testCorrection();
    testFreeze();
    return burp::test::failures();
}
