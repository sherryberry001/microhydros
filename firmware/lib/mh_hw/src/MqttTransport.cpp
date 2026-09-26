#ifdef ARDUINO

#include "mh/hw/MqttTransport.h"
#include "mh/Telemetry.h"

namespace mh {
namespace hw {

namespace {
constexpr uint32_t kBackoffStartMs = 1000;
constexpr uint32_t kBackoffMaxMs   = 60000;
} // namespace

MqttTransport::MqttTransport(const NetConfig& cfg, IClock& clock, ILogger* logger)
    : cfg_(cfg), clock_(clock), log_(logger), mqtt_(wifi_client_),
      next_attempt_ms_(0), backoff_ms_(kBackoffStartMs) {
    mqtt_.setServer(cfg_.mqtt_host, cfg_.mqtt_port);
    // PubSubClient har 256 byte buffert som standard, vilket ar mindre an vara
    // ramar. Utan det har anropet slangs varje telemetriram tyst av klienten och
    // publish() returnerar false, vilket ser ut som ett natverksfel.
    mqtt_.setBufferSize(kMaxFrameLen + 128);
    // Halsokontroll var femtonde sekund. Standardvardet pa 15 s behalls, men
    // sockettimeouten kortas fran 15 till 5 sekunder sa att en dod anslutning
    // upptacks snabbare an en publiceringsperiod.
    mqtt_.setSocketTimeout(5);
}

bool MqttTransport::ensureWifi() {
    if (WiFi.status() == WL_CONNECTED) {
        return true;
    }
    // WiFi.begin ar asynkron. Vi startar den och later loopen fortsatta, istallet
    // for att vanta har inne. En blockerande anslutningsloop ar anledningen till
    // att manga enheter slutar mata sensorerna sa fort natet krånglar.
    if (WiFi.status() != WL_IDLE_STATUS && WiFi.status() != WL_DISCONNECTED) {
        WiFi.mode(WIFI_STA);
    }
    WiFi.begin(cfg_.wifi_ssid, cfg_.wifi_password);
    return WiFi.status() == WL_CONNECTED;
}

bool MqttTransport::connect() {
    if (mqtt_.connected()) {
        return true;
    }

    const uint32_t now = clock_.nowMs();
    // Exponentiell backoff. Att forsoka koppla upp varje varv nar brokern ar nere
    // ater bade batteritid och brokerns anslutningskö, och gor dessutom att
    // sensorloopen kan svalta.
    if (next_attempt_ms_ != 0 && static_cast<int32_t>(now - next_attempt_ms_) < 0) {
        return false;
    }

    if (!ensureWifi()) {
        next_attempt_ms_ = now + backoff_ms_;
        if (backoff_ms_ < kBackoffMaxMs) backoff_ms_ *= 2;
        return false;
    }

    const bool ok = cfg_.mqtt_user
        ? mqtt_.connect(cfg_.client_id, cfg_.mqtt_user, cfg_.mqtt_password,
                        cfg_.status_topic, 1, true, cfg_.lwt_payload)
        : mqtt_.connect(cfg_.client_id, cfg_.status_topic, 1, true, cfg_.lwt_payload);

    if (ok) {
        backoff_ms_ = kBackoffStartMs;
        next_attempt_ms_ = 0;
        if (log_) log_->log("MQTT ansluten");
    } else {
        next_attempt_ms_ = now + backoff_ms_;
        if (backoff_ms_ < kBackoffMaxMs) backoff_ms_ *= 2;
        if (log_) log_->log("MQTT-anslutning misslyckades, forsoker igen senare");
    }
    return ok;
}

bool MqttTransport::isConnected() const {
    // PubSubClient::connected() ar inte const i biblioteket, men den andrar inget
    // observerbart tillstand.
    return const_cast<PubSubClient&>(mqtt_).connected();
}

bool MqttTransport::publish(const char* topic, const char* payload, size_t len) {
    if (!isConnected()) return false;

    // Statusramen publiceras retained sa att en instrumentpanel som just startat
    // far senast kanda tillstand direkt. Telemetrin ar en handelsestrom och ska
    // inte vara retained, annars far varje ny prenumerant ett gammalt matvarde
    // som ser fardskt ut.
    const bool retained = (cfg_.status_topic && strcmp(topic, cfg_.status_topic) == 0);

    return mqtt_.publish(topic, reinterpret_cast<const uint8_t*>(payload),
                         static_cast<unsigned int>(len), retained);
}

void MqttTransport::loop() {
    mqtt_.loop();
}

int8_t MqttTransport::rssi() const {
    if (WiFi.status() != WL_CONNECTED) return 0;
    const long r = WiFi.RSSI();
    if (r < -128) return -128;
    if (r > 127) return 127;
    return static_cast<int8_t>(r);
}

} // namespace hw
} // namespace mh

#endif // ARDUINO
