// Tube: implementation of the cell, link and wall updates in Tube.h as
// whole-array Eigen expressions over the cells in use.
//
// Coefficients (compliance, link inertance and resistance, wall area) depend
// on the areas, so they are recomputed whenever the area ramp moves.
// Nothing here allocates after prepare().

#include "model/acoustics/Tube.h"
#include "model/math/Acoustics.h"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <numbers>

namespace burp {

void Tube::prepare(double maxLength, double sampleRate)
{
    sampleRate_ = sampleRate;

    // Carbon dioxide has the slowest sound, so it needs the most cells.
    capacity_ = cellCountForLength(maxLength, speedOfSound(kCarbonDioxide, kBodyTemperature),
                                   1.0 / sampleRate);

    for (Eigen::ArrayXd* state : { &pressure_, &netFlow_, &area_, &areaTarget_, &areaStep_,
                                   &compliance_, &wallArea_, &wallVelocity_, &wallPosition_,
                                   &flow_, &inertance_, &resistance_ }) {
        state->resize(capacity_);
        state->setZero();
    }

    // A valid, closed tube until the owner configures it.
    AreaFunction closed;
    closed.fill(1, kMinArea);
    configure(maxLength, closed);
}

void Tube::configure(double tubeLength, const AreaFunction& shape)
{
    count_ = std::min(capacity_, cellCountForLength(tubeLength, soundSpeed_, 1.0 / sampleRate_));
    length_ = tubeLength;
    dx_ = tubeLength / count_;

    sampleTargets(shape);
    area_.head(count_) = areaTarget_.head(count_);
    areaStep_.setZero();
    rampRemaining_ = 0;

    reset();
    updateCoefficients();
}

void Tube::setGas(double density, double soundSpeed, double viscosity)
{
    density_ = density;
    soundSpeed_ = soundSpeed;
    viscosity_ = viscosity;
    if (count_ > 0) {
        updateCoefficients();
    }
}

void Tube::setWallScale(double scale)
{
    wallScale_ = scale;
}

void Tube::setWallStiffness(double scale)
{
    wallStiffness_ = scale;
}

void Tube::setTargetShape(const AreaFunction& shape, int rampSamples)
{
    sampleTargets(shape);

    if (rampSamples <= 0) {
        area_.head(count_) = areaTarget_.head(count_);
        areaStep_.head(count_).setZero();
        rampRemaining_ = 0;
        updateCoefficients();
        return;
    }

    // Slew limit: A_target <- A + clamp(A_target - A, +-max(relative A, floor) T),
    // T = the ramp's duration.
    if (slewRelative_ > 0.0) {
        const double duration = static_cast<double>(rampSamples) / sampleRate_;
        const auto area = area_.head(count_);
        const auto limit = (slewRelative_ * area).max(slewFloor_) * duration;
        areaTarget_.head(count_) = area + (areaTarget_.head(count_) - area).min(limit).max(-limit);
    }

    areaStep_.head(count_) =
        (areaTarget_.head(count_) - area_.head(count_)) / static_cast<double>(rampSamples);
    rampRemaining_ = rampSamples;
}

void Tube::setSlewLimit(double relativeRate, double floorRate)
{
    slewRelative_ = relativeRate;
    slewFloor_ = floorRate;
}

void Tube::reset()
{
    pressure_.setZero();
    netFlow_.setZero();
    flow_.setZero();
    wallVelocity_.setZero();
    wallPosition_.setZero();
}

void Tube::stepLinks(double dt)
{
    const int links = count_ - 1;

    // U <- (U + dt/L (P_i - P_(i+1))) / (1 + dt R / L)   (stepInertance, per link)
    flow_.head(links) =
        (flow_.head(links)
         + dt * (pressure_.head(links) - pressure_.segment(1, links)) / inertance_.head(links))
        / (1.0 + dt * resistance_.head(links) / inertance_.head(links));

    // Link i leaves cell i and enters cell i+1.
    netFlow_.head(links) -= flow_.head(links);
    netFlow_.segment(1, links) += flow_.head(links);
}

void Tube::stepCells(double dt)
{
    // m y'' + r y' + k y = P per unit wall area (stepMassSpring, per cell)
    wallVelocity_.head(count_) =
        (wallVelocity_.head(count_)
         + (dt / kWallMass)
               * (pressure_.head(count_) - (wallStiffness_ * kWallStiffness) * wallPosition_.head(count_)))
        / (1.0 + dt * kWallDamping / kWallMass);
    wallPosition_.head(count_) += dt * wallVelocity_.head(count_);

    // C dP/dt = netFlow - dx dA/dt - scale S y'   (dt dA/dt is the ramp step)
    pressure_.head(count_) +=
        (dt * (netFlow_.head(count_)
               - wallScale_ * wallArea_.head(count_) * wallVelocity_.head(count_))
         - dx_ * areaStep_.head(count_))
        / compliance_.head(count_);
    netFlow_.head(count_).setZero();

    if (rampRemaining_ > 0) {
        --rampRemaining_;
        if (rampRemaining_ == 0) {
            area_.head(count_) = areaTarget_.head(count_);
            areaStep_.head(count_).setZero();
        } else {
            area_.head(count_) += areaStep_.head(count_);
        }
        updateCoefficients();
    }
}

double Tube::inletPressure() const
{
    return pressure_(0);
}

double Tube::outletPressure() const
{
    return pressure_(count_ - 1);
}

void Tube::addInletFlow(double flow)
{
    netFlow_(0) += flow;
}

void Tube::addOutletFlow(double flow)
{
    netFlow_(count_ - 1) -= flow;
}

double Tube::inletArea() const
{
    return area_(0);
}

double Tube::outletArea() const
{
    return area_(count_ - 1);
}

double Tube::inletHalfInertance() const
{
    return inertance(density_, 0.5 * dx_, area_(0));
}

double Tube::outletHalfInertance() const
{
    return inertance(density_, 0.5 * dx_, area_(count_ - 1));
}

int Tube::cellCount() const
{
    return count_;
}

double Tube::cellLength() const
{
    return dx_;
}

double Tube::length() const
{
    return length_;
}

const Eigen::ArrayXd& Tube::pressures() const
{
    return pressure_;
}

const Eigen::ArrayXd& Tube::areas() const
{
    return area_;
}

void Tube::sampleTargets(const AreaFunction& shape)
{
    for (int i = 0; i < count_; ++i) {
        areaTarget_(i) = std::max(kMinArea, shape.sample((i + 0.5) / count_));
    }
}

void Tube::updateCoefficients()
{
    const int links = count_ - 1;

    // C_i = A_i dx / (rho c^2): a unit-area cell's compliance times A_i
    compliance_.head(count_) = compliance(dx_, density_, soundSpeed_) * area_.head(count_);

    // L_i = rho (dx/2) / A_i + rho (dx/2) / A_(i+1)
    const double halfInertance = inertance(density_, 0.5 * dx_, 1.0);
    inertance_.head(links) =
        halfInertance * (area_.head(links).inverse() + area_.segment(1, links).inverse());

    // R_i = 8 pi mu (dx/2) / A_i^2 + 8 pi mu (dx/2) / A_(i+1)^2
    const double halfResistance = viscousResistance(viscosity_, 0.5 * dx_, 1.0);
    resistance_.head(links) = halfResistance
                              * (area_.head(links).square().inverse()
                                 + area_.segment(1, links).square().inverse());

    // S_i = 2 sqrt(pi A_i) dx
    wallArea_.head(count_) = (2.0 * dx_ * std::sqrt(std::numbers::pi)) * area_.head(count_).sqrt();
}

} // namespace burp
