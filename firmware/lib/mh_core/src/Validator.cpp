#include "mh/Validator.h"

#include <cmath>

namespace mh {

// --- Grunsvarden per kanal -------------------------------------------------
//
// Siffrorna nedan ar inte gissade. Sensorspannen kommer fran databladen, de
// rimliga spannen fran vad ett inomhusodlingsskap faktiskt kan utsattas for, och
// derivatagranserna fran en overslagsrakning pa respektive medias termiska
// troghet. Motiveringen i sin helhet finns i docs/adr/ADR-004-validering.md.

ChannelLimits ChannelLimits::airInside() {
    ChannelLimits l;
    l.sensor_min = -40.0f;   // SHT45 datablad
    l.sensor_max = 125.0f;
    l.plausible_min = 0.0f;  // fryser det inne i skapet ar odlingen slut anda
    l.plausible_max = 60.0f; // over detta ar det brand, inte odlingsklimat
    l.max_rate_per_min = 6.0f;  // luften i nagra liter skap vid lampstart
    l.rate_noise_allowance = 0.25f; // SHT45 brusar ca 0.04 C
    l.stuck_epsilon = 0.001f;   // SHT45 har 0.01 C upplosning, exakt lika = last
    l.stuck_limit = 30;         // 30 avlasningar a 10 s = 5 minuter
    return l;
}

ChannelLimits ChannelLimits::airOutside() {
    ChannelLimits l;
    l.sensor_min = -40.0f;   // SHT40 datablad
    l.sensor_max = 125.0f;
    l.plausible_min = -20.0f; // enheten kan sta i ett ouppvarmt forrad
    l.plausible_max = 50.0f;
    l.max_rate_per_min = 2.0f; // rumsvolym, mycket trogare an skapet
    l.rate_noise_allowance = 0.40f; // SHT40 brusar ca 0.08 C
    l.stuck_epsilon = 0.001f;
    l.stuck_limit = 30;
    return l;
}

ChannelLimits ChannelLimits::waterTemperature() {
    ChannelLimits l;
    l.sensor_min = -55.0f;  // DS18B20 datablad
    l.sensor_max = 125.0f;
    l.plausible_min = 0.0f; // fryst naringslosning
    l.plausible_max = 45.0f;
    // Nagra liter vatten har en varmekapacitet pa storleksordningen 10 kJ/K.
    // Pumpen och lampan tillfor tillsammans under 50 W till losningen, vilket ger
    // hogst nagra hundradels grader per minut. 1.0 C/min ar darfor redan mycket
    // generost och fangar anda den glappande sondens hopp till 85 C.
    l.max_rate_per_min = 1.0f;
    l.rate_noise_allowance = 0.30f; // DS18B20 brus 0.05 C plus 0.0625 C kvantisering
    l.stuck_epsilon = 0.0001f; // DS18B20 upplosning 0.0625 C
    l.stuck_limit = 30;
    return l;
}

ChannelLimits ChannelLimits::humidityInside() {
    ChannelLimits l;
    l.sensor_min = 0.0f;
    l.sensor_max = 100.0f;
    // Ett slutet skap med ett oppet karr vatten och transpirerande vaxter kommer
    // aldrig under 15 procent RH. Gor det det ar det sensorn som ar trasig.
    l.plausible_min = 15.0f;
    l.plausible_max = 100.0f;
    l.max_rate_per_min = 20.0f; // fukt svarar snabbt pa lucköppning
    l.rate_noise_allowance = 1.5f; // SHT45 brusar ca 0.35 procent RH
    l.stuck_epsilon = 0.001f;
    l.stuck_limit = 30;
    return l;
}

// --- ChannelValidator ------------------------------------------------------

ChannelValidator::ChannelValidator()
    : limits_(ChannelLimits::airInside()), last_value_(0.0f), last_ms_(0),
      has_history_(false), stuck_count_(0) {}

ChannelValidator::ChannelValidator(const ChannelLimits& limits)
    : limits_(limits), last_value_(0.0f), last_ms_(0), has_history_(false),
      stuck_count_(0) {}

void ChannelValidator::reset() {
    has_history_ = false;
    stuck_count_ = 0;
    last_value_ = 0.0f;
    last_ms_ = 0;
}

Verdict ChannelValidator::evaluate(const RawReading& raw) {
    Verdict v{Quality::Good, Reason::None};

    // L1: drivrutinen sa redan ifran. CRC-fel eller ingen respons.
    if (!raw.ok) {
        return Verdict{Quality::Bad, raw.error};
    }

    // NaN och oandligheter ska aldrig slappas vidare, de forgiftar all
    // efterfoljande matematik inklusive trendberakningen.
    if (!std::isfinite(raw.value)) {
        return Verdict{Quality::Bad, Reason::OutOfSensorRange};
    }

    // L2: utanfor databladets spann. Da laser vi inte sensorn, vi laser brus.
    if (raw.value < limits_.sensor_min || raw.value > limits_.sensor_max) {
        return Verdict{Quality::Bad, Reason::OutOfSensorRange};
    }

    // L3: innanfor databladet men orimligt for anvandningsfallet. Detta ar
    // Suspect och inte Bad, for vardet ar tekniskt giltigt och kan vara sant om
    // nagot ovantat har hant med odlingen. Vi vill se det i grafen.
    if (raw.value < limits_.plausible_min || raw.value > limits_.plausible_max) {
        v.quality = Quality::Suspect;
        v.reason = Reason::OutOfPlausibleRange;
    }

    if (has_history_) {
        // L5: har vardet fastnat? Kolla fore derivatan, eftersom ett fastnat
        // varde har derivatan noll och annars alltid skulle se friskt ut.
        if (limits_.stuck_limit > 0 &&
            std::fabs(raw.value - last_value_) <= limits_.stuck_epsilon) {
            if (stuck_count_ < 255) ++stuck_count_;
            if (stuck_count_ >= limits_.stuck_limit && v.quality == Quality::Good) {
                v.quality = Quality::Suspect;
                v.reason = Reason::StuckValue;
            }
        } else {
            stuck_count_ = 0;
        }

        // L4: derivatakontroll. Jamfor mot senast accepterade varde, inte mot
        // senaste avlasning, sa att en ensam spik inte flyttar referensen och far
        // nasta korrekta varde att se ut som ett hopp at andra hallet.
        if (limits_.max_rate_per_min > 0.0f) {
            const uint32_t dt_ms = raw.timestamp_ms - last_ms_; // talar overflow
            if (dt_ms > 0) {
                const float dt_min = static_cast<float>(dt_ms) / 60000.0f;
                // Jamfor absoluta steg istallet for hastigheter, sa att givarens
                // egen matosakerhet kan laggas till som en konstant. Se
                // ChannelLimits::rate_noise_allowance for varfor.
                const float allowed =
                    limits_.max_rate_per_min * dt_min + limits_.rate_noise_allowance;
                if (std::fabs(raw.value - last_value_) > allowed) {
                    v.quality = Quality::Suspect;
                    v.reason = Reason::RateOfChangeExceeded;
                }
            }
        }
    }

    // Referensen flyttas bara av varden vi tror pa. En Suspect-avlasning far inte
    // bli utgangspunkt for nasta derivatakontroll, annars kan en sensor glida
    // ivag i sma steg utan att nagon regel nagonsin loser ut.
    if (v.quality == Quality::Good) {
        last_value_ = raw.value;
        last_ms_ = raw.timestamp_ms;
        has_history_ = true;
    } else if (!has_history_) {
        // Forsta avlasningen var redan tveksam. Ta den anda som referens, annars
        // kan vi aldrig komma igang och alla efterfoljande varden saknar historik.
        last_value_ = raw.value;
        last_ms_ = raw.timestamp_ms;
        has_history_ = true;
    }

    return v;
}

// --- Korskontroller --------------------------------------------------------

namespace crosscheck {

bool dewPointConsistent(float air_t_c, float dew_point_c, float tolerance_c) {
    if (!std::isfinite(air_t_c) || !std::isfinite(dew_point_c)) return false;
    return dew_point_c <= air_t_c + tolerance_c;
}

bool waterAirCoupled(float air_t_c, float water_t_c, float max_delta_c) {
    if (!std::isfinite(air_t_c) || !std::isfinite(water_t_c)) return false;
    return std::fabs(air_t_c - water_t_c) <= max_delta_c;
}

bool insideOutsideOrdering(float inside_c, float outside_c, float max_inversion_c) {
    if (!std::isfinite(inside_c) || !std::isfinite(outside_c)) return false;
    return inside_c >= outside_c - max_inversion_c;
}

} // namespace crosscheck
} // namespace mh
