// Editor: the VSTGUI window from Editor.h.
//
// Left: the body cross-section (BodyView), which shows the live parameters
// and the gas moving through the model. Right: one XY pad per pair of related
// parameters (kPads, kPadColumns to a row), x left to right and y bottom to
// top, both in normalised 0..1. Below them: a text label (name, plus units in
// parentheses) and a horizontal slider for every other row of paramTable(),
// in table order, filled down two columns. Sliders and pads are drawn with
// plain colours instead of bitmaps.
//
// Every control edits the host parameter on every mouse move, so the model
// follows a drag while it happens, not only on release.
//
// Edit gestures: CSlider calls CControl::beginEdit/endEdit on mouse down/up,
// mouse wheel and default-value clicks. CControl forwards those through
// CFrame::beginEdit/endEdit to VSTGUIEditor::beginEdit/endEdit, which call the
// controller's beginEdit/endEdit. So valueChanged() only sends performEdit;
// doing beginEdit/endEdit there as well would send each one to the host twice.
// XYPad is a plain CView with two parameters, so it calls the controller's
// beginEdit/performEdit/endEdit itself.
//
// Host-side changes (automation, MIDI mapping, state load): VSTGUIEditor owns
// a VSTGUI timer that it starts after open() succeeds and stops before
// close(). Each tick calls notify(), which copies the controller's values into
// every control the user is not currently dragging, and advances the body
// view, which reads the meters the processor sends (audio_engine/Meters.h).

#include "gui/Editor.h"

#include "gui/BodyView.h"

#include "audio_engine/Params.h"

#include "public.sdk/source/vst/vstguieditor.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cframe.h"
#include "vstgui/lib/crect.h"
#include "vstgui/lib/cview.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/controls/cparamdisplay.h"
#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/controls/ctextlabel.h"
#include "vstgui/lib/controls/icontrollistener.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace burp {

namespace {

using Steinberg::Vst::EditController;
using Steinberg::Vst::ParamID;

struct PadInfo {
    const char* title;
    ParamId x;
    const char* xName; // short axis name for the caption
    ParamId y;
    const char* yName;
};

// Parameter pairs shown as XY pads, left to right.
constexpr std::array<PadInfo, 5> kPads {{
    { "Tongue", ParamId::TonguePosition, "Position", ParamId::TongueHeight, "Height" },
    { "Mouth", ParamId::JawOpening, "Jaw", ParamId::LipOpening, "Lips" },
    { "LES", ParamId::LesOpeningTime, "Time", ParamId::LesOpeningArea, "Area" },
    { "UES", ParamId::UesMass, "Mass", ParamId::UesDamping, "Damping" },
    { "Texture", ParamId::Roughness, "Rough", ParamId::Breathiness, "Breath" },
}};
constexpr int kPadCount = static_cast<int>(kPads.size());
constexpr int kSliderCount = kParamCount - 2 * kPadCount;

// Layout, in pixels. The body view fills the left column. On the right, pads
// are filled left to right, then the next row; slider rows top to bottom,
// then the next column.
constexpr int kMargin = 12;
constexpr int kBodyWidth = 420;
constexpr int kBodyGap = 16;
constexpr int kControlsLeft = kMargin + kBodyWidth + kBodyGap;
constexpr int kControlsWidth = 724;

constexpr int kPadColumns = 3;
constexpr int kPadRows = (kPadCount + kPadColumns - 1) / kPadColumns;
constexpr int kPadGap = 16;
constexpr int kPadWidth = (kControlsWidth - (kPadColumns - 1) * kPadGap) / kPadColumns;
constexpr int kPadHeight = 110;
constexpr int kCaptionHeight = 18; // title above each pad, axis names below
constexpr int kPadBlockHeight = kCaptionHeight + kPadHeight + kCaptionHeight;
constexpr int kPadRowGap = 8;

constexpr int kColumns = 2;
constexpr int kRows = (kSliderCount + kColumns - 1) / kColumns;
constexpr int kRowHeight = 24;
constexpr int kLabelWidth = 200;
constexpr int kSliderWidth = 140;
constexpr int kSliderHeight = 14;
constexpr int kColumnGap = 24;
constexpr int kColumnWidth = kLabelWidth + kSliderWidth;
constexpr int kSlidersTop = kMargin + kPadRows * kPadBlockHeight + (kPadRows - 1) * kPadRowGap + kMargin;
constexpr int kControlsBottom = kSlidersTop + kRows * kRowHeight;
static_assert(kColumns * kColumnWidth + (kColumns - 1) * kColumnGap <= kControlsWidth);

constexpr int kBodyHeight = kControlsBottom - kMargin;
constexpr int kWindowWidth = kControlsLeft + kControlsWidth + kMargin;
constexpr int kWindowHeight = kControlsBottom + kMargin;

constexpr double kPadDotRadius = 5.0;

// Period of the host-to-control poll: about 30 times a second.
constexpr int kPollMilliseconds = 33;

constexpr VSTGUI::CColor kBackgroundColour(30, 30, 34);
constexpr VSTGUI::CColor kTextColour(220, 220, 220);
constexpr VSTGUI::CColor kDimTextColour(150, 150, 158);
constexpr VSTGUI::CColor kSliderFrameColour(90, 90, 98);
constexpr VSTGUI::CColor kSliderBackColour(45, 45, 50);
constexpr VSTGUI::CColor kSliderValueColour(120, 170, 230);

const ParamInfo& infoOf(ParamId id)
{
    return paramTable()[static_cast<std::size_t>(paramIndex(id))];
}

bool isOnPad(ParamId id)
{
    return std::any_of(kPads.begin(), kPads.end(),
                       [id](const PadInfo& pad) { return pad.x == id || pad.y == id; });
}

// "Name (units)", or just the name for unitless parameters.
std::string labelText(const ParamInfo& info)
{
    std::string text = info.name;
    if (info.units != nullptr && info.units[0] != '\0') {
        text += " (";
        text += info.units;
        text += ")";
    }
    return text;
}

VSTGUI::CTextLabel* makeLabel(const VSTGUI::CRect& size, const std::string& text,
                              const VSTGUI::CColor& colour, VSTGUI::CHoriTxtAlign align)
{
    auto* label = new VSTGUI::CTextLabel(size, text.c_str());
    label->setTransparency(true);
    label->setStyle(VSTGUI::CParamDisplay::kNoFrame);
    label->setFontColor(colour);
    label->setHoriAlign(align);
    label->setMouseEnabled(false);
    return label;
}

// Two parameters on one square. Drag to set both; Cmd + click (the same
// default-value click as CSlider) resets both to their defaults.
class XYPad : public VSTGUI::CView {
public:
    XYPad(const VSTGUI::CRect& size, EditController* controller, const PadInfo& pad);

    void draw(VSTGUI::CDrawContext* context) override;
    void onMouseDownEvent(VSTGUI::MouseDownEvent& event) override;
    void onMouseMoveEvent(VSTGUI::MouseMoveEvent& event) override;
    void onMouseUpEvent(VSTGUI::MouseUpEvent& event) override;
    void onMouseCancelEvent(VSTGUI::MouseCancelEvent& event) override;

    // Copies the controller's values unless the user is dragging.
    void syncFromController();

private:
    void moveTo(const VSTGUI::CPoint& where); // where: parent coordinates, like the mouse events
    void setValues(double x, double y);
    void endDrag();

    EditController* controller_; // borrowed; outlives the editor
    std::array<ParamID, 2> ids_;
    std::array<double, 2> defaults_;
    std::array<double, 2> values_ {};
    bool dragging_ = false;
};

XYPad::XYPad(const VSTGUI::CRect& size, EditController* controller, const PadInfo& pad)
    : CView(size)
    , controller_(controller)
    , ids_ { static_cast<ParamID>(pad.x), static_cast<ParamID>(pad.y) }
    , defaults_ { toNormalized(infoOf(pad.x), infoOf(pad.x).defaultValue),
                  toNormalized(infoOf(pad.y), infoOf(pad.y).defaultValue) }
{
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        values_[i] = controller_->getParamNormalized(ids_[i]);
    }
}

void XYPad::draw(VSTGUI::CDrawContext* context)
{
    const VSTGUI::CRect box = getViewSize();
    context->setLineWidth(1.0);
    context->setFillColor(kSliderBackColour);
    context->setFrameColor(kSliderFrameColour);
    context->drawRect(box, VSTGUI::kDrawFilledAndStroked);

    const VSTGUI::CPoint dot(box.left + values_[0] * box.getWidth(),
                             box.bottom - values_[1] * box.getHeight());
    context->drawLine(VSTGUI::CPoint(dot.x, box.top), VSTGUI::CPoint(dot.x, box.bottom));
    context->drawLine(VSTGUI::CPoint(box.left, dot.y), VSTGUI::CPoint(box.right, dot.y));

    context->setFillColor(kSliderValueColour);
    context->drawEllipse(VSTGUI::CRect(dot.x - kPadDotRadius, dot.y - kPadDotRadius,
                                       dot.x + kPadDotRadius, dot.y + kPadDotRadius),
                         VSTGUI::kDrawFilled);
    setDirty(false);
}

void XYPad::onMouseDownEvent(VSTGUI::MouseDownEvent& event)
{
    if (!event.buttonState.isLeft()) {
        return;
    }
    event.consumed = true;

    for (const ParamID id : ids_) {
        controller_->beginEdit(id);
    }
    if (event.modifiers.has(VSTGUI::ModifierKey::Control)) {
        setValues(defaults_[0], defaults_[1]);
        for (const ParamID id : ids_) {
            controller_->endEdit(id);
        }
        event.ignoreFollowUpMoveAndUpEvents(true);
        return;
    }
    dragging_ = true;
    moveTo(event.mousePosition);
}

void XYPad::onMouseMoveEvent(VSTGUI::MouseMoveEvent& event)
{
    if (dragging_) {
        moveTo(event.mousePosition);
        event.consumed = true;
    }
}

void XYPad::onMouseUpEvent(VSTGUI::MouseUpEvent& event)
{
    if (dragging_) {
        endDrag();
        event.consumed = true;
    }
}

void XYPad::onMouseCancelEvent(VSTGUI::MouseCancelEvent& event)
{
    if (dragging_) {
        endDrag();
        event.consumed = true;
    }
}

void XYPad::syncFromController()
{
    if (dragging_) {
        return;
    }
    const double x = controller_->getParamNormalized(ids_[0]);
    const double y = controller_->getParamNormalized(ids_[1]);
    if (x != values_[0] || y != values_[1]) {
        values_ = { x, y };
        invalid();
    }
}

// x = (mouse x - left) / width,  y = (bottom - mouse y) / height, clamped to 0..1.
void XYPad::moveTo(const VSTGUI::CPoint& where)
{
    const VSTGUI::CRect box = getViewSize();
    setValues(std::clamp((where.x - box.left) / box.getWidth(), 0.0, 1.0),
              std::clamp((box.bottom - where.y) / box.getHeight(), 0.0, 1.0));
}

void XYPad::setValues(double x, double y)
{
    const std::array<double, 2> next { x, y };
    for (std::size_t i = 0; i < ids_.size(); ++i) {
        if (next[i] != values_[i]) {
            values_[i] = next[i];
            controller_->setParamNormalized(ids_[i], next[i]);
            controller_->performEdit(ids_[i], next[i]);
        }
    }
    invalid();
}

void XYPad::endDrag()
{
    dragging_ = false;
    for (const ParamID id : ids_) {
        controller_->endEdit(id);
    }
}

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
    void addPads(EditController* editController);
    void addSliders(EditController* editController);
    void syncFromController();

    // Borrowed. After addView the frame owns each view (it keeps the
    // reference from `new` and releases it in CFrame::close()). All null
    // while the editor is closed.
    BodyView* body_ = nullptr;
    std::array<XYPad*, kPadCount> pads_ {};
    std::array<VSTGUI::CSlider*, kSliderCount> sliders_ {};
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
    body_ = new BodyView(VSTGUI::CRect(kMargin, kMargin, kMargin + kBodyWidth, kMargin + kBodyHeight), editController);
    frame->addView(body_);
    addPads(editController);
    addSliders(editController);

    if (!frame->open(parent, platformType)) {
        body_ = nullptr;
        pads_.fill(nullptr);
        sliders_.fill(nullptr);
        frame->forget(); // last reference: deletes the frame and its views
        frame = nullptr;
        return false;
    }
    return true;
}

// Title above each pad; "<-> x-name  ^v y-name" below it (UTF-8 arrows).
void Editor::addPads(EditController* editController)
{
    for (int i = 0; i < kPadCount; ++i) {
        const PadInfo& pad = kPads[static_cast<std::size_t>(i)];
        const int left = kControlsLeft + (i % kPadColumns) * (kPadWidth + kPadGap);
        const int right = left + kPadWidth;
        const int titleTop = kMargin + (i / kPadColumns) * (kPadBlockHeight + kPadRowGap);
        const int padTop = titleTop + kCaptionHeight;
        const int padBottom = padTop + kPadHeight;

        frame->addView(makeLabel(VSTGUI::CRect(left, titleTop, right, padTop), pad.title,
                                 kTextColour, VSTGUI::kCenterText));
        const std::string axes = std::string("\xE2\x86\x94 ") + pad.xName + "  \xE2\x86\x95 " + pad.yName;
        frame->addView(makeLabel(VSTGUI::CRect(left, padBottom, right, padBottom + kCaptionHeight), axes,
                                 kDimTextColour, VSTGUI::kCenterText));

        auto* view = new XYPad(VSTGUI::CRect(left, padTop, right, padBottom), editController, pad);
        frame->addView(view);
        pads_[static_cast<std::size_t>(i)] = view;
    }
}

void Editor::addSliders(EditController* editController)
{
    const auto& table = paramTable();
    int slot = 0;
    for (const ParamInfo& info : table) {
        if (isOnPad(info.id)) {
            continue;
        }
        const int x = kControlsLeft + (slot / kRows) * (kColumnWidth + kColumnGap);
        const int y = kSlidersTop + (slot % kRows) * kRowHeight;

        frame->addView(makeLabel(VSTGUI::CRect(x, y, x + kLabelWidth, y + kRowHeight), labelText(info),
                                 kTextColour, VSTGUI::kLeftText));

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
        sliders_[static_cast<std::size_t>(slot)] = slider;
        ++slot;
    }
}

void PLUGIN_API Editor::close()
{
    body_ = nullptr;
    pads_.fill(nullptr);
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
        syncFromController();
    }
    // The base redraws dirty views (CFrame::idle) on the same tick.
    return VSTGUIEditor::notify(sender, message);
}

void Editor::syncFromController()
{
    EditController* editController = getController();
    if (editController == nullptr) {
        return;
    }

    if (body_ != nullptr) {
        body_->update(kPollMilliseconds / 1000.0);
    }
    for (XYPad* pad : pads_) {
        if (pad != nullptr) {
            pad->syncFromController();
        }
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
