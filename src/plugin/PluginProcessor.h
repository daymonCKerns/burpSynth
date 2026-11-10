#pragma once

#include "pluginterfaces/vst/ivstevents.h"
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
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;

private:
    void handleEvent(const Steinberg::Vst::Event& event);

    // TEMPORARY test tone: a single sine voice standing in until burp_engine
    // exists. Replace with calls into the engine.
    double phase_ = 0.0;
    double phaseIncrement_ = 0.0;
    float amplitude_ = 0.0f;
    Steinberg::int16 currentPitch_ = -1;
};

} // namespace burp
