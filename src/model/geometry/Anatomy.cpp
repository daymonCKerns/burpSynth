// Anatomy: scaled lengths and the fixed esophagus and nasal shapes from Anatomy.h.

#include "model/geometry/Anatomy.h"

#include "model/math/Units.h"

#include <array>

namespace burp {

namespace {

constexpr int kEsophagusSections = 32;
constexpr int kNasalSections = 12;

// Nasal cavity from the velum end towards the nostrils (cm^2), one value per
// section except the last, which is kNostrilArea. Approximate, not measured:
// narrow at the velum, widest through the turbinates, narrowing again.
constexpr std::array<double, kNasalSections - 1> kNasalProfileCm2 {
    1.0, 1.6, 2.4, 3.2, 4.0, 4.4, 4.4, 4.0, 3.2, 2.2, 1.4,
};

} // namespace

void Anatomy::set(const BodySettings& settings)
{
    settings_ = settings;
}

double Anatomy::esophagusLength() const
{
    return settings_.esophagusLength * settings_.bodySize;
}

double Anatomy::pharynxLength() const
{
    return kPharynxLength * settings_.bodySize;
}

double Anatomy::mouthLength() const
{
    return kMouthLength * settings_.bodySize;
}

double Anatomy::nasalLength() const
{
    return kNasalLength * settings_.bodySize;
}

AreaFunction Anatomy::esophagusShape() const
{
    AreaFunction shape;
    shape.fill(kEsophagusSections, kEsophagusOpenArea);
    return shape;
}

AreaFunction Anatomy::nasalShape() const
{
    AreaFunction shape;
    shape.count = kNasalSections;
    for (int i = 0; i < kNasalSections - 1; ++i) {
        shape.area[i] = cm2ToM2(kNasalProfileCm2[i]);
    }
    shape.area[kNasalSections - 1] = kNostrilArea;
    return shape;
}

} // namespace burp
