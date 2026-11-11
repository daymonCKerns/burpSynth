#pragma once

// Physical constants and unit conversions.
//
// The model works in SI units throughout: Pa (gauge unless stated), m, m^2,
// m^3, m^3/s, kg, s, K. Values quoted in mmHg, cm or mL are converted with
// these functions at the edge of the model and nowhere else.

namespace burp {

inline constexpr double kGasConstant = 8.314462618;      // J/(mol K)
inline constexpr double kBodyTemperature = 310.15;       // K (37 C)
inline constexpr double kAtmosphericPressure = 101325.0; // Pa, absolute
inline constexpr double kViscosity = 1.9e-5;             // Pa s, air near 37 C

constexpr double mmHgToPa(double mmHg) { return mmHg * 133.322387415; }
constexpr double cmToM(double cm) { return cm * 1.0e-2; }
constexpr double cm2ToM2(double cm2) { return cm2 * 1.0e-4; }
constexpr double mlToM3(double ml) { return ml * 1.0e-6; }
constexpr double msToS(double ms) { return ms * 1.0e-3; }

} // namespace burp
