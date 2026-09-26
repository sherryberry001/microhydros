// MicroHydros - trend och forvarning
//
// Kunden var tydlig med vad som egentligen efterfragas: "upptacka att
// temperaturen successivt okar och varna anvandaren INNAN temperaturen nar en
// niva som riskerar att skada vaxterna". Ett troskelvarde som larmar vid 32 C
// larmar nar skadan redan sker. Det har ar den billigaste mojliga versionen av
// att larma i forvag.
//
// Metod: minstakvadratanpassning av en rat linje over ett glidande tidsfonster.
// Summorna uppdateras inkrementellt, sa kostnaden ar konstant per matvarde och
// minnet ar fast. Ingen flyttalsdivision utom en gang per forfragan.
//
// Varfor inte nagot mer avancerat? For att en MicroHydros-enhet ar ett litet
// slutet skap med en lampa. Termiska forlopp dar ar i praktiken monotona over
// tiotals minuter. En rat linje over ett kvartsfonster beskriver dem bra nog for
// att hinna saga till, och den gar att forklara for en kund. Kalmanfilter och
// ARIMA hor hemma i molnet nar det finns manader av data att trana pa, inte i
// firmware pa en forsta prototyp.

#pragma once

#include "mh/RingBuffer.h"

#include <cmath>
#include <cstdint>

namespace mh {

enum class TrendDirection : uint8_t {
    Unknown = 0, ///< for fa punkter for att saga nagot
    Stable,
    Rising,
    Falling,
};

struct Trend {
    TrendDirection direction;
    float slope_per_hour;    ///< lutning i enhet per timme
    float r_squared;         ///< hur val linjen beskriver punkterna, 0..1
    uint16_t sample_count;
    /// Minuter tills linjen skar den grans som lamnades till compute().
    /// Negativt varde betyder att vi rör oss bort fran gransen.
    float minutes_to_limit;

    /// Lutningen i fonstrets senare halva delat med lutningen i den tidigare.
    /// Diagnostiskt matt: under 1 betyder att forloppet bromsar in. Anvands inte
    /// langre for att avgora om det ska larmas, se projected_equilibrium nedan.
    float slope_ratio;

    /// Skattad jamviktstemperatur, alltsa vad forloppet ar pa vag MOT.
    ///
    /// Det har faltet ar svaret pa den viktigaste fragan kunden stallde: kommer
    /// temperaturen att na en skadlig niva, och i sa fall nar?
    ///
    /// Bakgrunden ar tva verkliga fel som integrationskorningen avslojade, bada
    /// beskrivna i docs/testrapport.md test I-13 och I-14.
    ///
    /// Forsta forsoket var rak linjar extrapolering. Den larmade varje gang
    /// odlingslampan tande, eftersom en rat linje genom borjan av en exponentiell
    /// invangning pekar langt over den niva kurvan faktiskt planar ut pa.
    ///
    /// Andra forsoket var att undertrycka larm nar forloppet bromsade in. Det tog
    /// bort falsklarmen men skulle ocksa ha tystat ett akta skenande forlopp, for
    /// aven en uppvarmning mot 38 C bromsar in pa vagen dit. Ett filter som
    /// missar det verkliga felet ar samre an inget filter alls.
    ///
    /// Losningen ar att sluta gissa och istallet identifiera systemet. Luften i
    /// ett odlingsskap foljer en forsta ordningens modell, y' = (A - y) / tau.
    /// Lutningen ar alltsa proportionell mot hur langt det ar kvar till jamvikten.
    /// Mater man lutningen och medelvardet i fonstrets bada halvor gar bade tau
    /// och A att losa ut i slutna uttryck:
    ///
    ///     tau = (y_sen - y_tidig) / (s_tidig - s_sen)
    ///     A   = y_sen + s_sen * tau
    ///
    /// Da blir fragan enkel och fysikaliskt riktig: ligger A over gransen kommer
    /// vi dit, och tiden raknas ut ur exponentialen istallet for ur en rat linje.
    /// Ligger A under gransen nas den aldrig, hur brant det an lutar just nu.
    ///
    /// Vid ett nara linjart forlopp gar tau mot oandligheten och skattningen blir
    /// meningslos. Da satts has_equilibrium till false och koden faller tillbaka
    /// pa linjar extrapolering, vilket ar det konservativa valet: linjart betyder
    /// att ingen jamvikt syns, och da ska vi varna.
    float projected_equilibrium;
    float tau_hours;       ///< skattad tidskonstant, bara giltig om has_equilibrium
    bool  has_equilibrium; ///< sant om modellen gick att identifiera

    /// Sant nar det aktuella vardet redan ligger pa eller over gransen. Det ar
    /// inte langre en forvarning utan ett pagaende problem, och mottagaren ska
    /// behandla det annorlunda.
    bool limit_exceeded;

    Trend()
        : direction(TrendDirection::Unknown), slope_per_hour(0.0f), r_squared(0.0f),
          sample_count(0), minutes_to_limit(-1.0f), slope_ratio(1.0f),
          projected_equilibrium(0.0f), tau_hours(0.0f), has_equilibrium(false),
          limit_exceeded(false) {}
};

/// Glidande trendfonster.
///
/// Capacity valjs sa att fonstret tacker den tid man vill se over. Med en
/// avlasning var tionde sekund ger 90 punkter 15 minuters fonster, vilket ar
/// langre an brusets tidsskala och kortare an de forlopp vi vill hinna reagera
/// pa.
template <size_t Capacity>
class TrendEstimator {
public:
    TrendEstimator() : stable_threshold_per_hour_(0.5f) {}

    /// Lutningar under detta belopp raknas som Stable. Satt efter hur mycket brus
    /// kanalen har. For en SHT45 med 0.1 C noggrannhet ar 0.5 C per timme lagom.
    void setStableThreshold(float per_hour) { stable_threshold_per_hour_ = per_hour; }

    /// Lagg till en godkand matning. Underkanda varden ska inte matas in har,
    /// annars drar en enda spik trendlinjen med sig.
    void add(float value, uint32_t timestamp_ms) {
        Point p;
        p.t_ms = timestamp_ms;
        p.v = value;
        window_.push(p);
    }

    void clear() { window_.clear(); }
    size_t size() const { return window_.size(); }

    /// Raknar fram trenden. limit ar det varde vi vill veta hur lang tid vi har
    /// kvar till, till exempel 30 C for lufttemperaturen inne.
    Trend compute(float limit) const;

    /// Trend utan tid-till-grans.
    Trend compute() const;

private:
    struct Point {
        uint32_t t_ms;
        float    v;
    };

    /// Minstakvadratlutning over ett delintervall [from, to) av fonstret.
    float slopeOver(size_t from, size_t to) const;

    /// Medelvarde over ett delintervall [from, to).
    float meanOver(size_t from, size_t to) const;

    /// Skattar jamvikt och tidskonstant ur delintervallet [from, to) och fyller
    /// i out om det lyckades. Returnerar false om intervallet inte beter sig som
    /// en forsta ordningens invangning.
    bool identifyFirstOrder(size_t from, size_t to, Trend& out) const;

    RingBuffer<Point, Capacity> window_;
    float stable_threshold_per_hour_;
};

// --- implementation, mall sa den maste ligga i headern ---

template <size_t Capacity>
Trend TrendEstimator<Capacity>::compute() const {
    return compute(0.0f / 0.0f); // NaN, ingen grans
}

template <size_t Capacity>
Trend TrendEstimator<Capacity>::compute(float limit) const {
    Trend out;
    const size_t n = window_.size();
    out.sample_count = static_cast<uint16_t>(n);

    if (n < 3) {
        return out; // Unknown
    }

    // Rakna tiden relativt aldsta punkten och i timmar. Det haller talen sma och
    // gor att en uint32 millisekundsklocka som narmar sig sitt varv inte
    // forstorar precisionen i flyttalen.
    const uint32_t t0 = window_.at(0).t_ms;

    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0, sum_yy = 0.0;
    for (size_t i = 0; i < n; ++i) {
        const double x = static_cast<double>(window_.at(i).t_ms - t0) / 3600000.0;
        const double y = static_cast<double>(window_.at(i).v);
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
        sum_yy += y * y;
    }

    const double nd = static_cast<double>(n);
    const double denom = nd * sum_xx - sum_x * sum_x;
    if (denom <= 1e-12) {
        // Alla punkter har samma tidsstampel. Kan handa om klockan star still.
        return out;
    }

    const double slope = (nd * sum_xy - sum_x * sum_y) / denom;
    out.slope_per_hour = static_cast<float>(slope);

    // Forklaringsgrad. Lag r2 betyder att punkterna sprider sig och att lutningen
    // inte ar nagot att lita pa. Gatewayen kan da valja att inte larma.
    const double ss_tot = sum_yy - (sum_y * sum_y) / nd;
    if (ss_tot > 1e-12) {
        const double intercept = (sum_y - slope * sum_x) / nd;
        double ss_res = 0.0;
        for (size_t i = 0; i < n; ++i) {
            const double x = static_cast<double>(window_.at(i).t_ms - t0) / 3600000.0;
            const double pred = intercept + slope * x;
            const double d = static_cast<double>(window_.at(i).v) - pred;
            ss_res += d * d;
        }
        double r2 = 1.0 - ss_res / ss_tot;
        if (r2 < 0.0) r2 = 0.0;
        if (r2 > 1.0) r2 = 1.0;
        out.r_squared = static_cast<float>(r2);
    } else {
        // Helt plan serie. Linjen beskriver den perfekt.
        out.r_squared = 1.0f;
    }

    if (slope > stable_threshold_per_hour_) {
        out.direction = TrendDirection::Rising;
    } else if (slope < -stable_threshold_per_hour_) {
        out.direction = TrendDirection::Falling;
    } else {
        out.direction = TrendDirection::Stable;
    }

    // --- Identifiera en forsta ordningens modell ---
    //
    // Se den utforliga motiveringen vid Trend::projected_equilibrium.
    //
    // Forsoket gors i tva omgangar. Forst over hela fonstret. Om lutningen dar
    // OKAR istallet for att minska betyder det att nagot just har hant: fonstret
    // straddlar da tva olika forlopp, till exempel lugnt morker foljt av att
    // lampan tander. En forsta ordningens modell passar inte pa tva forlopp
    // samtidigt, sa vi provar om pa enbart fonstrets senare halva, som med stor
    // sannolikhet ligger helt inom det nya forloppet.
    if (n >= 8) {
        const size_t mid = n / 2;
        const float s_early_full = slopeOver(0, mid);
        if (std::fabs(s_early_full) > 1e-3f) {
            out.slope_ratio = slopeOver(mid, n) / s_early_full;
        }

        if (!identifyFirstOrder(0, n, out) && n >= 16) {
            identifyFirstOrder(mid, n, out);
        }
    }

    // --- Hur lang tid har vi kvar till gransen? ---
    //
    // Gransen tolkas som en OVRE grans, vilket den ar for lufttemperaturen inne.
    out.minutes_to_limit = -1.0f;
    if (limit == limit) { // limit != NaN
        const float current = window_.newest().v;

        if (current >= limit) {
            // Redan over. Det ar inte en forvarning utan ett pagaende problem.
            out.limit_exceeded = true;
        } else if (out.has_equilibrium) {
            // Planar forloppet ut under gransen nas den aldrig, hur brant det an
            // lutar just nu. Det ar precis fallet nar odlingslampan tander.
            // Marginalen pa 0.2 C gor att vi inte sager "nas om 400 minuter" nar
            // jamvikten ligger en hundradel over gransen.
            if (out.projected_equilibrium > limit + 0.2f) {
                // Tiden ur exponentialen: t = -tau * ln((A - grans) / (A - nu)).
                const float num = out.projected_equilibrium - limit;
                const float den = out.projected_equilibrium - current;
                if (num > 0.0f && den > 0.0f) {
                    const float hours = -out.tau_hours * std::log(num / den);
                    if (hours > 0.0f && std::isfinite(hours)) {
                        out.minutes_to_limit = hours * 60.0f;
                    }
                }
            }
        } else if (slope > 0.0) {
            // Ingen jamvikt gick att skatta, alltsa ett i praktiken linjart
            // forlopp. Da finns inget som tyder pa att det planar ut, och linjar
            // extrapolering ar det konservativa valet.
            out.minutes_to_limit = static_cast<float>((limit - current) / slope * 60.0);
        }
    }

    return out;
}

template <size_t Capacity>
float TrendEstimator<Capacity>::slopeOver(size_t from, size_t to) const {
    if (to <= from + 1) return 0.0f;
    const size_t n = to - from;
    const uint32_t t0 = window_.at(from).t_ms;

    double sum_x = 0.0, sum_y = 0.0, sum_xx = 0.0, sum_xy = 0.0;
    for (size_t i = from; i < to; ++i) {
        const double x = static_cast<double>(window_.at(i).t_ms - t0) / 3600000.0;
        const double y = static_cast<double>(window_.at(i).v);
        sum_x += x;
        sum_y += y;
        sum_xx += x * x;
        sum_xy += x * y;
    }
    const double nd = static_cast<double>(n);
    const double denom = nd * sum_xx - sum_x * sum_x;
    if (denom <= 1e-12) return 0.0f;
    return static_cast<float>((nd * sum_xy - sum_x * sum_y) / denom);
}

template <size_t Capacity>
bool TrendEstimator<Capacity>::identifyFirstOrder(size_t from, size_t to,
                                                  Trend& out) const {
    const size_t n = to - from;
    if (n < 8) return false;

    const size_t mid = from + n / 2;
    const float s_early = slopeOver(from, mid);
    const float s_late  = slopeOver(mid, to);
    const float y_early = meanOver(from, mid);
    const float y_late  = meanOver(mid, to);

    const float ds = s_early - s_late;

    // Lutningen maste ha minskat markbart mellan halvorna. Ar den nastan
    // oforandrad ar forloppet linjart och det finns ingen jamvikt att skatta:
    // att dela med ett nastan nollskilt tal ger ett slumpmassigt stort A som ser
    // ut som ett svar men inte ar det. Har den istallet OKAT accelererar
    // forloppet, vilket inte heller ar en forsta ordningens invangning.
    if (!(std::fabs(ds) > 0.08f * std::fabs(s_early)) || !(std::fabs(ds) > 0.05f)) {
        return false;
    }
    if (std::fabs(s_late) < 1e-4f) return false;

    const float tau_h = (y_late - y_early) / ds;
    if (!(tau_h > 0.0f) || !std::isfinite(tau_h)) return false;

    const float a = y_late + s_late * tau_h;
    if (!std::isfinite(a)) return false;

    out.tau_hours = tau_h;
    out.projected_equilibrium = a;
    out.has_equilibrium = true;
    return true;
}

template <size_t Capacity>
float TrendEstimator<Capacity>::meanOver(size_t from, size_t to) const {
    if (to <= from) return 0.0f;
    double sum = 0.0;
    for (size_t i = from; i < to; ++i) sum += static_cast<double>(window_.at(i).v);
    return static_cast<float>(sum / static_cast<double>(to - from));
}

const char* toString(TrendDirection d);

} // namespace mh
