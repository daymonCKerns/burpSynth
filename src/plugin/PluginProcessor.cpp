#include "plugin/PluginProcessor.h"
#include "plugin/PluginIds.h"

#include <cmath>
#include <limits>
#include <numbers>

using namespace Steinberg;

namespace burp {

PluginProcessor::PluginProcessor()
{
    setControllerClass(kControllerUID);
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

tresult PLUGIN_API PluginProcessor::setActive(TBool state)
{
    // Start every activation silent, e.g. after the host changes sample rate.
    phase_ = 0.0;
    amplitude_ = 0.0f;
    currentPitch_ = -1;
    return AudioEffect::setActive(state);
}

tresult PLUGIN_API PluginProcessor::process(Vst::ProcessData& data)
{
    Vst::IEventList* events = data.inputEvents;
    const int32 numEvents = events ? events->getEventCount() : 0;
    int32 nextEvent = 0;

    // Applies every event scheduled at or before sampleOffset, so notes start
    // and stop on the exact sample the host asked for.
    auto applyEventsUntil = [&](int32 sampleOffset) {
        Vst::Event event{};
        while (nextEvent < numEvents && events->getEvent(nextEvent, event) == kResultOk
               && event.sampleOffset <= sampleOffset) {
            handleEvent(event);
            ++nextEvent;
        }
    };

    // Hosts may call process() with no audio just to deliver events.
    if (data.numOutputs == 0 || data.numSamples == 0) {
        applyEventsUntil(std::numeric_limits<int32>::max());
        return kResultOk;
    }

    Vst::AudioBusBuffers& output = data.outputs[0];
    for (int32 sample = 0; sample < data.numSamples; ++sample) {
        applyEventsUntil(sample);

        const float value = amplitude_ * static_cast<float>(std::sin(2.0 * std::numbers::pi * phase_));
        phase_ += phaseIncrement_;
        if (phase_ >= 1.0) {
            phase_ -= 1.0;
        }

        for (int32 channel = 0; channel < output.numChannels; ++channel) {
            output.channelBuffers32[channel][sample] = value;
        }
    }

    output.silenceFlags = 0;
    return kResultOk;
}

void PluginProcessor::handleEvent(const Vst::Event& event)
{
    switch (event.type) {
    case Vst::Event::kNoteOnEvent:
        // Some hosts send note-on with velocity 0 to mean note-off.
        if (event.noteOn.velocity > 0.0f) {
            const double frequency = 440.0 * std::pow(2.0, (event.noteOn.pitch - 69) / 12.0);
            phaseIncrement_ = frequency / processSetup.sampleRate;
            amplitude_ = 0.25f * event.noteOn.velocity;
            currentPitch_ = event.noteOn.pitch;
        } else if (event.noteOn.pitch == currentPitch_) {
            amplitude_ = 0.0f;
            currentPitch_ = -1;
        }
        break;
    case Vst::Event::kNoteOffEvent:
        if (event.noteOff.pitch == currentPitch_) {
            amplitude_ = 0.0f;
            currentPitch_ = -1;
        }
        break;
    default:
        break;
    }
}

} // namespace burp
