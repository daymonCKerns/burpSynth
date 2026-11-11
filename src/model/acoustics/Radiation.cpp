// Radiation: the backward-Euler step of the lip/nostril load from Radiation.h.
//
// With a = dt / L, b = dt / L_r and R = R_r, the two updates
//   U' = U + a (P - R (U' - i'))
//   i' = i + b R (U' - i')
// are linear in U' and i' and are solved in closed form.

#include "model/acoustics/Radiation.h"
#include "model/math/Acoustics.h"

#include <algorithm>

namespace burp {

void Radiation::setGas(double density, double soundSpeed)
{
    density_ = density;
    soundSpeed_ = soundSpeed;
    updateLoad();
}

void Radiation::setArea(double opening)
{
    area_ = opening;
    updateLoad();
}

void Radiation::setNeighbourInertance(double inertance)
{
    neighbourInertance_ = std::max(inertance, kMinInertance);
}

void Radiation::reset()
{
    flow_ = 0.0;
    inductorFlow_ = 0.0;
    flowDerivative_ = 0.0;
}

double Radiation::step(double dt, double pressure)
{
    if (area_ < kClosedArea) {
        flow_ = 0.0;
        inductorFlow_ = 0.0;
        flowDerivative_ = 0.0;
        return 0.0;
    }

    const double aR = dt / neighbourInertance_ * loadResistance_;
    const double bR = dt / loadInertance_ * loadResistance_;
    const double drive = flow_ + dt / neighbourInertance_ * pressure; // U + a P

    // Cramer's rule on
    //   (1 + aR) U' - aR i'       = U + a P
    //   -bR U'      + (1 + bR) i' = i
    const double determinant = 1.0 + aR + bR;
    const double newFlow = (drive * (1.0 + bR) + aR * inductorFlow_) / determinant;
    inductorFlow_ = ((1.0 + aR) * inductorFlow_ + bR * drive) / determinant;

    flowDerivative_ = (newFlow - flow_) / dt;
    flow_ = newFlow;
    return flow_;
}

double Radiation::flow() const
{
    return flow_;
}

double Radiation::flowDerivative() const
{
    return flowDerivative_;
}

void Radiation::updateLoad()
{
    if (area_ < kClosedArea) {
        return;
    }
    // R_r = 128 rho c / (9 pi^2 A),  L_r = 8 rho / (3 pi sqrt(pi A))
    loadResistance_ = radiationResistance(density_, soundSpeed_, area_);
    loadInertance_ = radiationInertance(density_, area_);
}

} // namespace burp
