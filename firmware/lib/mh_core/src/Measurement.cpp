#include "mh/Measurement.h"

namespace mh {

const char* toString(Quantity q) {
    switch (q) {
        case Quantity::AirTemperature:        return "air_temperature";
        case Quantity::WaterTemperature:      return "water_temperature";
        case Quantity::RelativeHumidity:      return "relative_humidity";
        case Quantity::DewPoint:              return "dew_point";
        case Quantity::VapourPressureDeficit: return "vpd";
    }
    return "unknown";
}

const char* toString(Location l) {
    switch (l) {
        case Location::Inside:  return "inside";
        case Location::Outside: return "outside";
        case Location::Water:   return "water";
        case Location::Derived: return "derived";
    }
    return "unknown";
}

const char* toString(Quality q) {
    switch (q) {
        case Quality::Good:    return "good";
        case Quality::Suspect: return "suspect";
        case Quality::Bad:     return "bad";
    }
    return "unknown";
}

const char* toString(Reason r) {
    switch (r) {
        case Reason::None:                 return "none";
        case Reason::ChecksumFailed:       return "checksum_failed";
        case Reason::NoResponse:           return "no_response";
        case Reason::OutOfSensorRange:     return "out_of_sensor_range";
        case Reason::OutOfPlausibleRange:  return "out_of_plausible_range";
        case Reason::RateOfChangeExceeded: return "rate_of_change_exceeded";
        case Reason::StuckValue:           return "stuck_value";
        case Reason::CrossCheckFailed:     return "cross_check_failed";
        case Reason::Stale:                return "stale";
        case Reason::NotInitialised:       return "not_initialised";
        case Reason::DerivedFromSuspect:   return "derived_from_suspect";
    }
    return "unknown";
}

const char* unitOf(Quantity q) {
    switch (q) {
        case Quantity::AirTemperature:
        case Quantity::WaterTemperature:
        case Quantity::DewPoint:              return "degC";
        case Quantity::RelativeHumidity:      return "percentRH";
        case Quantity::VapourPressureDeficit: return "kPa";
    }
    return "";
}

} // namespace mh
