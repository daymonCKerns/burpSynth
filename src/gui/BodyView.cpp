// BodyView: the body cross-section from BodyView.h.
//
// Everything is laid out in a 400 x 600 design space (x toward the face,
// y down) and mapped into the view keeping the aspect ratio. Organic outlines
// are smooth curves (Catmull-Rom splines) through a few anatomical points.
//
// Layers, back to front: body; brain, spine, heart, diaphragm; stomach;
// esophagus and LES; nose and pharynx; mouth (cavity, jaw, tongue, teeth,
// lips); velum; UES; gas; sound; labels.

#include "gui/BodyView.h"

#include "audio_engine/Meters.h"
#include "audio_engine/Params.h"
#include "model/math/Smooth.h"

#include "public.sdk/source/vst/vsteditcontroller.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/clinestyle.h"
#include "vstgui/lib/crect.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <numbers>

namespace burp {

using VSTGUI::CColor;
using VSTGUI::CPoint;

namespace {

constexpr double kDesignWidth = 400.0;
constexpr double kDesignHeight = 600.0;

constexpr CColor kPanel(36, 32, 36);
constexpr CColor kSkin(228, 170, 136);
constexpr CColor kSkinEdge(150, 92, 72);
constexpr CColor kFlesh(214, 136, 120);
constexpr CColor kMuscle(178, 80, 88);
constexpr CColor kMucosa(190, 88, 100);
constexpr CColor kLumen(62, 24, 34);
constexpr CColor kTongue(204, 94, 106);
constexpr CColor kTongueEdge(168, 70, 82);
constexpr CColor kLip(196, 100, 104);
constexpr CColor kBone(236, 226, 206);
constexpr CColor kBoneEdge(200, 180, 160);
constexpr CColor kBrain(234, 198, 190);
constexpr CColor kBrainFold(214, 170, 164);
constexpr CColor kHeart(146, 48, 60);
constexpr CColor kHeartEdge(112, 34, 46);
constexpr CColor kStomachWall(198, 106, 112);
constexpr CColor kStomachInside(120, 48, 60);
constexpr CColor kGas(246, 240, 214);
constexpr CColor kLabel(200, 190, 186);
constexpr CColor kLeader(200, 190, 186, 80);
constexpr CColor kNone(0, 0, 0, 0);

// Gas, in design units. Scaled down to be watchable, not real speeds.
constexpr double kGasSpeed = 900.0;    // per second at kFullFlow: a burst's gas reaches the mouth in about 1 s
constexpr double kSpawnRate = 190.0;   // bubbles per second at kFullFlow through the LES
constexpr double kDrift = 6.0;         // per second, so stray gas always clears
constexpr double kOutsideSpeed = 45.0; // per second, after the lips or nostrils
constexpr double kOutsideFade = 0.7;   // s
constexpr double kBubbleRadius = 2.4;
constexpr double kShutFlow = 0.004;    // UES flow (0..1) below which bubbles wait under it
constexpr double kSmoothingTime = 0.1; // s, display smoothing of the flows and level

constexpr double kLabelSize = 9.0;     // design units

CColor withAlpha(CColor colour, double alpha)
{
    colour.alpha = static_cast<uint8_t>(std::clamp(alpha, 0.0, 1.0) * 255.0);
    return colour;
}

CPoint mix(const CPoint& a, const CPoint& b, double t)
{
    return CPoint(lerp(a.x, b.x, t), lerp(a.y, b.y, t));
}

// Spine centre line: straight lines through these (y, x) knots.
double spineX(double y)
{
    constexpr std::array<std::array<double, 2>, 6> knots {{
        { 140.0, 186.0 }, { 220.0, 182.0 }, { 300.0, 160.0 }, { 400.0, 140.0 }, { 480.0, 132.0 }, { 590.0, 140.0 },
    }};
    for (std::size_t i = 1; i < knots.size(); ++i) {
        if (y <= knots[i][0]) {
            const double t = (y - knots[i - 1][0]) / (knots[i][0] - knots[i - 1][0]);
            return lerp(knots[i - 1][1], knots[i][1], std::clamp(t, 0.0, 1.0));
        }
    }
    return knots.back()[1];
}

} // namespace

// Maps design units into the view and draws smooth shapes.
class Canvas {
public:
    using Points = std::vector<CPoint>;

    Canvas(VSTGUI::CDrawContext* context, const VSTGUI::CRect& box)
        : context_(context)
        , scale_(std::min(box.getWidth() / kDesignWidth, box.getHeight() / kDesignHeight))
        , origin_(box.left + 0.5 * (box.getWidth() - kDesignWidth * scale_),
                  box.top + 0.5 * (box.getHeight() - kDesignHeight * scale_))
    {
    }

    CPoint map(const CPoint& p) const
    {
        return CPoint(origin_.x + p.x * scale_, origin_.y + p.y * scale_);
    }

    // Closed smooth shape through `points`, filled, optionally outlined.
    void shape(const Points& points, const CColor& fill, const CColor& edge = kNone, double edgeWidth = 0.0)
    {
        const auto path = smoothPath(points, true);
        if (!path) {
            return;
        }
        context_->setFillColor(fill);
        context_->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        if (edgeWidth > 0.0) {
            setPen(edge, edgeWidth);
            context_->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
        }
    }

    // Open smooth curve through `points`, round ends.
    void curve(const Points& points, double width, const CColor& colour)
    {
        const auto path = smoothPath(points, false);
        if (!path) {
            return;
        }
        setPen(colour, width);
        context_->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    void line(const CPoint& from, const CPoint& to, double width, const CColor& colour)
    {
        setPen(colour, width);
        context_->drawLine(map(from), map(to));
    }

    void ellipse(const CPoint& centre, double rx, double ry, const CColor& fill, const CColor& edge = kNone,
                 double edgeWidth = 0.0)
    {
        const CPoint c = map(centre);
        const VSTGUI::CRect box(c.x - rx * scale_, c.y - ry * scale_, c.x + rx * scale_, c.y + ry * scale_);
        context_->setFillColor(fill);
        context_->drawEllipse(box, VSTGUI::kDrawFilled);
        if (edgeWidth > 0.0) {
            setPen(edge, edgeWidth);
            context_->drawEllipse(box, VSTGUI::kDrawStroked);
        }
    }

    // `at` is the left end of the text's baseline.
    void text(const char* string, const CPoint& at, double size, const CColor& colour)
    {
        context_->setFont(VSTGUI::kNormalFontSmaller, size * scale_);
        context_->setFontColor(colour);
        context_->drawString(string, map(at));
    }

private:
    // Catmull-Rom through the points as cubic Beziers:
    //   c1 = p1 + (p2 - p0) / 6,  c2 = p2 - (p3 - p1) / 6
    VSTGUI::SharedPointer<VSTGUI::CGraphicsPath> smoothPath(const Points& points, bool closed) const
    {
        const int n = static_cast<int>(points.size());
        if (n < 2) {
            return {};
        }
        auto path = VSTGUI::owned(context_->createGraphicsPath());
        if (!path) {
            return {};
        }
        const auto at = [&](int i) {
            const int index = closed ? (i % n + n) % n : std::clamp(i, 0, n - 1);
            return map(points[static_cast<std::size_t>(index)]);
        };
        path->beginSubpath(at(0));
        const int segments = closed ? n : n - 1;
        for (int i = 0; i < segments; ++i) {
            const CPoint p0 = at(i - 1);
            const CPoint p1 = at(i);
            const CPoint p2 = at(i + 1);
            const CPoint p3 = at(i + 2);
            path->addBezierCurve(CPoint(p1.x + (p2.x - p0.x) / 6.0, p1.y + (p2.y - p0.y) / 6.0),
                                 CPoint(p2.x - (p3.x - p1.x) / 6.0, p2.y - (p3.y - p1.y) / 6.0), p2);
        }
        if (closed) {
            path->closeSubpath();
        }
        return path;
    }

    void setPen(const CColor& colour, double width)
    {
        context_->setFrameColor(colour);
        context_->setLineWidth(width * scale_);
        context_->setLineStyle(
            VSTGUI::CLineStyle(VSTGUI::CLineStyle::kLineCapRound, VSTGUI::CLineStyle::kLineJoinRound));
    }

    VSTGUI::CDrawContext* context_;
    double scale_;
    CPoint origin_;
};

// --- Route -------------------------------------------------------------------

void BodyView::Route::rebuild()
{
    distances.assign(points.size(), 0.0);
    for (std::size_t i = 1; i < points.size(); ++i) {
        const double dx = points[i].x - points[i - 1].x;
        const double dy = points[i].y - points[i - 1].y;
        distances[i] = distances[i - 1] + std::sqrt(dx * dx + dy * dy);
    }
}

double BodyView::Route::length() const
{
    return distances.empty() ? 0.0 : distances.back();
}

CPoint BodyView::Route::at(double distance, double offset) const
{
    if (points.size() < 2) {
        return points.empty() ? CPoint() : points.front();
    }
    std::size_t i = 1;
    while (i + 1 < points.size() && distances[i] < distance) {
        ++i;
    }
    const CPoint& a = points[i - 1];
    const CPoint& b = points[i];
    const double span = std::max(distances[i] - distances[i - 1], 1.0e-9);
    const double t = std::clamp((distance - distances[i - 1]) / span, 0.0, 1.0);
    // Sideways = the segment direction turned 90 degrees.
    const double nx = -(b.y - a.y) / span;
    const double ny = (b.x - a.x) / span;
    return CPoint(lerp(a.x, b.x, t) + offset * nx, lerp(a.y, b.y, t) + offset * ny);
}

// --- BodyView ----------------------------------------------------------------

BodyView::BodyView(const VSTGUI::CRect& size, Steinberg::Vst::EditController* controller)
    : CView(size)
    , controller_(controller)
{
    buildRoutes();
}

void BodyView::update(double dt)
{
    readControls(dt);
    buildRoutes();
    moveGas(dt);
    uesJitter_ = 1.2 * live_.uesFlow * uniform_(random_);
    invalid();
}

void BodyView::readControls(double dt)
{
    const auto read = [this](int id) {
        return std::clamp(controller_->getParamNormalized(static_cast<Steinberg::Vst::ParamID>(id)), 0.0, 1.0);
    };
    const auto param = [&read](ParamId id) { return read(static_cast<int>(id)); };
    const auto meter = [&read](MeterId id) { return read(static_cast<int>(id)); };

    shape_.jaw = param(ParamId::JawOpening);
    shape_.tonguePosition = param(ParamId::TonguePosition);
    shape_.tongueHeight = param(ParamId::TongueHeight);
    shape_.lips = param(ParamId::LipOpening);
    shape_.velum = param(ParamId::Velum);
    const ParamInfo& widthInfo = paramTable()[static_cast<std::size_t>(paramIndex(ParamId::PharynxWidth))];
    shape_.pharynxWidth = toPlain(widthInfo, param(ParamId::PharynxWidth));
    shape_.abdominal = param(ParamId::AbdominalPressure);

    // Openings and the wave as they come; flows smoothed, level held and decayed.
    const double a = std::exp(-dt / kSmoothingTime);
    live_.fill = meter(MeterId::StomachFill);
    live_.lesOpen = meter(MeterId::LesOpen);
    live_.uesOpen = meter(MeterId::UesOpen);
    live_.waveCentre = meter(MeterId::WaveCentre);
    live_.waveDepth = meter(MeterId::WaveDepth);
    live_.lesFlow = meter(MeterId::LesFlow) + a * (live_.lesFlow - meter(MeterId::LesFlow));
    live_.uesFlow = meter(MeterId::UesFlow) + a * (live_.uesFlow - meter(MeterId::UesFlow));
    live_.level = std::max(meter(MeterId::Level), a * live_.level);
}

// --- Geometry ----------------------------------------------------------------

// drop(x) = lerp(2, 24, jaw) * clamp((x - 222) / 70, 0, 1): nothing at the
// tongue root, the most at the chin.
double BodyView::jawDrop(double x) const
{
    return lerp(2.0, 24.0, shape_.jaw) * std::clamp((x - 222.0) / 70.0, 0.0, 1.0);
}

// Hard palate: an arch from the velum (x 226) to the upper teeth (x 288).
double BodyView::palateY(double x) const
{
    const double t = std::clamp((x - 226.0) / 62.0, 0.0, 1.0);
    return 136.0 - 5.0 * std::sin(std::numbers::pi * t);
}

// Floor of the mouth, carried by the jaw.
double BodyView::floorY(double x) const
{
    return lerp(178.0, 160.0, std::clamp((x - 222.0) / 68.0, 0.0, 1.0)) + jawDrop(x);
}

// Tongue top: a body plus a hump lerp(6, 34, height) high at
// lerp(234, 268, position), tapering to the tip, never above the palate.
double BodyView::tongueTopY(double x) const
{
    const double centre = lerp(234.0, 268.0, shape_.tonguePosition);
    const double hump = lerp(6.0, 34.0, shape_.tongueHeight);
    const double u = (x - centre) / 21.0;
    const double thickness = (9.0 + hump * std::exp(-u * u)) * smoothstep((286.0 - x) / 12.0);
    const double roof = x >= 226.0 ? palateY(x) + 1.5 : 112.0;
    return std::max(floorY(x) - thickness, roof);
}

// The lips part by lips * (11 + the jaw's drop at the lips); the upper lip
// takes a quarter of the closed-jaw part. Shut lips on an open jaw stretch.
double BodyView::upperLipBottom() const
{
    return 148.0 - 0.25 * 11.0 * shape_.lips;
}

double BodyView::lowerLipTop() const
{
    return upperLipBottom() + shape_.lips * (11.0 + jawDrop(290.0));
}

double BodyView::lowerLipBottom() const
{
    return std::max(lowerLipTop() + 10.0, floorY(290.0) + 9.0);
}

// x = 205 + 5 s - 5 sin(pi s),  y = 462 - 230 s: a gentle bow toward the spine.
CPoint BodyView::esophagusAt(double s) const
{
    return CPoint(205.0 + 5.0 * s - 5.0 * std::sin(std::numbers::pi * s), 462.0 - 230.0 * s);
}

// The stomach grows with its gas from the cardia (where the LES enters, so
// the joint never moves) and the abdomen squeezes it flatter:
//   p = C + (p_relaxed - C) (s, s (1 - 0.15 abdominal)),  s = 0.8 + 0.32 fill
CPoint BodyView::stomachPoint(const CPoint& relaxed) const
{
    const CPoint cardia(208.0, 470.0);
    const double scale = 0.8 + 0.32 * live_.fill;
    return CPoint(cardia.x + (relaxed.x - cardia.x) * scale,
                  cardia.y + (relaxed.y - cardia.y) * scale * (1.0 - 0.15 * shape_.abdominal));
}

// --- Gas ---------------------------------------------------------------------

void BodyView::buildRoutes()
{
    Points& mouth = mouthRoute_.points;
    mouth.clear();
    mouth.push_back(stomachPoint(CPoint(258.0, 474.0))); // the gas bubble in the stomach
    mouth.push_back(CPoint(214.0, 468.0));
    const std::size_t les = mouth.size();
    for (int i = 0; i <= 10; ++i) {
        mouth.push_back(esophagusAt(i / 10.0));
    }
    const std::size_t ues = mouth.size() - 1;
    mouth.push_back(CPoint(211.0, 200.0));
    mouth.push_back(CPoint(213.0, 168.0));
    const std::size_t branch = mouth.size();
    mouth.push_back(CPoint(214.0, 150.0));
    for (const double x : { 234.0, 252.0, 270.0 }) {
        mouth.push_back(CPoint(x, 0.5 * (palateY(x) + tongueTopY(x))));
    }
    const double mouthY = 0.5 * (upperLipBottom() + lowerLipTop());
    const std::size_t lips = mouth.size();
    mouth.push_back(CPoint(294.0, mouthY));
    mouth.push_back(CPoint(350.0, mouthY + 6.0));
    mouthRoute_.rebuild();

    lesDistance_ = mouthRoute_.distances[les];
    uesDistance_ = mouthRoute_.distances[ues];
    branchDistance_ = mouthRoute_.distances[branch];
    lipsDistance_ = mouthRoute_.distances[lips];

    noseRoute_.points = { CPoint(214.0, 150.0), CPoint(212.0, 128.0), CPoint(217.0, 107.0), CPoint(240.0, 101.0),
                          CPoint(266.0, 102.0), CPoint(298.0, 118.0), CPoint(338.0, 134.0) };
    noseRoute_.rebuild();
    nostrilDistance_ = noseRoute_.distances[5];
}

void BodyView::spawnBubble()
{
    for (Bubble& bubble : bubbles_) {
        if (!bubble.alive) {
            bubble = Bubble { true, false, lesDistance_ - 14.0 + 4.0 * uniform_(random_), uniform_(random_), 1.0 };
            return;
        }
    }
}

// Speeds are the flows (0..1) times kGasSpeed, plus kDrift:
//   stomach to LES     0.6 x LES flow
//   esophagus          0.8 x the larger flow; waits under the UES while it is shut
//   pharynx to lips    UES flow (the nose likewise)
//   outside            kOutsideSpeed, fading over kOutsideFade
// At the branch below the velum a bubble takes the nose with probability
// velum / (velum + lips + 0.15).
void BodyView::moveGas(double dt)
{
    spawnDebt_ += live_.lesFlow * kSpawnRate * dt;
    while (spawnDebt_ >= 1.0) {
        spawnBubble();
        spawnDebt_ -= 1.0;
    }

    const double lesSpeed = live_.lesFlow * kGasSpeed;
    const double uesSpeed = live_.uesFlow * kGasSpeed;
    const bool uesShut = live_.uesFlow < kShutFlow;
    const bool silent = live_.lesFlow < 1.0e-3 && live_.uesFlow < 1.0e-3 && live_.fill < 1.0e-3;
    const double noseShare = shape_.velum / (shape_.velum + shape_.lips + 0.15);

    for (Bubble& bubble : bubbles_) {
        if (!bubble.alive) {
            continue;
        }
        if (bubble.nose) {
            if (bubble.distance < nostrilDistance_) {
                bubble.distance += (uesSpeed + kDrift) * dt;
            } else {
                bubble.distance += kOutsideSpeed * dt;
                bubble.fade -= dt / kOutsideFade;
            }
            if (bubble.distance >= noseRoute_.length()) {
                bubble.fade = 0.0;
            }
        } else {
            const double before = bubble.distance;
            if (bubble.distance < lesDistance_) {
                bubble.distance += (0.6 * lesSpeed + kDrift) * dt;
            } else if (bubble.distance < uesDistance_) {
                bubble.distance += (0.8 * std::max(lesSpeed, uesSpeed) + kDrift) * dt;
                if (uesShut) {
                    const double queue = uesDistance_ - 2.0 - 2.5 * std::abs(bubble.offset);
                    bubble.distance = std::max(before, std::min(bubble.distance, queue));
                }
            } else if (bubble.distance < lipsDistance_) {
                bubble.distance += (uesSpeed + kDrift) * dt;
            } else {
                bubble.distance += kOutsideSpeed * dt;
                bubble.fade -= dt / kOutsideFade;
            }

            if (before < branchDistance_ && bubble.distance >= branchDistance_
                && 0.5 * (uniform_(random_) + 1.0) < noseShare) {
                bubble.nose = true;
                bubble.distance -= branchDistance_;
            }
            if (bubble.distance >= mouthRoute_.length()) {
                bubble.fade = 0.0;
            }
        }
        if (silent) {
            bubble.fade -= dt / kOutsideFade;
        }
        if (bubble.fade <= 0.0) {
            bubble.alive = false;
        }
    }
}

// --- Drawing -----------------------------------------------------------------

void BodyView::draw(VSTGUI::CDrawContext* context)
{
    const VSTGUI::CRect box = getViewSize();
    context->setDrawMode(VSTGUI::kAntiAliasing);
    context->setFillColor(kPanel);
    context->drawRect(box, VSTGUI::kDrawFilled);

    Canvas canvas(context, box);
    drawBody(canvas);
    drawInnerOrgans(canvas);
    drawStomach(canvas);
    drawEsophagus(canvas);
    drawHeadTract(canvas);
    drawMouth(canvas);

    // Velum: from the back of the hard palate, raised against the back of
    // the pharynx (nose shut) or hanging down (nose open).
    const double v = shape_.velum;
    canvas.curve({ CPoint(229.0, 132.0), mix(CPoint(219.0, 124.0), CPoint(226.0, 143.0), v),
                   mix(CPoint(209.0, 112.0), CPoint(222.0, 155.0), v) },
                 7.0, kMuscle);

    drawSphincter(canvas, esophagusAt(1.0), live_.uesOpen, uesJitter_);
    drawGas(canvas);
    drawLabels(canvas);
    setDirty(false);
}

void BodyView::drawBody(Canvas& canvas) const
{
    // Head, neck and torso in one outline, facing right; the belly wall moves
    // in with the abdominal pressure.
    const double belly = 340.0 - 14.0 * shape_.abdominal;
    const Points outline {
        CPoint(178.0, 16.0),  CPoint(238.0, 22.0),  CPoint(274.0, 50.0),  CPoint(288.0, 80.0),
        CPoint(291.0, 98.0),  CPoint(302.0, 114.0), CPoint(293.0, 124.0), CPoint(295.0, 136.0),
        CPoint(290.0, 147.0), CPoint(264.0, 156.0), CPoint(244.0, 180.0), CPoint(246.0, 214.0),
        CPoint(262.0, 246.0), CPoint(296.0, 272.0), CPoint(320.0, 320.0), CPoint(326.0, 410.0),
        CPoint(belly, 470.0), CPoint(belly + 2.0, 530.0), CPoint(334.0, 592.0), CPoint(200.0, 598.0),
        CPoint(96.0, 592.0),  CPoint(90.0, 470.0),  CPoint(94.0, 340.0),  CPoint(120.0, 262.0),
        CPoint(150.0, 216.0), CPoint(128.0, 172.0), CPoint(104.0, 112.0), CPoint(114.0, 52.0),
    };
    canvas.shape(outline, kFlesh, kSkin, 6.0);
    canvas.shape(outline, kNone, kSkinEdge, 1.0);

    // Arrows pressing on the belly, as long as the pressure is high.
    if (shape_.abdominal > 0.02) {
        const double length = 4.0 + 18.0 * shape_.abdominal;
        const CColor arrow = withAlpha(kLabel, 0.35 + 0.5 * shape_.abdominal);
        for (const double y : { 480.0, 515.0, 550.0 }) {
            const CPoint tip(belly + 6.0, y);
            canvas.line(CPoint(tip.x + length, y), tip, 1.6, arrow);
            canvas.line(tip, CPoint(tip.x + 4.0, y - 3.0), 1.6, arrow);
            canvas.line(tip, CPoint(tip.x + 4.0, y + 3.0), 1.6, arrow);
        }
    }
}

void BodyView::drawInnerOrgans(Canvas& canvas) const
{
    // Skull and brain, with a few folds.
    canvas.ellipse(CPoint(192.0, 62.0), 72.0, 38.0, kBone, kBoneEdge, 1.0);
    canvas.ellipse(CPoint(192.0, 62.0), 66.0, 32.0, kBrain);
    canvas.curve({ CPoint(140.0, 62.0), CPoint(158.0, 50.0), CPoint(178.0, 60.0) }, 1.6, kBrainFold);
    canvas.curve({ CPoint(184.0, 42.0), CPoint(202.0, 52.0), CPoint(226.0, 46.0) }, 1.6, kBrainFold);
    canvas.curve({ CPoint(156.0, 80.0), CPoint(188.0, 72.0), CPoint(214.0, 82.0), CPoint(238.0, 72.0) }, 1.6,
                 kBrainFold);

    // Spine: vertebrae down the back.
    for (double y = 150.0; y <= 588.0; y += 16.0) {
        canvas.ellipse(CPoint(spineX(y), y), 10.0, 6.0, kBone, kBoneEdge, 0.8);
    }

    // Heart, in front of the esophagus.
    canvas.shape({ CPoint(236.0, 345.0), CPoint(270.0, 336.0), CPoint(300.0, 356.0), CPoint(304.0, 392.0),
                   CPoint(282.0, 414.0), CPoint(250.0, 408.0), CPoint(232.0, 384.0) },
                 kHeart, kHeartEdge, 1.2);

    // Diaphragm, pushed up by the abdominal pressure.
    const double rise = 12.0 * shape_.abdominal;
    canvas.curve({ CPoint(94.0, 482.0), CPoint(150.0, 446.0 - 0.6 * rise), CPoint(212.0, 434.0 - rise),
                   CPoint(272.0, 444.0 - 0.6 * rise), CPoint(326.0, 484.0) },
                 6.0, kMuscle);
}

void BodyView::drawStomach(Canvas& canvas) const
{
    const Points relaxed {
        CPoint(208.0, 470.0), CPoint(228.0, 458.0), CPoint(256.0, 452.0), CPoint(286.0, 464.0),
        CPoint(304.0, 494.0), CPoint(302.0, 532.0), CPoint(284.0, 562.0), CPoint(254.0, 574.0),
        CPoint(224.0, 568.0), CPoint(206.0, 550.0), CPoint(212.0, 532.0), CPoint(226.0, 514.0),
        CPoint(226.0, 494.0), CPoint(214.0, 484.0),
    };
    const CPoint centre(256.0, 514.0);
    Points wall;
    Points inside;
    for (const CPoint& p : relaxed) {
        wall.push_back(stomachPoint(p));
        inside.push_back(stomachPoint(mix(centre, p, 0.82)));
    }
    canvas.shape(wall, kStomachWall, kMuscle, 1.2);
    canvas.shape(inside, kStomachInside);

    // The swallowed gas, pooled at the top.
    const double rx = 5.0 + 20.0 * std::sqrt(live_.fill);
    canvas.ellipse(stomachPoint(CPoint(258.0, 474.0)), rx, 0.62 * rx, withAlpha(kGas, 0.8),
                   withAlpha(kSkinEdge, 0.6), 0.8);
}

void BodyView::drawEsophagus(Canvas& canvas) const
{
    // Lining, then the open channel, narrowed where the peristaltic wave
    // squeezes: g = depth exp(-((s - centre) / 0.07)^2).
    constexpr int kSegments = 48;
    for (int pass = 0; pass < 2; ++pass) {
        const CColor colour = pass == 0 ? kMucosa : kLumen;
        const double base = pass == 0 ? 12.0 : 7.5;
        const double squeeze = pass == 0 ? 0.35 : 0.85;
        canvas.line(CPoint(208.0, 472.0), esophagusAt(0.0), base, colour); // into the stomach
        for (int i = 0; i < kSegments; ++i) {
            const double s0 = static_cast<double>(i) / kSegments;
            const double s1 = static_cast<double>(i + 1) / kSegments;
            const double u = (0.5 * (s0 + s1) - live_.waveCentre) / 0.07;
            const double g = live_.waveDepth * std::exp(-u * u);
            canvas.line(esophagusAt(s0), esophagusAt(s1), base * (1.0 - squeeze * g), colour);
        }
    }
    drawSphincter(canvas, CPoint(205.5, 464.0), live_.lesOpen, 0.0);
}

void BodyView::drawHeadTract(Canvas& canvas) const
{
    // Nasal cavity.
    const Points nose { CPoint(215.0, 106.0), CPoint(234.0, 101.0), CPoint(262.0, 102.0), CPoint(284.0, 108.0),
                        CPoint(298.0, 118.0) };
    canvas.curve(nose, 13.0, kMucosa);
    canvas.curve(nose, 8.5, kLumen);

    // Pharynx, as wide as Pharynx Width.
    const Points pharynx { CPoint(215.0, 100.0), CPoint(214.0, 128.0), CPoint(213.0, 160.0), CPoint(211.0, 200.0),
                           CPoint(210.0, 232.0) };
    const double width = 14.0 * shape_.pharynxWidth;
    canvas.curve(pharynx, width + 4.0, kMucosa);
    canvas.curve(pharynx, width, kLumen);

    // Hard palate bone, between the nose and the mouth.
    canvas.curve({ CPoint(228.0, 127.0), CPoint(256.0, 126.0), CPoint(284.0, 131.0) }, 3.5, kBone);
}

void BodyView::drawMouth(Canvas& canvas) const
{
    // Oral cavity: palate above, the jaw's floor below.
    Points cavity;
    for (double x = 224.0; x <= 288.0; x += 4.0) {
        cavity.push_back(CPoint(x, palateY(x) - 0.5));
    }
    cavity.push_back(CPoint(291.0, palateY(288.0)));
    cavity.push_back(CPoint(291.0, floorY(290.0)));
    for (double x = 288.0; x >= 222.0; x -= 4.0) {
        cavity.push_back(CPoint(x, floorY(x)));
    }
    cavity.push_back(CPoint(221.0, palateY(226.0)));
    canvas.shape(cavity, kLumen);

    // Lower jaw and chin, dropped by the jaw opening.
    const auto dropped = [this](double x, double y) { return CPoint(x, y + jawDrop(x)); };
    canvas.shape({ dropped(222.0, 178.0), dropped(240.0, 175.0), dropped(260.0, 170.0), dropped(280.0, 163.0),
                   dropped(291.0, 160.0), dropped(297.0, 170.0), dropped(296.0, 186.0), dropped(288.0, 198.0),
                   dropped(268.0, 206.0), dropped(246.0, 210.0), dropped(228.0, 214.0) },
                 kFlesh);
    const Points chin { dropped(297.0, 172.0), dropped(296.0, 186.0), dropped(288.0, 198.0), dropped(268.0, 206.0),
                        dropped(244.0, 212.0) };
    canvas.curve(chin, 6.0, kSkin);
    canvas.curve(chin, 1.0, kSkinEdge);

    // Tongue: its top surface, the tip, the floor back to the root, which a
    // back tongue pushes into the pharynx.
    const double push = (1.0 - shape_.tonguePosition) * (0.4 + shape_.tongueHeight) * 5.0;
    Points tongue;
    for (double x = 222.0; x <= 286.0; x += 4.0) {
        tongue.push_back(CPoint(x, tongueTopY(x)));
    }
    tongue.push_back(CPoint(288.0, floorY(288.0) - 3.0));
    for (double x = 284.0; x >= 222.0; x -= 8.0) {
        tongue.push_back(CPoint(x, floorY(x) + 1.0));
    }
    tongue.push_back(CPoint(222.0 - push, floorY(222.0) - 3.0));
    tongue.push_back(CPoint(222.0 - 1.1 * push, 0.5 * (floorY(222.0) + tongueTopY(222.0))));
    tongue.push_back(CPoint(222.0 - push, tongueTopY(222.0) + 3.0));
    canvas.shape(tongue, kTongue, kTongueEdge, 1.0);

    // Front teeth, the lower one on the jaw.
    canvas.ellipse(CPoint(283.0, 143.5), 2.6, 5.0, kBone, kBoneEdge, 0.6);
    canvas.ellipse(CPoint(282.0, floorY(282.0) - 4.0), 2.4, 4.5, kBone, kBoneEdge, 0.6);

    // The opening between the lips, then the lips.
    const double top = upperLipBottom();
    const double gap = lowerLipTop() - top;
    if (gap > 0.3) {
        canvas.ellipse(CPoint(292.0, top + 0.5 * gap), 6.0, 0.5 * gap + 0.5, kLumen);
    }
    canvas.ellipse(CPoint(293.0, top - 4.0), 6.5, 4.5, kLip, kSkinEdge, 0.6);
    const double lowerTop = lowerLipTop();
    const double lowerBottom = lowerLipBottom();
    canvas.ellipse(CPoint(292.0, 0.5 * (lowerTop + lowerBottom)), 6.5, 0.5 * (lowerBottom - lowerTop), kLip,
                   kSkinEdge, 0.6);
}

// Two muscle pads either side of the channel, parted by `open` (0..1).
void BodyView::drawSphincter(Canvas& canvas, const CPoint& centre, double open, double jitter) const
{
    const double half = 0.6 + 4.5 * open + jitter;
    for (const double side : { -1.0, 1.0 }) {
        canvas.ellipse(CPoint(centre.x + side * (half + 4.2), centre.y), 4.2, 6.5, kMuscle, kMucosa, 1.0);
    }
}

void BodyView::drawGas(Canvas& canvas) const
{
    for (const Bubble& bubble : bubbles_) {
        if (!bubble.alive) {
            continue;
        }
        const Route& route = bubble.nose ? noseRoute_ : mouthRoute_;
        const CPoint at = route.at(bubble.distance, 2.2 * bubble.offset);
        canvas.ellipse(at, kBubbleRadius, kBubbleRadius, withAlpha(kGas, 0.92 * bubble.fade),
                       withAlpha(kSkinEdge, 0.5 * bubble.fade), 0.6);
    }

    // Sound leaving the mouth: three rings, brighter the louder.
    const double mouthY = 0.5 * (upperLipBottom() + lowerLipTop());
    for (int ring = 0; ring < 3; ++ring) {
        const double radius = 9.0 + 7.0 * ring;
        const double alpha = 1.6 * live_.level * (1.0 - 0.28 * ring);
        if (alpha < 0.02) {
            continue;
        }
        Points arc;
        for (int i = -3; i <= 3; ++i) {
            const double angle = i * 0.2;
            arc.push_back(CPoint(299.0 + radius * std::cos(angle), mouthY + radius * std::sin(angle)));
        }
        canvas.curve(arc, 1.6, withAlpha(kGas, alpha));
    }
}

void BodyView::drawLabels(Canvas& canvas) const
{
    struct Label {
        const char* text;
        double y;
        CPoint target;
    };
    const std::array<Label, 6> labels {{
        { "Velum", 128.0, CPoint(220.0, 136.0) },
        { "Tongue", 168.0, CPoint(246.0, 165.0) },
        { "UES", 234.0, CPoint(200.0, 232.0) },
        { "Esophagus", 330.0, CPoint(198.0, 330.0) },
        { "LES", 466.0, CPoint(198.0, 464.0) },
        { "Stomach", 536.0, CPoint(236.0, 530.0) },
    }};
    for (const Label& label : labels) {
        canvas.text(label.text, CPoint(6.0, label.y), kLabelSize, kLabel);
        canvas.line(CPoint(52.0, label.y - 3.0), label.target, 0.7, kLeader);
    }

    // Readout of what the gas is doing, top right.
    char line[64];
    std::snprintf(line, sizeof line, "Stomach %.0f mL", live_.fill * kFullStomach * 1.0e6);
    canvas.text(line, CPoint(312.0, 26.0), kLabelSize, kLabel);
    std::snprintf(line, sizeof line, "LES %.0f mL/s", live_.lesFlow * kFullFlow * 1.0e6);
    canvas.text(line, CPoint(312.0, 40.0), kLabelSize, kLabel);
    std::snprintf(line, sizeof line, "UES %.0f mL/s", live_.uesFlow * kFullFlow * 1.0e6);
    canvas.text(line, CPoint(312.0, 54.0), kLabelSize, kLabel);
}

} // namespace burp
