#include "mh/sim/GrowBoxModel.h"
#include "mh/Psychrometrics.h"

#include <cmath>

namespace mh {
namespace sim {

GrowBoxModel::GrowBoxModel(const GrowBoxParams& p)
    : p_(p), heat_bias_c_(0.0f), lamp_forced_(false), lamp_forced_state_(false) {
    reset();
}

void GrowBoxModel::reset() {
    s_.elapsed_s = 0.0;
    s_.air_outside_c = outsideAt(0.0);
    s_.air_inside_c = s_.air_outside_c;
    s_.water_c = s_.air_outside_c;
    s_.lamp_on = lampOnAt(0.0);
    // Starta med samma absoluta fuktighet som rummet, sa att modellen inte
    // borjar med ett orimligt hopp.
    s_.absolute_humidity_gm3 =
        psychro::absoluteHumidityGm3(s_.air_outside_c, p_.outside_rh_percent);
    s_.rh_inside_percent = p_.outside_rh_percent;
    heat_bias_c_ = 0.0f;
}

float GrowBoxModel::outsideAt(double t_s) const {
    const double phase = 2.0 * 3.14159265358979 * t_s / p_.outside_period_s;
    return p_.outside_mean_c + p_.outside_swing_c * static_cast<float>(std::sin(phase));
}

bool GrowBoxModel::lampOnAt(double t_s) const {
    if (lamp_forced_) return lamp_forced_state_;
    const double cycle_s = p_.lamp_cycle_hours * 3600.0;
    const double on_s = p_.lamp_on_hours * 3600.0;
    const double into = std::fmod(t_s, cycle_s);
    return into < on_s;
}

void GrowBoxModel::step(float dt_s) {
    if (dt_s <= 0.0f) return;

    s_.elapsed_s += dt_s;
    s_.air_outside_c = outsideAt(s_.elapsed_s);
    s_.lamp_on = lampOnAt(s_.elapsed_s);

    // Luften stravar mot rummets temperatur plus lampans bidrag plus eventuell
    // pahangd storning. Forsta ordningens lag, exakt losning over steget sa att
    // resultatet blir oberoende av vilket dt man valjer.
    const float lamp = s_.lamp_on ? p_.lamp_delta_c : 0.0f;
    const float air_target = s_.air_outside_c + lamp + heat_bias_c_;
    const float air_alpha = 1.0f - std::exp(-dt_s / p_.air_tau_s);
    s_.air_inside_c += (air_target - s_.air_inside_c) * air_alpha;

    // Vattnet foljer luften men mycket langsammare, och ligger nagot under
    // eftersom avdunstningen kyler ytan.
    const float water_target = s_.air_inside_c - p_.water_offset_c;
    const float water_alpha = 1.0f - std::exp(-dt_s / p_.water_tau_s);
    s_.water_c += (water_target - s_.water_c) * water_alpha;

    // Fukten. Avdunstningen ar proportionell mot angtrycksdeficitet, alltsa mot
    // hur torr luften ar, vilket ar samma mekanism som driver vaxtens
    // transpiration. Lackaget drar absoluta fuktigheten mot rummets niva.
    const float vpd = psychro::vapourPressureDeficitKPa(s_.air_inside_c, s_.rh_inside_percent);
    const float evaporation = (vpd > 0.0f ? vpd : 0.0f) * p_.evaporation_gain * dt_s;

    const float ah_outside =
        psychro::absoluteHumidityGm3(s_.air_outside_c, p_.outside_rh_percent);
    const float leak_alpha = 1.0f - std::exp(-dt_s / p_.leak_tau_s);

    s_.absolute_humidity_gm3 += evaporation;
    s_.absolute_humidity_gm3 += (ah_outside - s_.absolute_humidity_gm3) * leak_alpha;

    // Rakna tillbaka RH ur absolut fuktighet och aktuell lufttemperatur. Det ar
    // har kopplingen uppstar: nar lampan tander stiger temperaturen, mattnaden
    // okar, och RH faller aven om vattenmangden ar oforandrad. Samma beteende som
    // en riktig odlare ser i sitt skap.
    const float ah_sat = psychro::absoluteHumidityGm3(s_.air_inside_c, 100.0f);
    if (ah_sat > 0.0f) {
        s_.rh_inside_percent = 100.0f * s_.absolute_humidity_gm3 / ah_sat;
    }
    if (s_.rh_inside_percent > 100.0f) {
        // Overmattnad blir kondens, inte RH over 100. Overskottet faller ut.
        s_.rh_inside_percent = 100.0f;
        s_.absolute_humidity_gm3 = ah_sat;
    }
    if (s_.rh_inside_percent < 0.0f) {
        s_.rh_inside_percent = 0.0f;
    }

    // Storningen klingar av langsamt, den ar en handelse och inte ett nytt
    // permanent driftlage.
    if (heat_bias_c_ != 0.0f) {
        heat_bias_c_ *= std::exp(-dt_s / 7200.0f);
        if (std::fabs(heat_bias_c_) < 0.01f) heat_bias_c_ = 0.0f;
    }
}

} // namespace sim
} // namespace mh
