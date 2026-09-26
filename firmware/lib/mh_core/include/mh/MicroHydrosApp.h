// MicroHydros - applikationen
//
// Det har ar hela beteendet hos enheten, och det innehaller inte en enda rad
// hardvarukod. Den kanner till ISensor, IClock, ITransport och ILogger, inget
// annat. Konsekvensen ar att exakt samma klass kor:
//
//   pa ESP32       mot Sht4xSensor, Ds18b20Sensor och MqttTransport
//   pa en Mac      mot simulerade sensorer och en NDJSON-transport
//   i enhetstest   mot testdubbler dar vi styr tiden och tvingar fram fel
//
// Det ar den uppdelningen som gor att logiken gick att verifiera systematiskt
// utan att en trasig kabel eller en dalig lodning blandar sig i felsokningen.
//
// Livscykel:
//   begin()          initierar sensorer och schemalaggning
//   loop()           anropas sa ofta det gar, blockerar aldrig
//
// Tidtabell:
//   var 10:e sekund  las alla sensorkanaler, validera, mata trendfonstret
//   var 60:e sekund  bygg och skicka telemetriram
//   var 300:e sekund skicka statusram
//   vid behov        tom kön av buffrade ramar

#pragma once

#include "mh/Interfaces.h"
#include "mh/Validator.h"
#include "mh/Scheduler.h"
#include "mh/Telemetry.h"
#include "mh/TrendEstimator.h"

namespace mh {

/// Storlek pa trendfonstret, i antal avlasningar.
///
/// 36 punkter med tio sekunders intervall ger ett sex minuters fonster.
///
/// Valet ar gjort mot skapets termiska tidskonstant, som ligger kring atta
/// minuter. Ett fonster ska vara ungefar lika langt som eller kortare an det
/// forlopp det ska beskriva. Forsta versionen hade femton minuter, och det visade
/// sig vara for langt: nar nagot gick sonder innehöll fonstret fortfarande mest
/// data fran innan felet, modellen gick inte att identifiera, och forvarningen
/// krympte till en minut. Se docs/testrapport.md test I-14.
///
/// Att korta fonstret kostar precision i lutningen, men marginalen ar god. Med
/// SHT45:ns brus pa 0.04 C och 36 punkter over sex minuter blir medelfelet i
/// lutningen ca 0.3 C per timme, vilket ska jamforas med de tiotals grader per
/// timme ett verkligt fel ger. Bruset ar alltsa inte det som begransar.
static constexpr size_t kTrendWindow = 36;

struct AppConfig {
    const char* device_id;
    const char* firmware_version;
    const char* topic_telemetry;
    const char* topic_status;

    uint32_t sample_interval_ms;
    uint32_t publish_interval_ms;
    uint32_t status_interval_ms;

    /// Tröskeln som trendberakningen raknar tid kvar till. 30 C ar den punkt dar
    /// sallad och de flesta ortar borjar fa problem i ett slutet skap.
    float inside_high_limit_c;

    /// Sa manga bussfel i rad innan enheten forsoker aterstalla sensorerna.
    uint16_t bus_error_reinit_threshold;

    AppConfig()
        : device_id("microhydros-01"),
          firmware_version("1.0.0"),
          topic_telemetry("microhydros/v1/microhydros-01/telemetry"),
          topic_status("microhydros/v1/microhydros-01/status"),
          sample_interval_ms(10000),
          publish_interval_ms(60000),
          status_interval_ms(300000),
          inside_high_limit_c(30.0f),
          bus_error_reinit_threshold(5) {}
};

/// Sensorkanaler som applikationen kanner igen. Indexen anvands for
/// korskontrollerna, dar vi behover veta vilken kanal som ar vilken.
enum ChannelIndex : uint8_t {
    kChAirInside = 0,
    kChAirOutside,
    kChWater,
    kChHumidityInside,
    kChannelCount,
};

/// Rapporterade rakneverk, anvands i tester och i statusramen.
struct AppStats {
    uint32_t samples_taken;
    uint32_t samples_good;
    uint32_t samples_suspect;
    uint32_t samples_bad;
    uint32_t frames_built;
    uint32_t frames_sent;
    uint32_t frames_queued;
    uint32_t frames_dropped;
    uint32_t bus_reinits;

    AppStats()
        : samples_taken(0), samples_good(0), samples_suspect(0), samples_bad(0),
          frames_built(0), frames_sent(0), frames_queued(0), frames_dropped(0),
          bus_reinits(0) {}
};

class MicroHydrosApp {
public:
    MicroHydrosApp(const AppConfig& config, IClock& clock, ITransport& transport,
                   ILogger* logger = nullptr);

    /// Kopplar in en sensor pa en kanal. Applikationen ager inte pekaren.
    void attachSensor(ChannelIndex channel, ISensor* sensor);

    bool begin();
    void loop();

    const AppStats& stats() const { return stats_; }
    const Measurement& lastMeasurement(ChannelIndex ch) const { return last_[ch]; }
    const Trend& insideTrend() const { return last_trend_; }
    size_t queuedFrames() const { return outbox_.size(); }

    /// Exponeras for testning av enskilda steg utan att behova vanta pa klockan.
    void sampleAllChannels();
    bool buildAndDispatchFrame();
    void drainOutbox();

private:
    enum TaskId : uint8_t { kTaskSample = 1, kTaskPublish = 2, kTaskStatus = 3 };

    void applyCrossChecks();
    void publishStatus(bool online);
    bool sendOrQueue(const char* topic, const char* payload, size_t len, uint32_t seq);

    AppConfig   cfg_;
    IClock&     clock_;
    ITransport& transport_;
    ILogger*    log_;

    ISensor*         sensors_[kChannelCount];
    ChannelValidator validators_[kChannelCount];
    Measurement      last_[kChannelCount];

    TrendEstimator<kTrendWindow> inside_trend_;
    Trend                        last_trend_;

    Scheduler scheduler_;
    Outbox    outbox_;

    uint32_t seq_;
    uint16_t consecutive_bus_errors_;
    bool     was_connected_;
    AppStats stats_;
};

} // namespace mh
