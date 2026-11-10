#pragma once

#include "pluginterfaces/gui/iplugview.h"

namespace burp {

// Creates the editor view the DAW embeds in its plugin window. The caller
// owns the returned reference.
Steinberg::IPlugView* createHelloWorldView();

} // namespace burp
