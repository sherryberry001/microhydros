// MicroHydros - rimlighetskontroll
//
// Kunden skrev att den forra prototypen "fungerade vid demonstration men visade
// sig vara betydligt mindre tillforlitlig nar systemet testades under langre
// tid". Det ar precis den har filen som ska ta hand om det problemet.
//
// En sensor som gar sonder ljuger nastan aldrig genom att sluta svara. Den
// fortsatter svara, men fel. En DS18B20 med glapp i kabeln rapporterar +85.0 C,
// vilket ar dess reset-varde och ser ut som ett helt normalt tal. En SHT4x som
// har fatt kondens i sig visar 100 procent RH i timmar. En I2C-buss som stort ut
// levererar 0xFFFF som blir samma varde om och om igen.
//
// Darfor sitter valideringen i fem lager, dar varje lager fangar en feltyp som
// lagret ovanfor inte kan se:
//
//   L1 Transport   CRC8 och bussfel. Sköts i drivrutinen, kommer hit som ok=false.
//   L2 Sensorspann Utanfor vad databladet lovar. Da ar det inte sensorn vi laser.
//   L3 Rimligt     Innanfor databladet men orimligt for en inomhusodling.
//   L4 Derivata    Andrar sig snabbare an fysiken tillater for den massan.
//   L5 Fastnat     Bitvis identiskt varde for manga avlasningar i rad.
//
// Korskontroller mellan sensorer (daggpunkt over lufttemperatur, vatten som
// springer ifran luften) ligger i CrossChecker langre ner, eftersom de behover se
// flera kanaler samtidigt.
//
// Viktigt designval: ett underkant varde kastas inte bort tyst. Det skickas
// vidare med Quality::Suspect eller Bad och en Reason. Gatewayen far da
// mojligheten att rita ett hal i kurvan istallet for att rita en rak linje over
// ett fel som ingen upptackte.

#pragma once

#include "mh/Measurement.h"

namespace mh {

/// Granser for en kanal. Sensorspannet kommer fran databladet, det rimliga
/// spannet fran odlingsfallet, och derivatagransen fran kanalens termiska
/// troghet.
struct ChannelLimits {
    float sensor_min;
    float sensor_max;
    float plausible_min;
    float plausible_max;
    float max_rate_per_min; ///< tillaten |dv/dt| i enhet per minut, <= 0 = av

    /// Tillagg till derivatakontrollen som tacker givarens egen matosakerhet.
    ///
    /// Utan det har faltet mater derivatakontrollen bruset istallet for fysiken.
    /// En DS18B20 brusar ca 0.05 C och kvantiserar i steg om 0.0625 C. Tva
    /// avlasningar tio sekunder isar skiljer sig darfor typiskt 0.07 C av rena
    /// matskal, vilket omraknat blir 0.42 C per minut. Med en fysikalisk grans pa
    /// 1.0 C per minut rackte det med ett par standardavvikelser for att slå i
    /// taket, och i den forsta integrationskorningen underkandes 3.6 procent av
    /// alla vattenmatningar i helt felfri drift. Se docs/testrapport.md test I-15.
    ///
    /// Tillatet steg mellan tva avlasningar ar darfor
    ///
    ///     max_rate_per_min * dt_minuter + rate_noise_allowance
    ///
    /// Konstanten satts till ungefar fyra ganger skillnadens standardavvikelse,
    /// vilket gor falsklarm av brus i praktiken omojliga samtidigt som den
    /// verkliga feltypen, en sond som hoppar till 85 C, har over hundra ganger
    /// marginalen kvar.
    float rate_noise_allowance;

    float stuck_epsilon;    ///< tva varden inom denna skillnad raknas som lika
    uint8_t stuck_limit;    ///< sa manga identiska i rad ger StuckValue, 0 = av

    /// Lufttemperatur inne, SHT45. Databladet ger -40..125 C. Ett odlingsskap som
    /// star inomhus lamnar aldrig 0..60 C utan att nagot ar allvarligt fel, och
    /// luften i ett litet skap kan andra sig snabbt nar lampan slar pa, darav de
    /// generosa 6 C per minut.
    static ChannelLimits airInside();

    /// Lufttemperatur ute, SHT40. Rumsluft utanfor skapet. Vidare rimligt spann
    /// eftersom enheten kan sta i ett kallt forrad, men langsammare tillaten
    /// forandring eftersom rumsvolymen ar mycket storre.
    static ChannelLimits airOutside();

    /// Vattentemperatur, DS18B20. Fryst eller kokande naringslosning betyder att
    /// odlingen redan ar forlorad, sa 0..45 C racker. Den viktiga gransen ar
    /// derivatan: naringslosningen i en MicroHydros har en termisk massa pa nagra
    /// liter vatten och kan fysiskt inte andra sig mer an ca 1 C per minut. Det
    /// ar den regeln som fangar en glappande sond som hoppar till 85 C.
    static ChannelLimits waterTemperature();

    /// Relativ fuktighet inne, SHT45. 0..100 ar hela definitionsmangden, men
    /// under 10 procent i ett slutet skap med vatten i botten ar orimligt och
    /// betyder nastan alltid en trasig sensor.
    static ChannelLimits humidityInside();
};

/// Utfall fran en validering.
struct Verdict {
    Quality quality;
    Reason  reason;
};

/// Validerar en kanal over tid. En instans per kanal, eftersom derivata- och
/// fastnat-kontrollerna behover minnas foregaende avlasning.
class ChannelValidator {
public:
    ChannelValidator();
    explicit ChannelValidator(const ChannelLimits& limits);

    void setLimits(const ChannelLimits& limits) { limits_ = limits; }
    const ChannelLimits& limits() const { return limits_; }

    /// Kör alla lager och uppdaterar internt tillstand.
    /// Ett Bad-varde uppdaterar medvetet inte referensen for derivatakontrollen,
    /// annars skulle en enda spik gora att nasta korrekta varde ocksa underkands.
    Verdict evaluate(const RawReading& raw);

    /// Nollstaller historiken, till exempel efter en avsiktlig omstart av bussen.
    void reset();

    bool     hasHistory() const { return has_history_; }
    float    lastAccepted() const { return last_value_; }
    uint32_t lastAcceptedMs() const { return last_ms_; }
    uint8_t  stuckCount() const { return stuck_count_; }

private:
    ChannelLimits limits_;
    float    last_value_;
    uint32_t last_ms_;
    bool     has_history_;
    uint8_t  stuck_count_;
};

/// Kontroller som kraver flera kanaler samtidigt.
///
/// De har ar vardefulla for att de fangar fel som varje enskild kanal tycker ser
/// helt normalt ut. Tva sensorer kan bada ge fysiskt mojliga varden och anda vara
/// omojliga tillsammans.
namespace crosscheck {

/// Daggpunkten kan per definition aldrig ligga over lufttemperaturen i samma
/// punkt. Gor den det ar antingen temperatur- eller fuktkanalen fel, och vi vet
/// inte vilken. Bada markeras Suspect.
/// Tolerans i grader for matosakerheten hos de bada givarna.
bool dewPointConsistent(float air_t_c, float dew_point_c, float tolerance_c = 0.5f);

/// Naringslosningen ar termiskt kopplad till luften i skapet. Over tid kan de
/// skilja sig, lampan varmer luften snabbare an vattnet, men ett vatten som
/// ligger mer an ett antal grader fran luften i ett litet slutet skap betyder
/// nastan alltid att sonden har lossnat och ligger utanfor karet.
bool waterAirCoupled(float air_t_c, float water_t_c, float max_delta_c = 15.0f);

/// Inne i ett upplyst skap ar det med mycket fa undantag varmare an utanfor,
/// eftersom lysrorstaket och pumpen tillfor effekt. Om det ar markant kallare
/// inne an ute ar sannolikt de tva luftgivarna forvaxlade vid montering.
/// Returnerar false vid misstankt forvaxling.
bool insideOutsideOrdering(float inside_c, float outside_c, float max_inversion_c = 5.0f);

} // namespace crosscheck
} // namespace mh
