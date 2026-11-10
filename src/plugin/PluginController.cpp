#include "plugin/PluginController.h"
#include "gui/HelloWorldView.h"

#include <cstring>

using namespace Steinberg;

namespace burp {

IPlugView* PLUGIN_API PluginController::createView(FIDString name)
{
    if (name && std::strcmp(name, Vst::ViewType::kEditor) == 0) {
        return createHelloWorldView();
    }
    return nullptr;
}

} // namespace burp
