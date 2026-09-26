// MicroHydros - drivrutin for Sensirion SHT4x
//
// SHT4x-familjen ersatter DHT22 i det har projektet. Motiveringen i sin helhet
// ligger i docs/adr/ADR-001-sensorval.md, men kortfattat: samma matstorheter,
// riktig I2C istallet for en tidskritisk enledarprotokoll, CRC pa varje
// overforing, en storleksordning battre noggrannhet och en specificerad
// langtidsdrift.
//
// Tva viktiga detaljer i uppbyggnaden:
//
// 1. En SHT4x levererar bade temperatur och fuktighet i samma matning. Darfor ar
//    klassen delad i en Sht4xDevice som ager I2C-konversationen, och tva tunna
//    ISensor-vyer over den. Om varje kanal hade last kretsen for sig hade vi
//    dubblat antalet matningar, och darmed dubblat kretsens egenuppvarmning, for
//    att fa exakt samma tva tal.
//
// 2. CRC kontrolleras alltid och ett CRC-fel rapporteras som ChecksumFailed
//    istallet for att tigas ihjal. Det ar forsta lagret i valideringskedjan, och
//    det ar det lager som fangar en for lang eller for daligt dragen I2C-kabel.
//
// Adressering: SHT45 och SHT40-AD1B sitter pa 0x44, SHT40-BD1B pa 0x45. Det ar
// darfor de bada luftgivarna kan dela samma buss utan multiplexer.

#pragma once

#ifdef ARDUINO

#include "mh/Interfaces.h"

#include <Arduino.h>
#include <Wire.h>

namespace mh {
namespace hw {

/// Ager I2C-konversationen med en SHT4x-krets.
class Sht4xDevice {
public:
    /// 0x44 for SHT45 och SHT40-AD1B, 0x45 for SHT40-BD1B.
    Sht4xDevice(TwoWire& wire, uint8_t address, IClock& clock);

    /// Mjuk aterstallning och verifiering att kretsen svarar.
    bool begin();

    /// Utfor en matning med hog precision. Resultatet cachas sa att bada
    /// kanalerna kan lasa samma matning.
    bool measure();

    bool     lastOk() const { return last_ok_; }
    Reason   lastError() const { return last_error_; }
    float    temperatureC() const { return temperature_c_; }
    float    humidityPercent() const { return humidity_percent_; }
    uint32_t lastMeasurementMs() const { return last_ms_; }

    /// Ser till att matningen ar fardsk nog, sa att de tva kanalvyerna kan anropa
    /// read() efter varandra utan att kretsen lases tva ganger.
    void ensureFresh(uint32_t max_age_ms = 1000);

    /// Kretsens unika serienummer. Loggas vid uppstart sa att det gar att se i
    /// efterhand om en enhet har fatt en sensor utbytt.
    bool readSerialNumber(uint32_t& out);

private:
    static uint8_t crc8(const uint8_t* data, size_t len);

    TwoWire& wire_;
    uint8_t  address_;
    IClock&  clock_;

    bool     last_ok_;
    Reason   last_error_;
    float    temperature_c_;
    float    humidity_percent_;
    uint32_t last_ms_;
};

/// Temperaturkanalen hos en SHT4x.
class Sht4xTemperature : public ISensor {
public:
    Sht4xTemperature(Sht4xDevice& dev, const char* id, Location loc)
        : dev_(dev), id_(id), location_(loc) {}

    const char* id() const override { return id_; }
    Quantity    quantity() const override { return Quantity::AirTemperature; }
    Location    location() const override { return location_; }
    bool        begin() override { return dev_.begin(); }
    RawReading  read() override;

private:
    Sht4xDevice& dev_;
    const char*  id_;
    Location     location_;
};

/// Fuktkanalen hos en SHT4x.
class Sht4xHumidity : public ISensor {
public:
    Sht4xHumidity(Sht4xDevice& dev, const char* id, Location loc)
        : dev_(dev), id_(id), location_(loc) {}

    const char* id() const override { return id_; }
    Quantity    quantity() const override { return Quantity::RelativeHumidity; }
    Location    location() const override { return location_; }
    bool        begin() override { return dev_.begin(); }
    RawReading  read() override;

private:
    Sht4xDevice& dev_;
    const char*  id_;
    Location     location_;
};

} // namespace hw
} // namespace mh

#endif // ARDUINO
