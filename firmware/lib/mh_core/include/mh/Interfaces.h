// MicroHydros - portarna ut mot omvarlden
//
// Kärnan kanner bara till dessa tre interface. Det ar hela poangen med
// uppdelningen: samma applikationslogik kor mot en riktig SHT45 pa en ESP32 och
// mot en simulerad sensor pa en Mac, utan att en enda rad i logiken andras.
// Det ar ocksa det som gor att kärnan gar att enhetstesta utan hardvara.

#pragma once

#include "mh/Measurement.h"

namespace mh {

/// Monoton klocka i millisekunder. Pa ESP32 ar det millis(), i tester ar det en
/// variabel vi stegar fram sjalva sa att testerna blir deterministiska och
/// snabba.
class IClock {
public:
    virtual ~IClock() = default;
    virtual uint32_t nowMs() const = 0;
};

/// En sensorkanal. En fysisk krets kan exponera flera kanaler, till exempel ger
/// en SHT45 bade lufttemperatur och relativ fuktighet, och da registreras den
/// som tva ISensor.
class ISensor {
public:
    virtual ~ISensor() = default;

    /// Kort, stabilt id. Anvands som nyckel hela vagen ut i databasen.
    virtual const char* id() const = 0;
    virtual Quantity    quantity() const = 0;
    virtual Location    location() const = 0;

    /// Startar upp kretsen. Returnerar false om den inte svarar. Fa anropas om
    /// igen for att forsoka aterstalla en buss som har hangt sig.
    virtual bool begin() = 0;

    /// Las av en gang. Far blockera hogst nagra millisekunder.
    virtual RawReading read() = 0;
};

/// Nagot som kan skicka en fardig telemetriram vidare. MQTT over WiFi pa
/// hardvaran, NDJSON over UART mot en gateway, eller en testdubbel som bara
/// raknar anrop.
class ITransport {
public:
    virtual ~ITransport() = default;

    virtual bool connect() = 0;
    virtual bool isConnected() const = 0;

    /// Skickar en nollterminerad nyttolast. false betyder att ramen inte gick
    /// ivag och att den bor buffras och skickas om.
    virtual bool publish(const char* topic, const char* payload, size_t len) = 0;

    /// Far anropas ofta. Kor eventuellt underliggande natverksloop.
    virtual void loop() {}
};

/// Minimal loggport sa att kärnan kan saga ifran utan att kanna till Serial.
class ILogger {
public:
    virtual ~ILogger() = default;
    virtual void log(const char* line) = 0;
};

} // namespace mh
