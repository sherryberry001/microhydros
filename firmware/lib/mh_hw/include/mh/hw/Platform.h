// MicroHydros - de sma hardvarudetaljerna
//
// Klocka, logg, seriell utgang och aterstallning av en hangd I2C-buss. Ingen av
// dem ar stora nog att fortjana en egen fil, men de ar alla noga valda.

#pragma once

#ifdef ARDUINO

#include "mh/Interfaces.h"
#include "mh/LineTransport.h"

#include <Arduino.h>
#include <Wire.h>

namespace mh {
namespace hw {

/// millis() bakom IClock-granssnittet.
class ArduinoClock : public IClock {
public:
    uint32_t nowMs() const override { return millis(); }
};

/// Loggar pa USB-seriellen. Loggraderna borjar inte med synkordet MH1, sa
/// gatewayens brygga sorterar bort dem av sig sjalv och man kan lasa loggen och
/// telemetrin i samma terminal.
class SerialLogger : public ILogger {
public:
    void log(const char* line) override {
        Serial.print(F("[mh] "));
        Serial.println(line);
    }
};

/// Seriell utgang for LineTransport.
class SerialSink : public ILineSink {
public:
    bool write(const char* data, size_t len) override {
        return Serial.write(reinterpret_cast<const uint8_t*>(data), len) == len;
    }
    bool endLine() override {
        Serial.println();
        return true;
    }
    bool ready() const override { return static_cast<bool>(Serial); }
};

/// Aterstaller en I2C-buss som har last sig.
///
/// Det har intraffar pa riktigt: om mikrokontrollern startas om mitt i en
/// I2C-lasning har slavkretsen redan borjat klocka ut en byte och fortsatter
/// halla SDA lag i vantan pa klockpulser som aldrig kommer. Bussen ar da dod
/// tills nagon klockar fram den, och en omstart av mikrokontrollern hjalper inte
/// eftersom det ar slaven som hanger.
///
/// Botet ar att tillfalligt anvanda pinnarna som GPIO, skicka upp till nio
/// klockpulser tills slaven slapper SDA, och sedan avsluta med ett stoppvillkor.
/// Nio for att en byte ar atta bitar plus en ACK.
void recoverI2cBus(uint8_t sda_pin, uint8_t scl_pin);

} // namespace hw
} // namespace mh

#endif // ARDUINO
