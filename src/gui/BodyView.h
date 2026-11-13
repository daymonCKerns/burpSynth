#pragma once

// BodyView: a side-on cross-section of the body, head to stomach, drawn with
// plain flesh and skin colours, showing what the model is doing.
//
// Shapes follow the parameters (read from the controller):
//   Jaw Opening        the lower jaw drops
//   Tongue Position    the tongue's hump moves back and front; a back
//                      tongue also pushes its root into the pharynx
//   Tongue Height      the hump rises toward the palate
//   Lip Opening        the gap between the lips
//   Velum              the soft palate lowers, opening the nose
//   Pharynx Width      the pharynx widens
//   Abdominal Pressure the belly wall pushes in, the diaphragm rises and the
//                      stomach is squeezed (arrows on the belly)
// and the meters (audio_engine/Meters.h):
//   Stomach Fill       the stomach's size and the gas bubble at its top
//   LES / UES Open     the two sphincters' flaps part
//   UES Flow           the UES flaps flutter while gas buzzes through
//   Wave Centre/Depth  the peristaltic squeeze moving up the esophagus
//   Level              sound rings leaving the mouth
//
// Gas: bubbles leave the stomach through the LES at a rate set by the LES
// flow, rise up the esophagus, wait at a shut UES, pass it at a speed set by
// the UES flow, and leave by the mouth (or the nose, in proportion to the
// velum opening). The speeds are the flows scaled down to be watchable; they
// show how the rates change, not the real speed of the gas.
//
// Not the final GUI: a step up from the testing interface.

#include "vstgui/lib/cpoint.h"
#include "vstgui/lib/cview.h"

#include <array>
#include <random>
#include <vector>

namespace Steinberg {
namespace Vst {
class EditController;
}
}

namespace burp {

class Canvas;

class BodyView : public VSTGUI::CView {
public:
    BodyView(const VSTGUI::CRect& size, Steinberg::Vst::EditController* controller);

    void draw(VSTGUI::CDrawContext* context) override;

    // Reads the parameters and meters, moves the gas by dt seconds and
    // redraws. The editor calls it from its timer.
    void update(double dt);

private:
    using Points = std::vector<VSTGUI::CPoint>;

    // Parameters as the drawing uses them.
    struct Shape {
        double jaw = 0.5;            // 0..1
        double tonguePosition = 0.5; // 0 = back, 1 = front
        double tongueHeight = 0.3;   // 0..1
        double lips = 0.5;           // 0..1 of the largest opening
        double velum = 0.0;          // 0..1 of the largest opening
        double pharynxWidth = 1.0;   // area scale
        double abdominal = 0.1;      // 0..1 of the largest pressure
    };

    // Meters, smoothed for display.
    struct Live {
        double fill = 0.0;
        double lesOpen = 0.0;
        double uesOpen = 0.0;
        double lesFlow = 0.0;
        double uesFlow = 0.0;
        double waveCentre = 0.0;
        double waveDepth = 0.0;
        double level = 0.0;
    };

    // A polyline through a lumen, with the distance to each point.
    struct Route {
        Points points;
        std::vector<double> distances;
        void rebuild(); // distances from points
        double length() const;
        // The point `distance` along the route, moved `offset` design units sideways.
        VSTGUI::CPoint at(double distance, double offset) const;
    };

    struct Bubble {
        bool alive = false;
        bool nose = false;     // on the nasal route
        double distance = 0.0; // along its route, design units
        double offset = 0.0;   // -1..1, sideways in the lumen
        double fade = 1.0;     // 1 = solid, 0 = gone
    };

    static constexpr int kMaxBubbles = 300;

    void readControls(double dt);
    void buildRoutes();
    void moveGas(double dt);
    void spawnBubble();

    // Geometry, in design units (400 x 600, y down, facing right).
    double jawDrop(double x) const;
    double palateY(double x) const;
    double floorY(double x) const;
    double tongueTopY(double x) const;
    double upperLipBottom() const;
    double lowerLipTop() const;
    double lowerLipBottom() const;
    VSTGUI::CPoint esophagusAt(double s) const; // s: 0 = LES, 1 = UES
    VSTGUI::CPoint stomachPoint(const VSTGUI::CPoint& relaxed) const;

    void drawBody(Canvas& canvas) const;
    void drawInnerOrgans(Canvas& canvas) const;
    void drawStomach(Canvas& canvas) const;
    void drawEsophagus(Canvas& canvas) const;
    void drawHeadTract(Canvas& canvas) const;
    void drawMouth(Canvas& canvas) const;
    void drawSphincter(Canvas& canvas, const VSTGUI::CPoint& centre, double open, double jitter) const;
    void drawGas(Canvas& canvas) const;
    void drawLabels(Canvas& canvas) const;

    Steinberg::Vst::EditController* controller_; // borrowed; outlives the view
    Shape shape_;
    Live live_;
    Route mouthRoute_; // stomach to the lips
    Route noseRoute_;  // from the branch below the velum to the nostrils
    double lesDistance_ = 0.0;
    double uesDistance_ = 0.0;
    double branchDistance_ = 0.0;
    double lipsDistance_ = 0.0;
    double nostrilDistance_ = 0.0;
    std::array<Bubble, kMaxBubbles> bubbles_ {};
    double spawnDebt_ = 0.0;
    double uesJitter_ = 0.0;
    std::minstd_rand random_;
    std::uniform_real_distribution<double> uniform_ { -1.0, 1.0 };
};

} // namespace burp
