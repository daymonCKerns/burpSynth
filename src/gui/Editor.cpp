// Editor: the VSTGUI slider-per-parameter window from Editor.h.
//
// One entry per row of paramTable(), in table order, filled down three
// columns: a text label (name, plus units in parentheses) and a horizontal
// slider whose tag is the raw parameter ID. Sliders work in normalised 0..1,
// the same values the host sees, and are drawn with CSlider's built-in
// frame/back/value style instead of bitmaps.
//
// Edit gestures: CSlider calls CControl::beginEdit/endEdit on mouse down/up,
// mouse wheel and default-value clicks. CControl forwards those through
// CFrame::beginEdit/endEdit to VSTGUIEditor::beginEdit/endEdit, which call the
// controller's beginEdit/endEdit. So this file only sends performEdit; doing
// beginEdit/endEdit here as well would send each one to the host twice.
//
// Host-side changes (automation, MIDI mapping, state load): VSTGUIEditor owns
// a VSTGUI timer that it starts after open() succeeds and stops before
// close(). Each tick calls notify(), which copies the controller's values into
// every slider the user is not currently dragging.

#include "gui/Editor.h"

#include "audio_engine/Params.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/controls/cparamdisplay.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace burp {

namespace {

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;

// Layout, in pixels. Rows are filled top to bottom, then the next column.
constexpr int kColumns = 3;
constexpr int kRows = (kParamCount + kColumns - 1) / kColumns;
constexpr int kMargin = 12;
constexpr int kRowHeight = 24;
constexpr int kLabelWidth = 170;
constexpr int kSliderWidth = 130;
constexpr int kSliderHeight = 14;
constexpr int kColumnGap = 20;
constexpr int kColumnWidth = kLabelWidth + kSliderWidth;
constexpr int kWindowWidth = 2 * kMargin + kColumns * kColumnWidth + (kColumns - 1) * kColumnGap;
constexpr int kWindowHeight = 2 * kMargin + kRows * kRowHeight;

// Period of the host-to-slider poll: about 30 times a second.
constexpr int kPollMilliseconds = 33;

constexpr VSTGUI::CColor kBackgroundColour(30, 30, 34);
constexpr VSTGUI::CColor kTextColour(220, 220, 220);
constexpr VSTGUI::CColor kSliderFrameColour(90, 90, 98);
constexpr VSTGUI::CColor kSliderBackColour(45, 45, 50);
constexpr VSTGUI::CColor kSliderValueColour(120, 170, 230);

class Editor : public Steinberg::Vst::VSTGUIEditor, public VSTGUI::IControlListener {
public:
    explicit Editor(EditController* editController);

    // VSTGUIEditor: build the views and open the frame on the host's view.
    bool PLUGIN_API open(void* parent, const VSTGUI::PlatformType& platformType) override;
    // VSTGUIEditor: close the frame, which releases every view.
    void PLUGIN_API close() override;

    // IControlListener: the user moved a slider.
    void valueChanged(VSTGUI::CControl* control) override;

    // CBaseObject: VSTGUIEditor's timer fired.
    VSTGUI::CMessageResult notify(VSTGUI::CBaseObject* sender, const char* message) override;

private:
    void syncSlidersFromController();

    // Borrowed, in paramTable() order. After addView the frame owns each view
    // (it keeps the reference from `new` and releases it in CFrame::close()).
    // All null while the editor is closed.
    std::array<VSTGUI::CSlider*, kParamCount> sliders_{};
};

Editor::Editor(EditController* editController)
    : VSTGUIEditor(editController)
{
    // Hosts ask for the size (getSize) before attaching the view.
    setRect(Steinberg::ViewRect(0, 0, kWindowWidth, kWindowHeight));
    setIdleRate(kPollMilliseconds);
}

bool PLUGIN_API Editor::open(void* parent, const VSTGUI::PlatformType& platformType)
{
    EditController* editController = getController();
    if (frame != nullptr || editController == nullptr) {
        return false;
    }

    frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, kWindowWidth, kWindowHeight), this);
    frame->setBackgroundColor(kBackgroundColour);

    const auto& table = paramTable();
    for (std::size_t i = 0; i < table.size(); ++i) {
        const ParamInfo& info = table[i];
        const int slot = static_cast<int>(i);
        const int x = kMargin + (slot / kRows) * (kColumnWidth + kColumnGap);
        const int y = kMargin + (slot % kRows) * kRowHeight;

        std::string text = info.name;
        if (info.units != nullptr && info.units[0] != '\0') {
            text += " (";
            text += info.units;
            text += ")";
        }

        auto* label = new VSTGUI::CTextLabel(
            VSTGUI::CRect(x, y, x + kLabelWidth, y + kRowHeight), text.c_str());
        label->setTransparency(true);
        label->setStyle(VSTGUI::CParamDisplay::kNoFrame);
        label->setFontColor(kTextColour);
        label->setHoriAlign(VSTGUI::kLeftText);
        label->setMouseEnabled(false);
        frame->addView(label);

        const int sliderLeft = x + kLabelWidth;
        const int sliderRight = sliderLeft + kSliderWidth;
        const int sliderTop = y + (kRowHeight - kSliderHeight) / 2;
        const auto tag = static_cast<std::int32_t>(info.id);

        // iMinPos/iMaxPos are the slider's own left and right edges, so the
        // whole width maps to 0..1. Null handle and background bitmaps: the
        // draw style below does all the drawing.
        auto* slider = new VSTGUI::CSlider(
            VSTGUI::CRect(sliderLeft, sliderTop, sliderRight, sliderTop + kSliderHeight),
            this, tag, sliderLeft, sliderRight, nullptr, nullptr);
        slider->setDrawStyle(VSTGUI::CSlider::kDrawFrame | VSTGUI::CSlider::kDrawBack
                             | VSTGUI::CSlider::kDrawValue);
        slider->setFrameColor(kSliderFrameColour);
        slider->setBackColor(kSliderBackColour);
        slider->setValueColor(kSliderValueColour);
        // The value VSTGUI's default-value click (Cmd + click on macOS)
        // resets to.
        slider->setDefaultValue(static_cast<float>(toNormalized(info, info.defaultValue)));
        slider->setValueNormalized(
            static_cast<float>(editController->getParamNormalized(static_cast<ParamID>(tag))));
        frame->addView(slider);
        sliders_[i] = slider;
    }

    if (!frame->open(parent, platformType)) {
        sliders_.fill(nullptr);
        frame->forget(); // last reference: deletes the frame and its views
        frame = nullptr;
        return false;
    }
    return true;
}

void PLUGIN_API Editor::close()
{
    sliders_.fill(nullptr);
    if (frame != nullptr) {
        frame->close(); // removes and releases every view, then the frame
        frame = nullptr;
    }
}

void Editor::valueChanged(VSTGUI::CControl* control)
{
    EditController* editController = getController();
    if (control == nullptr || editController == nullptr) {
        return;
    }

    // beginEdit/endEdit come from VSTGUIEditor (see the top of this file).
    const auto tag = static_cast<ParamID>(control->getTag());
    const auto value = static_cast<double>(control->getValueNormalized());
    editController->setParamNormalized(tag, value);
    editController->performEdit(tag, value);
}

VSTGUI::CMessageResult Editor::notify(VSTGUI::CBaseObject* sender, const char* message)
{
    // VSTGUIEditor compares the message pointer the same way.
    if (message == VSTGUI::CVSTGUITimer::kMsgTimer) {
        syncSlidersFromController();
    }
    // The base redraws dirty views (CFrame::idle) on the same tick.
    return VSTGUIEditor::notify(sender, message);
}

void Editor::syncSlidersFromController()
{
    EditController* editController = getController();
    if (editController == nullptr) {
        return;
    }

    for (VSTGUI::CSlider* slider : sliders_) {
        if (slider == nullptr || slider->isEditing()) {
            continue;
        }
        const auto value = static_cast<float>(
            editController->getParamNormalized(static_cast<ParamID>(slider->getTag())));
        if (slider->getValueNormalized() != value) {
            slider->setValueNormalized(value);
            slider->setDirty();
        }
    }
}

} // namespace

Steinberg::IPlugView* createEditor(Steinberg::Vst::EditController* controller)
{
    return new Editor(controller);
}

} // namespace burp
