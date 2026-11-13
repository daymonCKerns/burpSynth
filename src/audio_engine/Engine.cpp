// Engine: note handling, oversampled rendering and output protection from
// Engine.h.

#include "audio_engine/Engine.h"

#include "audio_engine/ControlMapping.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace burp {

void Engine::prepare(double hostSampleRate, int maxBlockSize)
{
    hostSampleRate_ = hostSampleRate;
    maxBlockSize_ = std::max(maxBlockSize, 1);

    model_.setControls(makeControls(values_));
    model_.prepare(hostSampleRate * kOversampling);
    decimator_.prepare();

    modelBuffer_.assign(static_cast<std::size_t>(kOversampling * maxBlockSize_), 0.0);
    hostBuffer_.assign(static_cast<std::size_t>(maxBlockSize_), 0.0);
    reset();
}

void Engine::reset()
{
    model_.reset();
    decimator_.reset();
    notes_.clear();
}

void Engine::noteOn(int key, double semitones, double velocity)
{
    const double clampedVelocity = std::clamp(velocity, 0.0, 1.0);
    const bool wasEmpty = notes_.empty();
    notes_.push(key, semitones);
    if (wasEmpty) {
        // trigger() applies the body and snaps the smoothers to the controls,
        // so hand it the parameters set since the last render (e.g. earlier
        // in this block). Otherwise it starts from the old values and the
        // smoothers glide to the new ones during the burp's first 15 ms.
        model_.setControls(makeControls(values_));
        model_.trigger(semitones, clampedVelocity);
    } else {
        model_.changeNote(semitones, clampedVelocity);
    }
}

void Engine::noteOff(int key)
{
    const bool wasSounding = notes_.topKey() == key;
    if (!notes_.remove(key)) {
        return;
    }
    if (notes_.empty()) {
        model_.release();
    } else if (wasSounding) {
        model_.changeNote(notes_.topSemitones(), 0.0);
    }
}

void Engine::setParamNormalized(int rawId, double normalized)
{
    const int index = paramIndex(rawId);
    if (index < 0) {
        return;
    }
    const auto row = static_cast<std::size_t>(index);
    values_[row] = toPlain(paramTable()[row], normalized);
}

void Engine::render(float* const* outputs, int numChannels, int numFrames)
{
    if (maxBlockSize_ == 0) {
        // Not prepared: the model cannot run, so write silence.
        for (int channel = 0; outputs != nullptr && channel < numChannels; ++channel) {
            if (outputs[channel] != nullptr) {
                std::fill(outputs[channel], outputs[channel] + std::max(numFrames, 0), 0.0f);
            }
        }
        return;
    }

    model_.setControls(makeControls(values_));
    const double gain = outputGain(values_) / kFullScalePascals;

    for (int offset = 0; offset < numFrames; offset += maxBlockSize_) {
        renderChunk(outputs, numChannels, offset, std::min(numFrames - offset, maxBlockSize_), gain);
    }
}

const BurpModel& Engine::model() const
{
    return model_;
}

// Each value clamped to 0..1, in kMeters order (Meters.h).
MeterValues Engine::takeMeters()
{
    const auto unit = [](double value) { return std::clamp(value, 0.0, 1.0); };
    const ReflexOutput& reflex = model_.reflexOutput();
    const MeterValues values {
        unit(model_.stomachVolume() / kFullStomach),
        unit(reflex.lesOpen),
        unit(reflex.uesOpen),
        unit(model_.lesFlowAverage() / kFullFlow),
        unit(model_.uesFlowAverage() / kFullFlow),
        unit(reflex.waveCentre),
        unit(reflex.waveDepth),
        unit(peak_),
    };
    peak_ = 0.0;
    return values;
}

void Engine::renderChunk(float* const* outputs, int numChannels, int offset, int numFrames,
                         double gain)
{
    double* const modelOut = modelBuffer_.data();
    double* const hostOut = hostBuffer_.data();
    const int numModelFrames = kOversampling * numFrames;

    model_.render(modelOut, numModelFrames);
    for (int i = 0; i < numModelFrames; ++i) {
        modelOut[i] *= gain;
    }

    decimator_.process(modelOut, hostOut, numFrames);

    const bool finite = std::all_of(hostOut, hostOut + numFrames,
                                    [](double sample) { return std::isfinite(sample); });
    if (finite) {
        for (int i = 0; i < numFrames; ++i) {
            hostOut[i] = std::tanh(hostOut[i]);
            peak_ = std::max(peak_, std::abs(hostOut[i]));
        }
    } else {
        model_.reset();
        decimator_.reset();
        notes_.clear();
        std::fill(hostOut, hostOut + numFrames, 0.0);
    }

    if (outputs == nullptr) {
        return;
    }
    for (int channel = 0; channel < numChannels; ++channel) {
        float* const destination = outputs[channel];
        if (destination == nullptr) {
            continue;
        }
        for (int i = 0; i < numFrames; ++i) {
            destination[offset + i] = static_cast<float>(hostOut[i]);
        }
    }
}

} // namespace burp
