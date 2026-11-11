#pragma once

// The plugin editor: a basic window with one labelled slider per parameter
// in paramTable(), built in code with VSTGUI (no resource files).
//
// Moving a slider edits the host parameter (beginEdit / performEdit /
// endEdit, so automation records it); host-side changes move the sliders.
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
