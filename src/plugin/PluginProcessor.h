#pragma once

#include "audio_engine/Engine.h"
#include "audio_engine/Params.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>

namespace burp {

// One normalised (0..1) value per row of paramTable(), in table order.
using NormalizedValues = std::array<double, kParamCount>;

// The processor state saved in DAW projects, little endian:
//   int32 version (1), int32 count, then count x (int32 raw ID, double normalised value)
// PluginController::setComponentState reads the same format.
bool writeState(Steinberg::IBStream* stream, const NormalizedValues& values);

// Overwrites the rows found in the stream and keeps the others. Unknown IDs
// are skipped. On failure `values` is left unchanged.
bool readState(Steinberg::IBStream* stream, NormalizedValues& values);

// Audio side of the plugin; the DAW calls process() on its audio thread.
class PluginProcessor : public Steinberg::Vst::AudioEffect {
public:
    PluginProcessor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new PluginProcessor);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(Steinberg::int32 symbolicSampleSize) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) SMTG_OVERRIDE;

private:
    // The engine writes the same signal to every channel; more than two are
    // filled with silence.
    static constexpr Steinberg::int32 kMaxChannels = 2;

    void applyParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void pushAllParams();
    void handleEvent(const Steinberg::Vst::Event& event);
    void renderFrames(Steinberg::Vst::AudioBusBuffers& output, Steinberg::int32 start,
                      Steinberg::int32 frames);
    NormalizedValues loadNormalized() const;

    Engine engine_;

    // Largest block the engine was prepared for; 0 until setupProcessing.
    Steinberg::int32 maxBlock_ = 0;

    // Current normalised values, for getState. Atomic because getState and
    // setState run on the UI thread while process() runs.
    std::array<std::atomic<double>, kParamCount> normalized_;

    // Set by setState; the next process() pushes every value to the engine,
    // so the engine is only ever touched from the audio thread while active.
    std::atomic<bool> stateChanged_{false};
};

} // namespace burp
