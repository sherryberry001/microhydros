#include "mh/MicroHydrosApp.h"
#include "mh/Psychrometrics.h"

#include <cmath>
#include <cstring>

namespace mh {

namespace {

/// Kopierar ett id sakert in i matningens fasta falt.
void setSensorId(Measurement& m, const char* id) {
    if (!id) {
        m.sensor_id[0] = '\0';
        return;
    }
    size_t i = 0;
    for (; i + 1 < kSensorIdLen && id[i]; ++i) {
        m.sensor_id[i] = id[i];
    }
    m.sensor_id[i] = '\0';
}

} // namespace

MicroHydrosApp::MicroHydrosApp(const AppConfig& config, IClock& clock,
                               ITransport& transport, ILogger* logger)
    : cfg_(config), clock_(clock), transport_(transport), log_(logger),
      seq_(0), consecutive_bus_errors_(0), was_connected_(false) {
    for (size_t i = 0; i < kChannelCount; ++i) {
        sensors_[i] = nullptr;
    }
    validators_[kChAirInside].setLimits(ChannelLimits::airInside());
    validators_[kChAirOutside].setLimits(ChannelLimits::airOutside());
    validators_[kChWater].setLimits(ChannelLimits::waterTemperature());
    validators_[kChHumidityInside].setLimits(ChannelLimits::humidityInside());

    // Lufttemperaturen inne brusar med storleksordningen 0.1 C. En lutning under
    // 0.5 C per timme ar inte skiljbar fran brus och kallas Stable.
    inside_trend_.setStableThreshold(0.5f);
}

void MicroHydrosApp::attachSensor(ChannelIndex channel, ISensor* sensor) {
    if (channel < kChannelCount) {
        sensors_[channel] = sensor;
    }
}

bool MicroHydrosApp::begin() {
    const uint32_t now = clock_.nowMs();
    bool all_ok = true;

    for (size_t i = 0; i < kChannelCount; ++i) {
        if (!sensors_[i]) {
            all_ok = false;
            continue;
        }
        if (!sensors_[i]->begin()) {
            all_ok = false;
            if (log_) log_->log("sensor begin() misslyckades");
        }
    }

    // Uppgifterna far olika forsta forfallotid sa att de inte trangs i samma varv
    // direkt efter boot. Forsta matningen sker nastan omedelbart sa att enheten
    // har nagot att visa, forsta publiceringen efter en hel period sa att den
    // hinner samla ihop nagra matvarden forst.
    scheduler_.addTask(kTaskSample, cfg_.sample_interval_ms, now, 200);
    scheduler_.addTask(kTaskPublish, cfg_.publish_interval_ms, now, cfg_.publish_interval_ms);
    scheduler_.addTask(kTaskStatus, cfg_.status_interval_ms, now, 1000);

    transport_.connect();
    was_connected_ = transport_.isConnected();
    if (was_connected_) {
        publishStatus(true);
    }
    return all_ok;
}

void MicroHydrosApp::sampleAllChannels() {
    const uint32_t now = clock_.nowMs();
    bool any_bus_error = false;

    for (size_t i = 0; i < kChannelCount; ++i) {
        Measurement& m = last_[i];
        if (!sensors_[i]) {
            setSensorId(m, "missing");
            m.quality = Quality::Bad;
            m.reason = Reason::NotInitialised;
            m.value = NAN;
            m.timestamp_ms = now;
            continue;
        }

        const RawReading raw = sensors_[i]->read();
        const Verdict v = validators_[i].evaluate(raw);

        setSensorId(m, sensors_[i]->id());
        m.quantity = sensors_[i]->quantity();
        m.location = sensors_[i]->location();
        m.quality = v.quality;
        m.reason = v.reason;
        m.timestamp_ms = raw.timestamp_ms ? raw.timestamp_ms : now;
        // Ett Bad-varde far inget tal. Att skicka det sista kanda vardet vidare
        // som om det vore fardskt ar precis den sortens tysta fel som gjorde att
        // kundens forra prototyp inte gick att lita pa over tid.
        m.value = (v.quality == Quality::Bad) ? NAN : raw.value;

        ++stats_.samples_taken;
        switch (v.quality) {
            case Quality::Good:    ++stats_.samples_good; break;
            case Quality::Suspect: ++stats_.samples_suspect; break;
            case Quality::Bad:     ++stats_.samples_bad; break;
        }

        if (!raw.ok && (raw.error == Reason::NoResponse || raw.error == Reason::ChecksumFailed)) {
            any_bus_error = true;
        }
    }

    applyCrossChecks();

    // Bara godkanda varden far mata trendfonstret. En spik som slapptes igenom
    // som Suspect skulle annars dra trendlinjen och utlosa en falsk forvarning.
    const Measurement& inside = last_[kChAirInside];
    if (inside.quality == Quality::Good && std::isfinite(inside.value)) {
        inside_trend_.add(inside.value, inside.timestamp_ms);
    }
    last_trend_ = inside_trend_.compute(cfg_.inside_high_limit_c);

    // Bussfel i rad betyder oftast att I2C har last sig, typiskt for att en
    // slavkrets holl ner SDA nar en transaktion avbrots. Att bara fortsatta
    // forsoka hjalper inte, bussen maste vackas. Darfor initieras sensorerna om.
    if (any_bus_error) {
        if (consecutive_bus_errors_ < 0xFFFF) ++consecutive_bus_errors_;
        if (consecutive_bus_errors_ >= cfg_.bus_error_reinit_threshold) {
            if (log_) log_->log("for manga bussfel i rad, initierar om sensorerna");
            for (size_t i = 0; i < kChannelCount; ++i) {
                if (sensors_[i]) sensors_[i]->begin();
                validators_[i].reset();
            }
            consecutive_bus_errors_ = 0;
            ++stats_.bus_reinits;
        }
    } else {
        consecutive_bus_errors_ = 0;
    }
}

void MicroHydrosApp::applyCrossChecks() {
    Measurement& air_in = last_[kChAirInside];
    Measurement& air_out = last_[kChAirOutside];
    Measurement& water = last_[kChWater];
    Measurement& rh_in = last_[kChHumidityInside];

    const bool have_air = air_in.quality != Quality::Bad && std::isfinite(air_in.value);
    const bool have_rh = rh_in.quality != Quality::Bad && std::isfinite(rh_in.value);

    // Daggpunkt over lufttemperatur ar fysiskt omojligt i samma punkt. Vi vet
    // inte vilken av de tva kanalerna som ljuger, sa bada markeras Suspect.
    if (have_air && have_rh) {
        const float dew = psychro::dewPointC(air_in.value, rh_in.value);
        if (std::isfinite(dew) && !crosscheck::dewPointConsistent(air_in.value, dew)) {
            if (air_in.quality == Quality::Good) {
                air_in.quality = Quality::Suspect;
                air_in.reason = Reason::CrossCheckFailed;
            }
            if (rh_in.quality == Quality::Good) {
                rh_in.quality = Quality::Suspect;
                rh_in.reason = Reason::CrossCheckFailed;
            }
        }
    }

    // Vatten som ligger langt fran luften i ett litet slutet skap betyder nastan
    // alltid att sonden har lossnat ur karet.
    if (have_air && water.quality != Quality::Bad && std::isfinite(water.value)) {
        if (!crosscheck::waterAirCoupled(air_in.value, water.value)) {
            if (water.quality == Quality::Good) {
                water.quality = Quality::Suspect;
                water.reason = Reason::CrossCheckFailed;
            }
        }
    }

    // Markant kallare inne an ute tyder pa att de bada luftgivarna har blivit
    // forvaxlade vid montering. Det ar ett monteringsfel som annars ar nastan
    // omojligt att upptacka pa distans, eftersom bada kanalerna ser friska ut.
    if (have_air && air_out.quality != Quality::Bad && std::isfinite(air_out.value)) {
        if (!crosscheck::insideOutsideOrdering(air_in.value, air_out.value)) {
            if (air_in.quality == Quality::Good) {
                air_in.quality = Quality::Suspect;
                air_in.reason = Reason::CrossCheckFailed;
            }
            if (air_out.quality == Quality::Good) {
                air_out.quality = Quality::Suspect;
                air_out.reason = Reason::CrossCheckFailed;
            }
        }
    }
}

bool MicroHydrosApp::buildAndDispatchFrame() {
    TelemetrySnapshot snap;
    snap.device_id = cfg_.device_id;
    snap.seq = ++seq_;
    snap.uptime_ms = clock_.nowMs();
    snap.sample_interval_ms = cfg_.sample_interval_ms;

    size_t n = 0;
    for (size_t i = 0; i < kChannelCount && n < kMaxMeasurements; ++i) {
        snap.measurements[n++] = last_[i];
    }

    // Harledda storheter. De raknas har nere och inte i molnet sa att enheten kan
    // resonera om kondensrisk aven utan uppkoppling, och sa att alla mottagare
    // far samma tal istallet for att var och en implementerar Magnus sjalv.
    const Measurement& air_in = last_[kChAirInside];
    const Measurement& rh_in = last_[kChHumidityInside];
    if (air_in.quality != Quality::Bad && rh_in.quality != Quality::Bad &&
        std::isfinite(air_in.value) && std::isfinite(rh_in.value) &&
        n + 1 < kMaxMeasurements) {

        // Ett harlett varde kan aldrig vara battre an sina indata. Det arver
        // darfor den samsta kvaliteten av de tva, och far en egen orsakskod sa
        // att man ser i efterhand att daggpunkten var tveksam for att
        // lufttemperaturen var det, inte for att berakningen sviktade.
        const bool inputs_good =
            (air_in.quality == Quality::Good && rh_in.quality == Quality::Good);
        const Quality derived_q = inputs_good ? Quality::Good : Quality::Suspect;
        const Reason derived_r = inputs_good ? Reason::None : Reason::DerivedFromSuspect;

        Measurement dew;
        setSensorId(dew, "dew_in");
        dew.quantity = Quantity::DewPoint;
        dew.location = Location::Derived;
        dew.value = psychro::dewPointC(air_in.value, rh_in.value);
        dew.quality = std::isfinite(dew.value) ? derived_q : Quality::Bad;
        dew.reason = std::isfinite(dew.value) ? derived_r : Reason::OutOfPlausibleRange;
        dew.timestamp_ms = air_in.timestamp_ms;
        snap.measurements[n++] = dew;

        Measurement vpd;
        setSensorId(vpd, "vpd_in");
        vpd.quantity = Quantity::VapourPressureDeficit;
        vpd.location = Location::Derived;
        vpd.value = psychro::vapourPressureDeficitKPa(air_in.value, rh_in.value);
        vpd.quality = std::isfinite(vpd.value) ? derived_q : Quality::Bad;
        vpd.reason = std::isfinite(vpd.value) ? derived_r : Reason::OutOfPlausibleRange;
        vpd.timestamp_ms = air_in.timestamp_ms;
        snap.measurements[n++] = vpd;
    }
    snap.measurement_count = n;

    snap.inside_trend = last_trend_;
    snap.inside_trend_limit_c = cfg_.inside_high_limit_c;
    snap.dropped_frames = outbox_.overwritten();
    snap.queued_frames = static_cast<uint16_t>(outbox_.size());
    snap.consecutive_bus_errors = consecutive_bus_errors_;

    char payload[kMaxFrameLen];
    const size_t len = encodeTelemetry(snap, payload, sizeof(payload));
    if (len == 0) {
        // Ramen fick inte plats. Den far inte skickas halv, for da far gatewayen
        // trasig JSON och kan inte skilja det fran ett natverksfel.
        if (log_) log_->log("telemetriramen fick inte plats i bufferten");
        return false;
    }

    ++stats_.frames_built;
    return sendOrQueue(cfg_.topic_telemetry, payload, len, snap.seq);
}

bool MicroHydrosApp::sendOrQueue(const char* topic, const char* payload, size_t len,
                                 uint32_t seq) {
    if (transport_.isConnected() && transport_.publish(topic, payload, len)) {
        ++stats_.frames_sent;
        return true;
    }

    Frame f;
    f.len = static_cast<uint16_t>(len);
    f.seq = seq;
    for (size_t i = 0; i < len && i < kMaxFrameLen - 1; ++i) {
        f.payload[i] = payload[i];
    }
    f.payload[(len < kMaxFrameLen - 1) ? len : kMaxFrameLen - 1] = '\0';

    const uint32_t before = outbox_.overwritten();
    outbox_.push(f);
    ++stats_.frames_queued;
    if (outbox_.overwritten() != before) {
        ++stats_.frames_dropped;
    }
    return false;
}

void MicroHydrosApp::drainOutbox() {
    if (!transport_.isConnected()) return;

    // Skicka hogst nagra ramar per varv. Att tomma en full kö pa en gang kan ta
    // langre tid an watchdogens timeout och gor dessutom att en enhet som just
    // kommit tillbaka efter ett avbrott skickar en skur som kan fa brokern att
    // strypa den.
    const size_t kMaxPerLoop = 4;
    for (size_t i = 0; i < kMaxPerLoop && !outbox_.empty(); ++i) {
        Frame f;
        if (!outbox_.peek(f)) break;
        if (!transport_.publish(cfg_.topic_telemetry, f.payload, f.len)) {
            break; // natet foll igen, behall ramen i kön
        }
        outbox_.pop(f);
        ++stats_.frames_sent;
    }
}

void MicroHydrosApp::publishStatus(bool online) {
    char payload[192];
    const size_t len = encodeStatus(cfg_.device_id, online, clock_.nowMs(),
                                    cfg_.firmware_version, payload, sizeof(payload));
    if (len == 0) return;
    transport_.publish(cfg_.topic_status, payload, len);
}

void MicroHydrosApp::loop() {
    transport_.loop();

    const bool connected = transport_.isConnected();
    if (!connected) {
        transport_.connect();
    } else if (!was_connected_) {
        // Just aterkopplad. Sag att vi lever igen och tom kön.
        if (log_) log_->log("transport ateruppkopplad");
        publishStatus(true);
    }
    was_connected_ = connected;

    const uint32_t now = clock_.nowMs();
    uint8_t due[kMaxTasks];
    const size_t n = scheduler_.tick(now, due, kMaxTasks);

    for (size_t i = 0; i < n; ++i) {
        switch (due[i]) {
            case kTaskSample:  sampleAllChannels(); break;
            case kTaskPublish: buildAndDispatchFrame(); break;
            case kTaskStatus:  publishStatus(true); break;
            default: break;
        }
    }

    drainOutbox();
}

} // namespace mh
