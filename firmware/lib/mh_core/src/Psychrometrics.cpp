#include "mh/Psychrometrics.h"

#include <cmath>

namespace mh {
namespace psychro {

static const float kNaN = NAN;

float saturationVapourPressureKPa(float t_c) {
    // Magnus, Alduchov och Eskridge (1996). 0.61094 kPa ar mattnadstrycket vid 0 C.
    return 0.61094f * std::exp((kMagnusA * t_c) / (t_c + kMagnusB));
}

float dewPointC(float t_c, float rh_percent) {
    if (!(rh_percent > 0.0f) || rh_percent > 100.0f) {
        return kNaN;
    }
    // Gamma ar logaritmen av det verkliga angtrycket relativt referensen.
    const float gamma = std::log(rh_percent / 100.0f) + (kMagnusA * t_c) / (t_c + kMagnusB);
    const float denom = kMagnusA - gamma;
    if (std::fabs(denom) < 1e-6f) {
        return kNaN;
    }
    return (kMagnusB * gamma) / denom;
}

float vapourPressureDeficitKPa(float t_c, float rh_percent) {
    if (rh_percent < 0.0f || rh_percent > 100.0f) {
        return kNaN;
    }
    const float es = saturationVapourPressureKPa(t_c);
    return es * (1.0f - rh_percent / 100.0f);
}

float absoluteHumidityGm3(float t_c, float rh_percent) {
    if (rh_percent < 0.0f || rh_percent > 100.0f) {
        return kNaN;
    }
    // Idealgaslagen for vattenanga: AH = e * M_w / (R * T). Med M_w = 18.016 g/mol
    // och R = 8.314 J/(mol K) blir M_w/R = 2.167, och med angtrycket uttryckt i
    // hPa istallet for Pa blir konstanten 216.7.
    const float e_hpa = saturationVapourPressureKPa(t_c) * 10.0f * (rh_percent / 100.0f);
    return 216.74f * e_hpa / (t_c + 273.15f);
}

} // namespace psychro
} // namespace mh
