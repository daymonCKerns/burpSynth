#pragma once

// The plugin editor: a body cross-section (BodyView) that shows the live
// parameters as shapes and the gas moving through the model, XY pads for
// related parameter pairs and one labelled slider for each other parameter
// in paramTable(), built in code with VSTGUI (no resource files). An
// intermediate testing interface, not the final GUI.
//
// Moving a control edits the host parameter on every mouse move (beginEdit /
// performEdit / endEdit, so automation records it); host-side changes move
// the controls.
//
// Only this header is seen by the plugin layer, so VSTGUI types stay inside
// the gui library.

#include "pluginterfaces/gui/iplugview.h"

namespace Steinberg {
namespace Vst {
class EditController;
}
}

namespace burp {

// Creates the editor for `controller`. The caller owns the returned reference.
Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController* controller);

} // namespace burp
