#pragma once

#include "pluginterfaces/base/funknown.h"

namespace burp {

// Unique IDs DAWs use to find these classes. They are saved inside DAW
// projects, so never change them once projects using the plugin exist.
// Generated with macOS `uuidgen`, one UUID per class, split into the four
// 32-bit words FUID takes:
//   processor:  5EF9727A-A801-4DA2-89AF-152C141B26D3
//   controller: DCDC2840-63CE-4DEB-938B-D64EE556F93F
static const Steinberg::FUID kProcessorUID(0x5EF9727A, 0xA8014DA2, 0x89AF152C, 0x141B26D3);
static const Steinberg::FUID kControllerUID(0xDCDC2840, 0x63CE4DEB, 0x938BD64E, 0xE556F93F);

} // namespace burp
