#pragma once

#include "public.sdk/source/vst/vsteditcontroller.h"

namespace burp {

// UI side of the plugin: owns parameters (none yet) and creates the editor.
class PluginController : public Steinberg::Vst::EditController {
public:
    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IEditController*>(new PluginController);
    }

    Steinberg::IPlugView* PLUGIN_API createView(Steinberg::FIDString name) SMTG_OVERRIDE;
};

} // namespace burp
