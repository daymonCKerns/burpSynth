#include "plugin/PluginProcessor.h"
#include "plugin/PluginIds.h"

#include <algorithm>
#include <cstddef>
#include <cstring>

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

    addAudioInput(STR16("Stereo In"), Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Stereo Out"), Vst::SpeakerArr::kStereo);
    return kResultOk;
}

tresult PLUGIN_API PluginProcessor::process(Vst::ProcessData& data)
{
    if (data.numInputs == 0 || data.numOutputs == 0 || data.numSamples == 0) {
        return kResultOk;
    }

    const Vst::AudioBusBuffers& input = data.inputs[0];
    Vst::AudioBusBuffers& output = data.outputs[0];
    const std::size_t numBytes = sizeof(Vst::Sample32) * static_cast<std::size_t>(data.numSamples);
    const int32 numCopied = std::min(input.numChannels, output.numChannels);

    // Hosts may hand us the same buffer for input and output (in-place processing).
    for (int32 channel = 0; channel < numCopied; ++channel) {
        if (output.channelBuffers32[channel] != input.channelBuffers32[channel]) {
            std::memcpy(output.channelBuffers32[channel], input.channelBuffers32[channel], numBytes);
        }
    }
    for (int32 channel = numCopied; channel < output.numChannels; ++channel) {
        std::memset(output.channelBuffers32[channel], 0, numBytes);
    }

    output.silenceFlags = 0;
    return kResultOk;
}

} // namespace burp
