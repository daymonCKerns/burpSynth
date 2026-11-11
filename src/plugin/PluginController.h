#pragma once

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "public.sdk/source/vst/vsteditcontroller.h"

namespace burp {

// UI side of the plugin: registers one host parameter per row of
// paramTable(), maps MIDI controllers to parameters and creates the editor.
class PluginController : public Steinberg::Vst::EditController, public Steinberg::Vst::IMidiMapping {
public:
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new PluginController);
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) SMTG_OVERRIDE;
    Steinberg::tresult PLUGIN_API setComponentState(Steinberg::IBStream* state) SMTG_OVERRIDE;
    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;

    // Mod wheel -> Abdominal Pressure, pitch wheel -> Pitch Bend, on event
    // bus 0 and any MIDI channel. The host turns them into parameter changes.
    Steinberg::tresult PLUGIN_API getMidiControllerAssignment(Steinberg::int32 busIndex,
                                                              Steinberg::int16 channel,
                                                              Steinberg::Vst::CtrlNumber midiControllerNumber,
                                                              Steinberg::Vst::ParamID& paramId) SMTG_OVERRIDE;

    // Adds IMidiMapping to the interfaces the host can query.
    OBJ_METHODS(PluginController, Steinberg::Vst::EditController)
    DEFINE_INTERFACES
        DEF_INTERFACE(Steinberg::Vst::IMidiMapping)
    END_DEFINE_INTERFACES(Steinberg::Vst::EditController)
    REFCOUNT_METHODS(Steinberg::Vst::EditController)
};

} // namespace burp
