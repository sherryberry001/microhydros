// MicroHydros - drivrutin for Maxim DS18B20 i vattentat sond
//
// Vattentemperaturen ar den enda kanal som kraver att givaren sitter nere i
// naringslosningen. Det utesluter i praktiken alla kretsar som inte finns i en
// tat sond, och DS18B20 i rostfritt holje ar den som ar tillganglig, billig och
// beprovad. Motivering i docs/adr/ADR-001-sensorval.md.
//
// Det tekniskt intressanta i den har drivrutinen ar att den aldrig blockerar.
//
// En DS18B20 tar upp till 750 ms pa sig att gora en 12-bitars matning. Den naiva
// drivrutinen gor convert, delay(750), read. Det ar tre kvarts sekund per
// avlasning dar enheten inte kan svara pa natverket, inte kan mata watchdogen och
// inte kan gora nagot annat. Det ar den enskilt vanligaste orsaken till att
// prototyper med 1-Wire kanns tröga och tappar MQTT-anslutningen.
//
// Har startas istallet konverteringen vid en avlasning och resultatet hamtas vid
// nasta, tio sekunder senare. Vardet ar da tio sekunder gammalt. For vatten med
// en termisk tidskonstant pa storleksordningen en och en halv timme ar det helt
// utan betydelse: pa tio sekunder hinner naringslosningen andra sig nagra
// tusendels grader, vilket ar langt under sensorns egen upplosning pa 0.0625 C.
//
// Priset ar att den allra forsta avlasningen efter uppstart inte har nagot varde
// och rapporteras som Stale. Det ar en medveten och dokumenterad avvagning.

#pragma once

#ifdef ARDUINO

#include "mh/Interfaces.h"

#include <Arduino.h>
#include <OneWire.h>

namespace mh {
namespace hw {

class Ds18b20Sensor : public ISensor {
public:
    Ds18b20Sensor(OneWire& bus, IClock& clock, const char* id = "water");

    const char* id() const override { return id_; }
    Quantity    quantity() const override { return Quantity::WaterTemperature; }
    Location    location() const override { return Location::Water; }

    /// Soker upp kretsen pa bussen, verifierar familjekoden och stallar in
    /// 12 bitars upplosning.
    bool begin() override;

    RawReading read() override;

    /// Sondens ROM-kod. Loggas vid uppstart, sa att ett sondbyte gar att se i
    /// efterhand.
    const uint8_t* romCode() const { return rom_; }
    bool           found() const { return found_; }

private:
    bool startConversion();
    bool readScratchpad(int16_t& raw_out);
    bool setResolution12Bit();

    OneWire& bus_;
    IClock&  clock_;
    const char* id_;

    uint8_t  rom_[8];
    bool     found_;
    bool     conversion_pending_;
    uint32_t conversion_started_ms_;
};

} // namespace hw
} // namespace mh

#endif // ARDUINO
