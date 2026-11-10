#pragma once

#include "public.sdk/source/vst/vstaudioeffect.h"

namespace burp {

// Audio side of the plugin; the DAW calls process() on its audio thread.
class PluginProcessor : public Steinberg::Vst::AudioEffect {
public:
    PluginProcessor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new PluginProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
};

} // namespace burp
