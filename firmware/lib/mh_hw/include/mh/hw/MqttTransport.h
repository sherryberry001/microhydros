// MicroHydros - WiFi och MQTT
//
// Kommunikationsvalet ar motiverat i docs/adr/ADR-002-kommunikation.md. Kort:
// kunden har inte valt molnplattform an, sa det viktigaste ar att inte lasa fast
// sig. MQTT ar en oppen publish-subscribe-standard dar enheten bara kanner till
// en broker, och alla stora molnplattformar (AWS IoT Core, Azure IoT Hub, Google
// Cloud IoT, HiveMQ) talar det. Att byta mottagare blir en adressandring istallet
// for en ombyggnad.
//
// Tva detaljer som avgor om det fungerar i falt:
//
// LWT   Last Will and Testament. Brokern far vid uppkopplingen en fardigskriven
//       offline-status som den publicerar om enheten forsvinner utan att saga
//       hejda. Utan det ser en enhet som tappat strommen exakt likadan ut som en
//       enhet som bara ar tyst, och ingen upptacker att odlingen star oovervakad.
//
// Retained  Statusramen publiceras retained. En instrumentpanel som startar far
//       da senast kanda tillstand direkt istallet for att visa tomt tills nasta
//       utskick. Telemetrin publiceras inte retained, den ar en handelsestrom.
//
// Kand begransning: PubSubClient stodjer bara QoS 0 for publicering. Vi
// kompenserar med lopnummer och store-and-forward i applikationen, vilket gor att
// forlorade ramar syns och kan skickas om. Vill man ha aktat QoS 1 ar
// ESP-IDF:s egen MQTT-klient vagen, och det ar en rekommendation infor nasta fas.

#pragma once

#ifdef ARDUINO

#include "mh/Interfaces.h"

#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>

namespace mh {
namespace hw {

struct NetConfig {
    const char* wifi_ssid;
    const char* wifi_password;
    const char* mqtt_host;
    uint16_t    mqtt_port;
    const char* mqtt_user;     ///< nullptr om brokern ar oppen
    const char* mqtt_password;
    const char* client_id;
    const char* status_topic;  ///< dar LWT laggs ut
    const char* lwt_payload;
};

class MqttTransport : public ITransport {
public:
    MqttTransport(const NetConfig& cfg, IClock& clock, ILogger* logger = nullptr);

    bool connect() override;
    bool isConnected() const override;
    bool publish(const char* topic, const char* payload, size_t len) override;
    void loop() override;

    int8_t rssi() const;

private:
    bool ensureWifi();

    NetConfig    cfg_;
    IClock&      clock_;
    ILogger*     log_;
    WiFiClient   wifi_client_;
    PubSubClient mqtt_;

    uint32_t next_attempt_ms_;
    uint32_t backoff_ms_;
};

} // namespace hw
} // namespace mh

#endif // ARDUINO
