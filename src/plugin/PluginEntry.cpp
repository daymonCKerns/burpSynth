// The exported factory DAWs call to discover the plugin's classes.
#include "plugin/PluginController.h"
#include "plugin/PluginIds.h"
#include "plugin/PluginProcessor.h"

#include "public.sdk/source/main/pluginfactory.h"

using namespace Steinberg;

BEGIN_FACTORY_DEF("BurpSynth", "", "")

    // Registered as an audio effect so DAWs route audio into it for the
    // pass-through. Switch to Vst::PlugType::kInstrument once it synthesizes.
    DEF_CLASS2(INLINE_UID_FROM_FUID(burp::kProcessorUID),
               PClassInfo::kManyInstances,
               kVstAudioEffectClass,
               "BurpSynth",
               Vst::kDistributable,
               Vst::PlugType::kFx,
               "0.0.0",
               kVstVersionString,
               burp::PluginProcessor::createInstance)

    DEF_CLASS2(INLINE_UID_FROM_FUID(burp::kControllerUID),
               PClassInfo::kManyInstances,
               kVstComponentControllerClass,
               "BurpSynth Controller",
               0,
               "",
               "0.0.0",
               kVstVersionString,
               burp::PluginController::createInstance)

END_FACTORY
