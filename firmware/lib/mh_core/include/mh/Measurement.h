// MicroHydros - matvardesmodell
//
// Hela systemet bygger pa att en matning aldrig ar bara ett tal. Den bar alltid
// med sig vad den mater, var den mats, nar den mattes och hur mycket vi litar pa
// den. Det ar det som gor att gatewayen kan lagra aven tveksamma varden utan att
// forstora historiken, istallet for att firmwaren tyst kastar bort dem.
//
// Inga heapallokeringar, inga std::string. Allt ar POD med kand storlek sa att
// en frame kan ligga i en statiskt allokerad ringbuffert.

#pragma once

#include <cstdint>
#include <cstddef>

namespace mh {

/// Vad som mats. Enheten foljer av storheten och ar alltid SI-baserad.
enum class Quantity : uint8_t {
    AirTemperature = 0,    ///< grader Celsius
    WaterTemperature,      ///< grader Celsius
    RelativeHumidity,      ///< procent RH
    DewPoint,              ///< grader Celsius, harledd
    VapourPressureDeficit, ///< kPa, harledd
};

/// Var i odlingen matningen kommer ifran.
enum class Location : uint8_t {
    Inside = 0,  ///< inne i odlingsutrymmet
    Outside,     ///< utanfor odlingsutrymmet
    Water,       ///< i naringslosningen
    Derived,     ///< raknat fram ur andra matningar
};

/// Hur mycket systemet litar pa vardet.
///
/// Good    = passerade alla kontroller, anvand rakt av.
/// Suspect = vardet ar fysiskt mojligt men nagot ar konstigt. Lagra det, rita
///           det, men larma inte pa det och rakna inte trend pa det.
/// Bad     = vardet ar inte anvandbart. Varde satt till NaN.
enum class Quality : uint8_t {
    Good = 0,
    Suspect,
    Bad,
};

/// Varfor kvaliteten inte ar Good. Skickas med i telemetrin sa att man kan
/// felsoka en enhet ute i falt utan att koppla in en debugger.
enum class Reason : uint8_t {
    None = 0,
    ChecksumFailed,       ///< CRC8 fran sensorn stamde inte
    NoResponse,           ///< sensorn svarade inte pa bussen
    OutOfSensorRange,     ///< utanfor det datablad ens lovar
    OutOfPlausibleRange,  ///< innanfor databladet men orimligt for en odling
    RateOfChangeExceeded, ///< andrade sig snabbare an fysiken tillater
    StuckValue,           ///< exakt samma varde for lange, sensorn har last sig
    CrossCheckFailed,     ///< motsager en annan sensor, t.ex. daggpunkt > lufttemp
    Stale,                ///< ingen fardsk avlasning inom forvantad tid
    NotInitialised,       ///< sensorn har aldrig levererat ett giltigt varde
    DerivedFromSuspect,   ///< harlett varde vars indata var tveksam
};

/// Hur manga tecken ett sensor-id far vara, inklusive avslutande nolla.
static constexpr size_t kSensorIdLen = 12;

/// En enskild matning.
struct Measurement {
    char     sensor_id[kSensorIdLen]; ///< "air_in", "air_out", "water", "rh_in"
    Quantity quantity;
    Location location;
    Quality  quality;
    Reason   reason;
    float    value;        ///< NaN om quality == Bad
    uint32_t timestamp_ms; ///< monoton tid sedan uppstart, inte vagguret

    Measurement()
        : sensor_id{0},
          quantity(Quantity::AirTemperature),
          location(Location::Inside),
          quality(Quality::Bad),
          reason(Reason::NotInitialised),
          value(0.0f),
          timestamp_ms(0) {}
};

/// Rapresultat fran en drivrutin innan valideringen har sagt sitt.
/// Drivrutiner ska inte kanna till applikationens rimlighetsgranser, de ska bara
/// saga om bussen och checksumman fungerade.
struct RawReading {
    bool     ok;           ///< false = transportfel eller CRC-fel
    Reason   error;        ///< bara meningsfull nar ok == false
    float    value;
    uint32_t timestamp_ms;

    RawReading() : ok(false), error(Reason::NotInitialised), value(0.0f), timestamp_ms(0) {}

    static RawReading good(float v, uint32_t ts) {
        RawReading r;
        r.ok = true;
        r.error = Reason::None;
        r.value = v;
        r.timestamp_ms = ts;
        return r;
    }

    static RawReading fail(Reason why, uint32_t ts) {
        RawReading r;
        r.ok = false;
        r.error = why;
        r.value = 0.0f;
        r.timestamp_ms = ts;
        return r;
    }
};

const char* toString(Quantity q);
const char* toString(Location l);
const char* toString(Quality q);
const char* toString(Reason r);
const char* unitOf(Quantity q);

} // namespace mh
