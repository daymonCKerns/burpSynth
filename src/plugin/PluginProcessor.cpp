#include "plugin/PluginProcessor.h"
#include "plugin/PluginIds.h"

#include "base/source/fstreamer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace Steinberg;

namespace burp {

namespace {

constexpr int32 kStateVersion = 1;

} // namespace

bool writeState(IBStream* stream, const NormalizedValues& values)
{
    if (stream == nullptr) {
        return false;
    }

    IBStreamer streamer(stream, kLittleEndian);
    if (!streamer.writeInt32(kStateVersion) || !streamer.writeInt32(kParamCount)) {
        return false;
    }

    const auto& table = paramTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        if (!streamer.writeInt32(static_cast<int32>(table[i].id)) || !streamer.writeDouble(values[i])) {
            return false;
        }
    }
    return true;
}

bool readState(IBStream* stream, NormalizedValues& values)
{
    if (stream == nullptr) {
        return false;
    }

    IBStreamer streamer(stream, kLittleEndian);
    int32 version = 0;
    int32 count = 0;
    if (!streamer.readInt32(version) || version != kStateVersion || !streamer.readInt32(count)) {
        return false;
    }

    NormalizedValues result = values;
    for (int32 i = 0; i < count; ++i) {
        int32 rawId = 0;
        double value = 0.0;
        if (!streamer.readInt32(rawId) || !streamer.readDouble(value)) {
            return false;
        }
        const int index = paramIndex(static_cast<int>(rawId));
        if (index >= 0 && std::isfinite(value)) {
            result[static_cast<std::size_t>(index)] = std::clamp(value, 0.0, 1.0);
        }
    }

    values = result;
    return true;
}

PluginProcessor::PluginProcessor()
{
    setControllerClass(kControllerUID);

    const auto& table = paramTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        normalized_[i].store(toNormalized(table[i], table[i].defaultValue), std::memory_order_relaxed);
    }
    sentMeters_.fill(-1.0);
}

tresult PLUGIN_API PluginProcessor::initialize(FUnknown* context)
{
    const tresult result = AudioEffect::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    addEventInput(STR16("Event In"), 1);
    addAudioOutput(STR16("Stereo Out"), Vst::SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API PluginProcessor::setupProcessing(Vst::ProcessSetup& setup)
{
    const tresult result = AudioEffect::setupProcessing(setup);
    if (result != kResultOk) {
        return result;
    }
    if (setup.sampleRate <= 0.0 || setup.maxSamplesPerBlock <= 0) {
        return kInvalidArgument;
    }

    // The host never calls this while processing, so the engine can be
    // prepared and given the current values directly.
    engine_.prepare(setup.sampleRate, setup.maxSamplesPerBlock);
    maxBlock_ = setup.maxSamplesPerBlock;
    pushAllParams();
    return kResultOk;
}

tresult PLUGIN_API PluginProcessor::canProcessSampleSize(int32 symbolicSampleSize)
{
    return symbolicSampleSize == Vst::kSample32 ? kResultTrue : kResultFalse;
}

tresult PLUGIN_API PluginProcessor::setActive(TBool state)
{
    // Start every activation silent, e.g. after the host changes sample rate.
    if (maxBlock_ > 0) {
        engine_.reset();
    }
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API PluginProcessor::process(Vst::ProcessData& data)
{
    if (stateChanged_.exchange(false, std::memory_order_acquire)) {
        pushAllParams();
    }
    applyParameterChanges(data.inputParameterChanges);

    // Output bus 0, when the host gave 32-bit buffers.
    Vst::AudioBusBuffers* output = nullptr;
    if (data.numOutputs > 0 && data.outputs != nullptr && data.outputs[0].channelBuffers32 != nullptr
        && data.symbolicSampleSize == Vst::kSample32) {
        output = &data.outputs[0];
    }
    const int32 numSamples = std::max<int32>(data.numSamples, 0);
    const bool canRender = output != nullptr && maxBlock_ > 0;

    // Render up to each event, apply it, carry on, so notes start and stop on
    // the sample the host asked for. Offsets are clamped into the block and
    // never move backwards.
    Vst::IEventList* events = data.inputEvents;
    const int32 numEvents = events != nullptr ? events->getEventCount() : 0;
    int32 rendered = 0;
    for (int32 i = 0; i < numEvents; ++i) {
        Vst::Event event{};
        if (events->getEvent(i, event) != kResultOk) {
            continue;
        }
        const int32 offset = std::clamp(event.sampleOffset, rendered, numSamples);
        if (canRender) {
            renderFrames(*output, rendered, offset - rendered);
        }
        rendered = offset;
        handleEvent(event);
    }
    if (canRender) {
        renderFrames(*output, rendered, numSamples - rendered);
        sendMeters(data.outputParameterChanges);
    }

    if (output != nullptr) {
        // Silence the channels the engine did not write (all of them if it is
        // not prepared).
        const int32 firstSilent = canRender ? std::min(output->numChannels, kMaxChannels) : 0;
        for (int32 channel = firstSilent; channel < output->numChannels; ++channel) {
            if (Vst::Sample32* buffer = output->channelBuffers32[channel]) {
                std::fill_n(buffer, numSamples, 0.0f);
            }
        }
        output->silenceFlags = 0;
    }
    return kResultOk;
}

tresult PLUGIN_API PluginProcessor::setState(IBStream* state)
{
    NormalizedValues values = loadNormalized();
    if (!readState(state, values)) {
        return kResultFalse;
    }

    for (std::size_t i = 0; i < values.size(); ++i) {
        normalized_[i].store(values[i], std::memory_order_relaxed);
    }
    // Hand the values to the audio thread instead of touching the engine
    // here, since process() may be running.
    stateChanged_.store(true, std::memory_order_release);
    return kResultOk;
}

tresult PLUGIN_API PluginProcessor::getState(IBStream* state)
{
    return writeState(state, loadNormalized()) ? kResultOk : kResultFalse;
}

// Block-accurate, not sample-accurate: each parameter takes the last value
// the host queued for this block, before any of the block is rendered.
void PluginProcessor::applyParameterChanges(Vst::IParameterChanges* changes)
{
    if (changes == nullptr) {
        return;
    }

    const int32 numQueues = changes->getParameterCount();
    for (int32 i = 0; i < numQueues; ++i) {
        Vst::IParamValueQueue* queue = changes->getParameterData(i);
        if (queue == nullptr) {
            continue;
        }
        const int32 numPoints = queue->getPointCount();
        int32 sampleOffset = 0;
        Vst::ParamValue value = 0.0;
        if (numPoints <= 0 || queue->getPoint(numPoints - 1, sampleOffset, value) != kResultOk) {
            continue;
        }

        const int rawId = static_cast<int>(queue->getParameterId());
        const int index = paramIndex(rawId);
        if (index < 0) {
            continue;
        }
        normalized_[static_cast<std::size_t>(index)].store(value, std::memory_order_relaxed);
        engine_.setParamNormalized(rawId, value);
    }
}

void PluginProcessor::pushAllParams()
{
    const auto& table = paramTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        engine_.setParamNormalized(static_cast<int>(table[i].id), normalized_[i].load(std::memory_order_relaxed));
    }
}

void PluginProcessor::handleEvent(const Vst::Event& event)
{
    switch (event.type) {
    case Vst::Event::kNoteOnEvent: {
        const Vst::NoteOnEvent& note = event.noteOn;
        // Some hosts send note-on with velocity 0 to mean note-off.
        if (note.velocity > 0.0f) {
            // tuning is in cents.
            engine_.noteOn(note.pitch, note.pitch + static_cast<double>(note.tuning) / 100.0,
                           static_cast<double>(note.velocity));
        } else {
            engine_.noteOff(note.pitch);
        }
        break;
    }
    case Vst::Event::kNoteOffEvent:
        engine_.noteOff(event.noteOff.pitch);
        break;
    default:
        break;
    }
}

void PluginProcessor::renderFrames(Vst::AudioBusBuffers& output, int32 start, int32 frames)
{
    const int32 numChannels = std::min(output.numChannels, kMaxChannels);
    if (numChannels <= 0) {
        return;
    }

    std::array<float*, kMaxChannels> channels{};
    for (int32 channel = 0; channel < numChannels; ++channel) {
        Vst::Sample32* buffer = output.channelBuffers32[channel];
        if (buffer == nullptr) {
            return;
        }
        channels[static_cast<std::size_t>(channel)] = buffer + start;
    }

    // Engine::render takes at most the prepared block size per call.
    while (frames > 0) {
        const int32 chunk = std::min(frames, maxBlock_);
        engine_.render(channels.data(), numChannels, chunk);
        for (int32 channel = 0; channel < numChannels; ++channel) {
            channels[static_cast<std::size_t>(channel)] += chunk;
        }
        frames -= chunk;
    }
}

// One point at offset 0 per meter that changed. The host passes output
// parameters on to the controller, where the editor reads them.
void PluginProcessor::sendMeters(Vst::IParameterChanges* changes)
{
    const MeterValues values = engine_.takeMeters();
    if (changes == nullptr) {
        return;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (values[i] == sentMeters_[i]) {
            continue;
        }
        int32 queueIndex = 0;
        Vst::IParamValueQueue* queue =
            changes->addParameterData(static_cast<Vst::ParamID>(kMeters[i].id), queueIndex);
        int32 pointIndex = 0;
        if (queue != nullptr && queue->addPoint(0, values[i], pointIndex) == kResultOk) {
            sentMeters_[i] = values[i];
        }
    }
}

NormalizedValues PluginProcessor::loadNormalized() const
{
    NormalizedValues values{};
    for (std::size_t i = 0; i < values.size(); ++i) {
        values[i] = normalized_[i].load(std::memory_order_relaxed);
    }
    return values;
}

} // namespace burp
