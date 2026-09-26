// MicroHydros - radbaserad transport
//
// Uppgiften tillater uttryckligen "seriell kommunikation till en gateway" som
// kommunikationslosning. Den har transporten ar den vagen, och den fyller tva
// syften samtidigt:
//
//   1. Pa hardvaran ar den en reservvag och en felsokningsvag. Allt som skickas
//      over MQTT speglas pa UART, sa man kan folja enheten med en seriell
//      terminal utan att rora natverket.
//   2. Pa utvecklingsdatorn ar den den enda vagen. Simuleringsnoden skriver
//      samma ramar pa stdout, en liten brygga i gatewayen laser dem och
//      publicerar dem pa MQTT. Applikationskoden vet inte om skillnaden, och
//      darfor testar den kedjan verkligen samma kod som kor pa kortet.
//
// Ramformat, en rad per ram:
//
//     MH1 <topic> <json>\n
//
// Synkordet MH1 finns for att en UART-lank aldrig ar ren. En ESP32 spottar ur sig
// bootloaderns meddelanden i 74880 baud innan applikationen tar over, och en
// mottagare som forutsatter att varje rad ar giltig JSON snubblar pa det direkt.
// Med synkord kan bryggan kasta allt som inte borjar ratt och fortsatta.

#pragma once

#include "mh/Interfaces.h"

namespace mh {

/// Nagot som kan skriva tecken. Serial pa ESP32, stdout i simuleringen.
///
/// Granssnittet ar strombaserat och inte radbaserat, sa att en 2 kB-ram aldrig
/// behover byggas ihop i en lokal buffert innan den skickas. Det halls medvetet
/// sa: stackutrymmet i Arduino-loopens task ar 8 kB, och att lagga en fjardedel
/// av det pa en telemetriram ar den sortens marginal som forst syns nar enheten
/// borjar starta om av sig sjalv efter nagra veckor.
class ILineSink {
public:
    virtual ~ILineSink() = default;
    virtual bool write(const char* data, size_t len) = 0;
    virtual bool endLine() = 0;
    virtual bool ready() const { return true; }
};

/// Transport som ramar in telemetrin som rader.
class LineTransport : public ITransport {
public:
    explicit LineTransport(ILineSink& sink) : sink_(sink), connected_(false) {}

    bool connect() override {
        connected_ = sink_.ready();
        return connected_;
    }
    bool isConnected() const override { return connected_ && sink_.ready(); }

    bool publish(const char* topic, const char* payload, size_t len) override;

private:
    ILineSink& sink_;
    bool       connected_;
};

/// Transport som skickar till tva stallen och lyckas om den primara lyckas.
///
/// Anvands pa hardvaran: MQTT ar den primara vagen och avgor om ramen raknas som
/// levererad, UART ar en spegel for felsokning. Spegeln far aldrig fa en ram att
/// se levererad ut nar natet ar nere, for da slutar store-and-forward fungera och
/// data gar forlorad utan att nagon marker det.
class TeeTransport : public ITransport {
public:
    TeeTransport(ITransport& primary, ITransport& mirror)
        : primary_(primary), mirror_(mirror) {}

    bool connect() override {
        mirror_.connect();
        return primary_.connect();
    }
    bool isConnected() const override { return primary_.isConnected(); }
    void loop() override {
        primary_.loop();
        mirror_.loop();
    }

    bool publish(const char* topic, const char* payload, size_t len) override {
        mirror_.publish(topic, payload, len);
        return primary_.publish(topic, payload, len);
    }

private:
    ITransport& primary_;
    ITransport& mirror_;
};

} // namespace mh
