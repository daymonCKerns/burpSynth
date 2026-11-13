#pragma once

// Stomach: an elastic bag of gas squeezed by its own wall and by the abdomen.
//
// State: n, the moles of gas. The wall is in balance with the gas at all
// times (its inertia is ignored):
//   gas law:        (P_atm + P) V = n R T            (isothermal, T = 37 C)
//   wall balance:    P = P_ext + E V                 (E = wall elastance, Pa/m^3)
// Eliminating P gives a quadratic in the gas volume V:
//   E V^2 + (P_atm + P_ext) V - n R T = 0
//   V = (-b + sqrt(b^2 + 4 E n R T)) / (2 E),   b = P_atm + P_ext
// Gas leaving at volume flow U (measured at the stomach's pressure):
//   dn/dt = -U (P_atm + P) / (R T)
//
// As the stomach empties, V and P fall, which gives each burp its decay.
// Below a residual volume (1 mL) the reported pressure fades linearly to
// zero so an empty stomach cannot keep pushing.
//
// P_ext is the abdominal pressure (diaphragm and abdominal wall).

namespace burp {

class Reservoir {
public:
    void setWallElastance(double elastance);   // Pa/m^3
    void setExternalPressure(double pressure); // Pa, gauge

    // Sets the gas amount from its volume at atmospheric pressure (m^3).
    void fill(double gasVolume);

    // Adds gas, given as its volume at atmospheric pressure (m^3).
    void addGas(double gasVolume);

    // Empties the stomach.
    void reset();

    double pressure() const; // Pa, gauge
    double volume() const;   // m^3, current gas volume
    double moles() const;

    // Flow leaving through the LES this sample (negative = flowing back in).
    void addOutflow(double flow);

    // Applies the accumulated outflow, then re-solves volume and pressure.
    void step(double dt);

private:
    // V and P from n, E and P_ext.
    void solve();

    static constexpr double kResidualVolume = 1.0e-6; // m^3 (1 mL)

    double elastance_ = 0.0;        // Pa/m^3
    double externalPressure_ = 0.0; // Pa
    double moles_ = 0.0;
    double outflow_ = 0.0;          // m^3/s, accumulated this sample
    double volume_ = 0.0;           // m^3
    double gasPressure_ = 0.0;      // Pa, before the residual fade
};

} // namespace burp
