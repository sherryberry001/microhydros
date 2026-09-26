#include "mh/Telemetry.h"
#include "mh/JsonWriter.h"

namespace mh {

const char* toString(TrendDirection d) {
    switch (d) {
        case TrendDirection::Unknown: return "unknown";
        case TrendDirection::Stable:  return "stable";
        case TrendDirection::Rising:  return "rising";
        case TrendDirection::Falling: return "falling";
    }
    return "unknown";
}

size_t encodeTelemetry(const TelemetrySnapshot& snap, char* out, size_t capacity) {
    JsonWriter w(out, capacity);

    w.beginObject();
    w.kvUInt("schema_version", kSchemaVersion);
    w.kvString("device_id", snap.device_id);
    w.kvUInt("seq", snap.seq);
    w.kvUInt("uptime_ms", snap.uptime_ms);
    w.kvUInt("sample_interval_ms", snap.sample_interval_ms);

    w.beginArray("measurements");
    for (size_t i = 0; i < snap.measurement_count && i < kMaxMeasurements; ++i) {
        const Measurement& m = snap.measurements[i];
        w.beginObject();
        w.kvString("sensor_id", m.sensor_id);
        w.kvString("quantity", toString(m.quantity));
        w.kvString("location", toString(m.location));
        w.kvString("unit", unitOf(m.quantity));
        // Tre decimaler racker for alla vara storheter och ar en storleksordning
        // finare an sensorernas upplosning, sa ingenting gar forlorat.
        w.kvFloat("value", m.value, 3);
        w.kvString("quality", toString(m.quality));
        w.kvString("reason", toString(m.reason));
        w.kvUInt("t_ms", m.timestamp_ms);
        w.endObject();
    }
    w.endArray();

    w.key("trend");
    w.beginObject();
    w.kvString("channel", "air_in");
    w.kvString("direction", toString(snap.inside_trend.direction));
    w.kvFloat("slope_per_hour", snap.inside_trend.slope_per_hour, 3);
    w.kvFloat("r_squared", snap.inside_trend.r_squared, 3);
    w.kvFloat("slope_ratio", snap.inside_trend.slope_ratio, 3);
    w.kvBool("limit_exceeded", snap.inside_trend.limit_exceeded);
    if (snap.inside_trend.has_equilibrium) {
        w.kvFloat("projected_equilibrium_c", snap.inside_trend.projected_equilibrium, 2);
        w.kvFloat("tau_minutes", snap.inside_trend.tau_hours * 60.0f, 1);
    } else {
        w.key("projected_equilibrium_c");
        w.valueNull();
        w.key("tau_minutes");
        w.valueNull();
    }
    w.kvUInt("samples", snap.inside_trend.sample_count);
    w.kvFloat("limit_c", snap.inside_trend_limit_c, 1);
    if (snap.inside_trend.minutes_to_limit >= 0.0f) {
        w.kvFloat("minutes_to_limit", snap.inside_trend.minutes_to_limit, 1);
    } else {
        w.key("minutes_to_limit");
        w.valueNull();
    }
    w.endObject();

    w.key("health");
    w.beginObject();
    w.kvUInt("free_heap_bytes", snap.free_heap_bytes);
    w.kvUInt("dropped_frames", snap.dropped_frames);
    w.kvUInt("queued_frames", snap.queued_frames);
    w.kvUInt("bus_errors", snap.consecutive_bus_errors);
    w.kvInt("wifi_rssi_dbm", snap.wifi_rssi_dbm);
    w.endObject();

    w.endObject();
    return w.finish();
}

size_t encodeStatus(const char* device_id, bool online, uint32_t uptime_ms,
                    const char* firmware_version, char* out, size_t capacity) {
    JsonWriter w(out, capacity);
    w.beginObject();
    w.kvUInt("schema_version", kSchemaVersion);
    w.kvString("device_id", device_id);
    w.kvString("state", online ? "online" : "offline");
    w.kvString("firmware", firmware_version);
    w.kvUInt("uptime_ms", uptime_ms);
    w.endObject();
    return w.finish();
}

} // namespace mh
