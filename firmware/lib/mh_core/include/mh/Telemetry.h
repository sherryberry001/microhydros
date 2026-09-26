// MicroHydros - telemetrikontraktet
//
// Det har ar granssnittet mellan enheten och allt som kommer efter den. Kunden
// vet annu inte vilken molnplattform de vill ha, sa det viktiga ar att formatet
// overlever ett byte av mottagare. Darav:
//
//   schema_version  Mottagaren kan avgora om den forstar ramen. Utan detta falt
//                   blir varje framtida faltandring en utrullning dar bade enhet
//                   och backend maste bytas samtidigt.
//   seq             Monotont lopnummer. Gor att gatewayen kan se att ram 41
//                   saknas, aven om den aldrig kom fram. Utan lopnummer ar
//                   tappad data osynlig.
//   uptime_ms       Monoton tid. Enheten har ingen batteribackad realtidsklocka,
//                   sa den ljuger hellre an gissar om vagguret. Gatewayen satter
//                   mottagningstid och kan rakna bakat med uptime.
//   quality/reason  Foljer med varje matvarde, se Validator.h.
//
// Ramen ar fysiskt begransad till kMaxFrameLen tecken. Det ar inte bara en
// bekvamlighet, det ar det som gor att kön nedan kan vara statiskt allokerad och
// att MQTT-paketet garanterat far plats i en TCP-segment.

#pragma once

#include "mh/Measurement.h"
#include "mh/TrendEstimator.h"
#include "mh/RingBuffer.h"

namespace mh {

static constexpr uint8_t kSchemaVersion = 1;

/// Storsta telemetriram i tecken, inklusive avslutande nolla.
///
/// Siffran ar inte gissad. Varje matobjekt kostar som mest ca 190 tecken med de
/// langsta strangvarden schemat kan producera ("water_temperature",
/// "rate_of_change_exceeded"), atta matobjekt ger ca 1540, och huvud, trend och
/// halsotal lagger till ca 300. 2048 ger da knappt 15 procents marginal.
///
/// Forsta versionen hade 640 har, satt pa kansla. Enhetstestet
/// test_full_ram_far_plats_i_bufferten fangade det innan nagon hardvara var
/// inblandad: encodeTelemetry returnerade 0, ingen ram byggdes, och enheten hade
/// varit helt tyst i falt utan att nagot loggades som ett fel. Det ar precis den
/// sortens tysta fel kunden beskrev fran sin forra leverantor.
static constexpr size_t kMaxFrameLen = 2048;

static constexpr size_t kMaxMeasurements = 8;

/// Djup pa kön for obeskickade ramar.
///
/// Minnesbudget: 2048 * 16 = 32 kB statiskt allokerat, av ESP32:ns 320 kB DRAM.
/// Vid 60 sekunders publiceringsintervall overbryggar det 16 minuters avbrott,
/// vilket racker med god marginal for att en router ska hinna starta om. Kön ar
/// avsiktligt inte storre: den ska overbrygga ett avbrott, inte ersatta en
/// databas, och varje ram till ar 2 kB mindre till WiFi-stacken.
static constexpr size_t kOutboxDepth = 16;

/// En fardig, seriealiserad ram redo att skickas. Ligger i kön nar natet ar nere.
struct Frame {
    char     payload[kMaxFrameLen];
    uint16_t len;
    uint32_t seq;

    Frame() : payload{0}, len(0), seq(0) {}
};

/// Allt som ska med i en telemetriram.
struct TelemetrySnapshot {
    const char* device_id;
    uint32_t    seq;
    uint32_t    uptime_ms;
    uint32_t    sample_interval_ms;

    Measurement measurements[kMaxMeasurements];
    size_t      measurement_count;

    /// Trenden for lufttemperaturen inne, den kanal kunden faktiskt vill bli
    /// varnad om. Ovriga kanaler trendas i gatewayen dar det finns plats.
    Trend    inside_trend;
    float    inside_trend_limit_c;

    /// Halsotal. Ett fjarrplacerat system som inte rapporterar om sig sjalvt gar
    /// inte att felsoka utan att aka dit.
    uint32_t free_heap_bytes;
    uint32_t dropped_frames;
    uint16_t queued_frames;
    uint16_t consecutive_bus_errors;
    int8_t   wifi_rssi_dbm;

    TelemetrySnapshot()
        : device_id("unknown"), seq(0), uptime_ms(0), sample_interval_ms(0),
          measurement_count(0), inside_trend_limit_c(0.0f), free_heap_bytes(0),
          dropped_frames(0), queued_frames(0), consecutive_bus_errors(0),
          wifi_rssi_dbm(0) {}
};

/// Serialiserar ett snapshot till JSON. Returnerar antal tecken, 0 vid overflow.
size_t encodeTelemetry(const TelemetrySnapshot& snap, char* out, size_t capacity);

/// Bygger status-ramen som publiceras retained pa uppkoppling, och som LWT
/// lagger ut nar enheten forsvinner utan att saga hejda.
size_t encodeStatus(const char* device_id, bool online, uint32_t uptime_ms,
                    const char* firmware_version, char* out, size_t capacity);

/// Kö som haller ramar nar transporten ar nere.
///
/// Kunden sa att historiska matdata kommer vara viktiga. Ett system som tappar en
/// timmes matvarden varje gang wifi:t hickar ar darfor inte bra nog, aven om
/// varje enskild matning ar korrekt. Kön ar liten och avsiktligt sa: den ska
/// overbrygga en router som startar om, inte ersatta en databas.
using Outbox = RingBuffer<Frame, kOutboxDepth>;

} // namespace mh
