// Tests for the audio engine.
//
//   Params      The table has kParamCount rows with unique IDs matching the
//               ParamId enum; defaults lie inside their ranges;
//               toPlain(toNormalized(x)) = x; paramIndex of an unknown ID
//               is -1.
//   Mapping     makeControls on the defaults equals a default BurpControls
//               (field by field, within rounding); 100 mmHg maps to
//               13332 Pa; Pitch Settle at its maximum sets
//               pitchAlwaysCorrect.
//   NoteStack   Last-note priority; releasing the top note falls back;
//               re-pressing a held key moves it to the top; overflow drops
//               the oldest.
//   Decimator   A constant input comes out at the same level; a sine at
//               0.4 of the input rate is attenuated by at least 60 dB; a
//               sine at 0.05 of the input rate passes within 0.1 dB.
//   Engine      At 48 kHz: silence before any note; a note produces finite
//               output within -1..1 on every channel; after noteOff the
//               output returns to silence; an unknown parameter ID is
//               ignored.

#include "Check.h"

#include "audio_engine/ControlMapping.h"
#include "audio_engine/Decimator.h"
#include "audio_engine/Engine.h"
#include "audio_engine/NoteStack.h"
#include "audio_engine/Params.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <numbers>
#include <vector>

namespace {

using namespace burp;

std::size_t indexOf(ParamId id)
{
    return static_cast<std::size_t>(paramIndex(id));
}

void testParams()
{
    const std::array<ParamId, kParamCount> allIds {
        ParamId::AbdominalPressure, ParamId::JawOpening,       ParamId::TonguePosition,
        ParamId::TongueHeight,      ParamId::LipOpening,       ParamId::Velum,
        ParamId::PitchBend,         ParamId::GasAmount,        ParamId::LesOpeningTime,
        ParamId::LesOpeningArea,    ParamId::UesMass,          ParamId::UesDamping,
        ParamId::UesRestingGap,     ParamId::Roughness,        ParamId::Breathiness,
        ParamId::WallSoftness,      ParamId::PeristalsisStrength, ParamId::ReflexSpeed,
        ParamId::GasMix,            ParamId::OutputLevel,      ParamId::BendRange,
        ParamId::GlissandoRate,     ParamId::PitchSettle,      ParamId::ReleaseTime,
        ParamId::AccentAmount,      ParamId::BurstLength,      ParamId::SustainStrength,
        ParamId::BodySize,          ParamId::StomachStiffness,
        ParamId::EsophagusLength,   ParamId::EsophagusStiffness, ParamId::PharynxWidth,
    };

    const auto& table = paramTable();
    BURP_CHECK(static_cast<int>(table.size()) == kParamCount);

    for (std::size_t i = 0; i < table.size(); ++i) {
        for (std::size_t j = i + 1; j < table.size(); ++j) {
            BURP_CHECK(table[i].id != table[j].id);
        }
    }

    for (const ParamId id : allIds) {
        const int index = paramIndex(id);
        BURP_CHECK(index >= 0);
        if (index >= 0) {
            BURP_CHECK(table[static_cast<std::size_t>(index)].id == id);
        }
    }

    for (const ParamInfo& info : table) {
        BURP_CHECK(info.min < info.max);
        BURP_CHECK(info.defaultValue >= info.min && info.defaultValue <= info.max);

        const double span = info.max - info.min;
        const std::array<double, 4> plains {info.min, info.defaultValue, info.max,
                                            info.min + 0.37 * span};
        for (const double plain : plains) {
            BURP_CHECK_NEAR(toPlain(info, toNormalized(info, plain)), plain, 1.0e-12 * span);
        }
    }

    BURP_CHECK(paramIndex(7) == -1);
    BURP_CHECK(paramIndex(-1) == -1);
    BURP_CHECK(paramIndex(1000) == -1);
}

// Relative tolerance for a mapped field against its BurpControls default.
#define CHECK_FIELD(field) \
    BURP_CHECK_NEAR(mapped.field, nominal.field, 1.0e-9 * std::abs(nominal.field) + 1.0e-15)

void testMapping()
{
    const BurpControls mapped = makeControls(defaultParamValues());
    const BurpControls nominal {};

    CHECK_FIELD(abdominalPressure);
    CHECK_FIELD(mouth.jawOpening);
    CHECK_FIELD(mouth.tonguePosition);
    CHECK_FIELD(mouth.tongueHeight);
    CHECK_FIELD(mouth.lipArea);
    CHECK_FIELD(mouth.pharynxWidth);
    CHECK_FIELD(velumArea);
    CHECK_FIELD(pitchBendSemitones);
    CHECK_FIELD(gasVolume);
    CHECK_FIELD(lesOpeningArea);
    CHECK_FIELD(reflex.lesOpeningTime);
    CHECK_FIELD(reflex.releaseTime);
    CHECK_FIELD(reflex.speed);
    CHECK_FIELD(reflex.peristalsisStrength);
    CHECK_FIELD(reflex.burstTime);
    CHECK_FIELD(reflex.sustainStrength);
    CHECK_FIELD(uesMassScale);
    CHECK_FIELD(uesDampingRatio);
    CHECK_FIELD(uesRestArea);
    CHECK_FIELD(roughness);
    CHECK_FIELD(breathiness);
    CHECK_FIELD(wallSoftness);
    CHECK_FIELD(gasMix);
    CHECK_FIELD(glissandoRate);
    CHECK_FIELD(pitchSettleTime);
    BURP_CHECK(mapped.pitchAlwaysCorrect == nominal.pitchAlwaysCorrect);
    CHECK_FIELD(accentPressure);
    CHECK_FIELD(body.bodySize);
    CHECK_FIELD(body.esophagusLength);
    CHECK_FIELD(stomachElastance);
    CHECK_FIELD(esophagusStiffness);
    BURP_CHECK_NEAR(outputGain(defaultParamValues()), 1.0, 1.0e-12);

    ParamValues values = defaultParamValues();
    values[indexOf(ParamId::AbdominalPressure)] = 100.0;
    const double pressure = makeControls(values).abdominalPressure;
    std::printf("mapping: 100 mmHg = %.3f Pa\n", pressure);
    BURP_CHECK_NEAR(pressure, 13332.0, 1.0);

    const ParamInfo& settle = paramTable()[indexOf(ParamId::PitchSettle)];
    values[indexOf(ParamId::PitchSettle)] = toPlain(settle, 1.0);
    BURP_CHECK(makeControls(values).pitchAlwaysCorrect);
    values[indexOf(ParamId::PitchSettle)] = toPlain(settle, 0.99);
    BURP_CHECK(!makeControls(values).pitchAlwaysCorrect);
}

#undef CHECK_FIELD

void testNoteStack()
{
    NoteStack stack;
    BURP_CHECK(stack.empty());
    BURP_CHECK(stack.topKey() == -1);
    BURP_CHECK_NEAR(stack.topSemitones(), 0.0, 0.0);

    stack.push(60, 60.0);
    stack.push(64, 64.5);
    BURP_CHECK(stack.size() == 2);
    BURP_CHECK(stack.topKey() == 64);
    BURP_CHECK_NEAR(stack.topSemitones(), 64.5, 0.0);

    BURP_CHECK(stack.remove(64));
    BURP_CHECK(stack.topKey() == 60);
    BURP_CHECK_NEAR(stack.topSemitones(), 60.0, 0.0);
    BURP_CHECK(!stack.remove(64));

    stack.push(64, 64.0);
    stack.push(67, 67.0);
    stack.push(60, 60.0);
    BURP_CHECK(stack.size() == 3);
    BURP_CHECK(stack.topKey() == 60);
    BURP_CHECK(stack.remove(60));
    BURP_CHECK(stack.topKey() == 67);

    stack.clear();
    BURP_CHECK(stack.empty());
    for (int key = 0; key <= NoteStack::kCapacity; ++key) {
        stack.push(key, static_cast<double>(key));
    }
    BURP_CHECK(stack.size() == NoteStack::kCapacity);
    BURP_CHECK(stack.topKey() == NoteStack::kCapacity);
    BURP_CHECK(!stack.remove(0));
    BURP_CHECK(stack.remove(1));
}

constexpr int kSettleOutputs = 2 * Decimator::kHalfLength;
constexpr int kMeasuredOutputs = 4000;

// Decimates `input` in blocks of an odd size, so the state must carry
// across block boundaries.
std::vector<double> decimate(const std::vector<double>& input)
{
    constexpr int kBlock = 37;
    Decimator decimator;
    decimator.prepare();
    const int numOut = static_cast<int>(input.size() / 2);
    std::vector<double> output(static_cast<std::size_t>(numOut));
    for (int done = 0; done < numOut; done += kBlock) {
        const int count = std::min(kBlock, numOut - done);
        decimator.process(input.data() + 2 * done, output.data() + done, count);
    }
    return output;
}

// RMS of the measured part of a decimated signal, after the filter settles.
double settledRms(const std::vector<double>& output)
{
    double sum = 0.0;
    for (std::size_t i = kSettleOutputs; i < output.size(); ++i) {
        sum += output[i] * output[i];
    }
    return std::sqrt(sum / static_cast<double>(output.size() - kSettleOutputs));
}

// Gain in dB of the decimator for a sine at `fraction` of the input rate.
double sineGainDb(double fraction)
{
    std::vector<double> input(static_cast<std::size_t>(2 * (kSettleOutputs + kMeasuredOutputs)));
    for (std::size_t n = 0; n < input.size(); ++n) {
        input[n] = std::sin(2.0 * std::numbers::pi * fraction * static_cast<double>(n));
    }
    return 20.0 * std::log10(settledRms(decimate(input)) * std::numbers::sqrt2);
}

void testDecimator()
{
    const std::vector<double> constant(
        static_cast<std::size_t>(2 * (kSettleOutputs + kMeasuredOutputs)), 0.75);
    const std::vector<double> output = decimate(constant);
    double worst = 0.0;
    for (std::size_t i = kSettleOutputs; i < output.size(); ++i) {
        worst = std::max(worst, std::abs(output[i] - 0.75));
    }
    std::printf("decimator: constant 0.75, worst error %g\n", worst);
    BURP_CHECK_NEAR(worst, 0.0, 1.0e-9);

    const double stopDb = sineGainDb(0.4);
    const double passDb = sineGainDb(0.05);
    std::printf("decimator: gain at 0.4 fs %.1f dB, at 0.05 fs %.4f dB\n", stopDb, passDb);
    BURP_CHECK(stopDb <= -60.0);
    BURP_CHECK_NEAR(passDb, 0.0, 0.1);
}

constexpr double kHostRate = 48000.0;
constexpr int kHostBlock = 512;
constexpr int kChannels = 2;

struct RenderStats {
    double peak = 0.0;
    bool finite = true;
    bool channelsMatch = true;
};

RenderStats renderFor(Engine& engine, double seconds)
{
    std::array<std::vector<float>, kChannels> buffers;
    std::array<float*, kChannels> outputs {};
    for (std::size_t channel = 0; channel < buffers.size(); ++channel) {
        buffers[channel].assign(static_cast<std::size_t>(kHostBlock), 0.0f);
        outputs[channel] = buffers[channel].data();
    }

    RenderStats stats;
    const int blocks = static_cast<int>(std::ceil(seconds * kHostRate / kHostBlock));
    for (int block = 0; block < blocks; ++block) {
        engine.render(outputs.data(), kChannels, kHostBlock);
        for (std::size_t i = 0; i < buffers[0].size(); ++i) {
            const double first = static_cast<double>(buffers[0][i]);
            for (const std::vector<float>& buffer : buffers) {
                const double sample = static_cast<double>(buffer[i]);
                stats.finite = stats.finite && std::isfinite(sample);
                stats.channelsMatch = stats.channelsMatch && sample == first;
                stats.peak = std::max(stats.peak, std::abs(sample));
            }
        }
    }
    return stats;
}

void testEngine()
{
    Engine engine;
    engine.prepare(kHostRate, kHostBlock);

    const RenderStats before = renderFor(engine, 0.2);
    std::printf("engine: peak before any note %g\n", before.peak);
    BURP_CHECK(before.finite);
    BURP_CHECK(before.peak < 1.0e-6);

    engine.setParamNormalized(7, 1.0);
    engine.setParamNormalized(-1, 1.0);
    engine.setParamNormalized(1000, 0.0);
    const RenderStats unknown = renderFor(engine, 0.05);
    BURP_CHECK(unknown.finite);
    BURP_CHECK(unknown.peak < 1.0e-6);

    engine.noteOn(60, 60.0, 1.0);
    const RenderStats held = renderFor(engine, 1.0);
    std::printf("engine: peak while held %g\n", held.peak);
    BURP_CHECK(held.finite);
    BURP_CHECK(held.channelsMatch);
    BURP_CHECK(held.peak > 0.0);
    BURP_CHECK(held.peak <= 1.0);

    engine.noteOff(60);
    const RenderStats tail = renderFor(engine, 1.5);
    const RenderStats after = renderFor(engine, 0.5);
    std::printf("engine: peak in tail %g, after tail %g\n", tail.peak, after.peak);
    BURP_CHECK(tail.finite);
    BURP_CHECK(tail.peak <= 1.0);
    BURP_CHECK(after.finite);
    BURP_CHECK(after.peak < 1.0e-6);
}

} // namespace

int main()
{
    testParams();
    testMapping();
    testNoteStack();
    testDecimator();
    testEngine();
    return burp::test::failures();
}
