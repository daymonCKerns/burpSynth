#include "plugin/PluginController.h"
#include "plugin/PluginProcessor.h"

#include "audio_engine/Params.h"
#include "gui/Editor.h"

#include "pluginterfaces/base/ustring.h"
#include "pluginterfaces/vst/ivstmidicontrollers.h"

#include <cstddef>
#include <cstring>

using namespace Steinberg;

namespace burp {

tresult PLUGIN_API PluginController::initialize(FUnknown* context)
{
    const tresult result = EditController::initialize(context);
    if (result != kResultOk) {
        return result;
    }

    // Table order, so hosts list the parameters in the documented order.
    // Names and units are ASCII; the host wants UTF-16.
    for (const ParamInfo& row : paramTable()) {
        const UString128 title(row.name);
        const UString128 units(row.units);
        parameters.addParameter(new Vst::RangeParameter(title,
                                                        static_cast<Vst::ParamID>(row.id),
                                                        units,
                                                        row.min,
                                                        row.max,
                                                        row.defaultValue,
                                                        Vst::kStepCountContinuous,
                                                        Vst::ParameterInfo::kCanAutomate));
    }
    return kResultOk;
}

// Called with the processor's state (see PluginProcessor::getState) when a
// project or preset loads, so the controller shows the same values.
tresult PLUGIN_API PluginController::setComponentState(IBStream* state)
{
    const auto& table = paramTable();
    NormalizedValues values{};
    for (std::size_t i = 0; i < table.size(); ++i) {
        values[i] = getParamNormalized(static_cast<Vst::ParamID>(table[i].id));
    }

    if (!readState(state, values)) {
        return kResultFalse;
    }

    for (std::size_t i = 0; i < table.size(); ++i) {
        setParamNormalized(static_cast<Vst::ParamID>(table[i].id), values[i]);
    }
    return kResultOk;
}

IPlugView* PLUGIN_API PluginController::createView(FIDString name)
{
    if (name != nullptr && std::strcmp(name, Vst::ViewType::kEditor) == 0) {
        return createEditor(this);
    }
    return nullptr;
}

tresult PLUGIN_API PluginController::getMidiControllerAssignment(int32 busIndex,
                                                                 int16 /*channel*/,
                                                                 Vst::CtrlNumber midiControllerNumber,
                                                                 Vst::ParamID& paramId)
{
    if (busIndex != 0) {
        return kResultFalse;
    }

    switch (midiControllerNumber) {
    case Vst::kCtrlModWheel:
        paramId = static_cast<Vst::ParamID>(ParamId::AbdominalPressure);
        return kResultTrue;
    case Vst::kPitchBend:
        paramId = static_cast<Vst::ParamID>(ParamId::PitchBend);
        return kResultTrue;
    default:
        return kResultFalse;
    }
}

} // namespace burp
