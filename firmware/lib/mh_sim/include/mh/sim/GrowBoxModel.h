// MicroHydros - termisk modell av odlingsskapet
//
// Det har ar ingen leksak som slumpar fram tal mellan 20 och 25. Det ar en enkel
// men fysikaliskt uppbyggd modell av vad som faktiskt hander i ett litet slutet
// skap med en lampa, ett karr naringslosning och ett visst lackage mot rummet.
// Poangen ar att valideringslogiken ska testas mot data som beter sig som
// verkligheten: trogt, kopplat och med rimliga tidskonstanter. En modell som
// slumpar oberoende varden hade godkant nastan vilken kod som helst.
//
// Modellen:
//
//   Ute       En langsam dygnssvangning kring ett medelvarde. Rumsluft.
//   Inneluft  Forsta ordningens lag mot (ute + lampans bidrag). Tidskonstant
//             ca 8 minuter, vilket ar vad nagra tiotal liter luft med den
//             lackagearea ett skap har hamnar pa.
//   Vatten    Forsta ordningens lag mot inneluften, tidskonstant ca 90 minuter.
//             Vatten har ungefar 3400 ganger luftens varmekapacitet per volym,
//             sa vattnet foljer efter langt senare. Det ar exakt den tragheten
//             som gor derivatakontrollen i Validator.h meningsfull.
//   Fukt      Absolut fuktighet som tillstand. Avdunstning driver den uppat i
//             takt med angtrycksdeficitet, lackage drar den mot rummets niva.
//             RH raknas sedan fram ur AH och lufttemperaturen, vilket ger den
//             ratta kopplingen: nar lampan tander stiger temperaturen och RH
//             faller trots att mangden vatten i luften ar oforandrad.
//
// Belysningen foljer en fotoperiod, 16 timmar pa och 8 av, vilket ar
// standardschemat for sallad och bladortar.

#pragma once

#include <cstdint>

namespace mh {
namespace sim {

struct GrowBoxParams {
    float outside_mean_c;       ///< rummets medeltemperatur
    float outside_swing_c;      ///< halva dygnsamplituden
    float outside_period_s;     ///< dygnslangd, kortas i tester

    float lamp_delta_c;         ///< hur mycket lampan lyfter jamviktstemperaturen
    float lamp_on_hours;        ///< fotoperiod, timmar tand
    float lamp_cycle_hours;     ///< hela cykeln, typiskt 24

    float air_tau_s;            ///< luftens tidskonstant
    float water_tau_s;          ///< vattnets tidskonstant
    float water_offset_c;       ///< vattnet ligger nagot under luften vid jamvikt

    float evaporation_gain;     ///< avdunstning per kPa VPD och sekund
    float leak_tau_s;           ///< fuktutbyte med rummet
    float outside_rh_percent;   ///< rummets relativa fuktighet

    GrowBoxParams()
        : outside_mean_c(21.0f), outside_swing_c(1.5f), outside_period_s(86400.0f),
          lamp_delta_c(6.5f), lamp_on_hours(16.0f), lamp_cycle_hours(24.0f),
          air_tau_s(480.0f), water_tau_s(5400.0f), water_offset_c(1.2f),
          evaporation_gain(0.004f), leak_tau_s(1800.0f), outside_rh_percent(40.0f) {}
};

/// Modellens sanna tillstand. Det ar de har vardena sensorerna forsoker mata, och
/// det ar mot dem testerna jamfor for att avgora om systemet ar rimligt.
struct GrowBoxState {
    float air_inside_c;
    float air_outside_c;
    float water_c;
    float rh_inside_percent;
    float absolute_humidity_gm3;
    bool  lamp_on;
    double elapsed_s;
};

class GrowBoxModel {
public:
    explicit GrowBoxModel(const GrowBoxParams& p = GrowBoxParams());

    /// Stegar modellen dt sekunder framat. Anropa med samma dt som
    /// simuleringsloopen anvander, typiskt 1 sekund.
    void step(float dt_s);

    const GrowBoxState& state() const { return s_; }
    const GrowBoxParams& params() const { return p_; }

    /// Tvingar fram en storning, anvands i tester for att se att trendlogiken
    /// hinner varna innan gransen nas. Simulerar till exempel en fläkt som stannar
    /// eller att skapet stalls i solen.
    void injectHeatStep(float delta_c) { heat_bias_c_ += delta_c; }

    /// Sla av eller pa lampan manuellt, oberoende av fotoperioden.
    void forceLamp(bool on, bool enabled = true) {
        lamp_forced_ = enabled;
        lamp_forced_state_ = on;
    }

    void reset();

private:
    float outsideAt(double t_s) const;
    bool  lampOnAt(double t_s) const;

    GrowBoxParams p_;
    GrowBoxState  s_;
    float         heat_bias_c_;
    bool          lamp_forced_;
    bool          lamp_forced_state_;
};

} // namespace sim
} // namespace mh
