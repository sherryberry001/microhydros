// Enhetstester for JSON-skrivaren och telemetriformatet.
//
// Det ar har kontraktet mot gatewayen testas. En trunkerad ram far aldrig gå ivag,
// och ett underkant matvarde ska bli null och inte noll.

#include <unity.h>

#include "mh/JsonWriter.h"
#include "mh/Telemetry.h"

#include <cstring>
#include <cmath>

using namespace mh;

static bool contains(const char* haystack, const char* needle) {
    return std::strstr(haystack, needle) != nullptr;
}

void test_enkelt_objekt(void) {
    char buf[128];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.kvString("id", "microhydros-01");
    w.kvInt("n", -42);
    w.kvBool("ok", true);
    w.endObject();
    const size_t len = w.finish();

    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_EQUAL_STRING("{\"id\":\"microhydros-01\",\"n\":-42,\"ok\":true}", buf);
}

void test_flyttal_med_decimaler(void) {
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.kvFloat("t", 22.456f, 2);
    w.kvFloat("neg", -3.5f, 1);
    w.endObject();
    w.finish();
    TEST_ASSERT_EQUAL_STRING("{\"t\":22.46,\"neg\":-3.5}", buf);
}

void test_ledande_nollor_i_decimaldelen(void) {
    // Klassisk bugg i handskrivna flyttalsformaterare: 1.05 blir "1.5".
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.kvFloat("a", 1.05f, 2);
    w.kvFloat("b", 0.007f, 3);
    w.endObject();
    w.finish();
    TEST_ASSERT_EQUAL_STRING("{\"a\":1.05,\"b\":0.007}", buf);
}

void test_nan_blir_null(void) {
    // JSON har ingen NaN. Ett underkant matvarde maste bli null, aldrig 0, for
    // annars ser det ut som en riktig matning nere vid noll grader.
    char buf[64];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.kvFloat("v", NAN, 2);
    w.endObject();
    w.finish();
    TEST_ASSERT_EQUAL_STRING("{\"v\":null}", buf);
}

void test_for_liten_buffert_ger_noll(void) {
    // En halv ram ar trasig JSON. Skrivaren maste saga ifran istallet for att
    // skicka nagot som mottagaren inte kan tolka.
    char buf[16];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.kvString("nyckel", "ett ganska langt varde som inte far plats");
    w.endObject();
    TEST_ASSERT_EQUAL(0, w.finish());
    TEST_ASSERT_TRUE(w.truncated());
}

void test_specialtecken_escapas(void) {
    char buf[128];
    JsonWriter w(buf, sizeof(buf));
    w.beginObject();
    w.kvString("s", "rad1\nrad\"2\"");
    w.endObject();
    w.finish();
    TEST_ASSERT_EQUAL_STRING("{\"s\":\"rad1\\nrad\\\"2\\\"\"}", buf);
}

void test_hel_telemetriram(void) {
    TelemetrySnapshot snap;
    snap.device_id = "microhydros-01";
    snap.seq = 7;
    snap.uptime_ms = 420000;
    snap.sample_interval_ms = 10000;

    Measurement m;
    std::strncpy(m.sensor_id, "air_in", kSensorIdLen);
    m.quantity = Quantity::AirTemperature;
    m.location = Location::Inside;
    m.quality = Quality::Good;
    m.reason = Reason::None;
    m.value = 24.125f;
    m.timestamp_ms = 419000;
    snap.measurements[0] = m;

    Measurement bad;
    std::strncpy(bad.sensor_id, "water", kSensorIdLen);
    bad.quantity = Quantity::WaterTemperature;
    bad.location = Location::Water;
    bad.quality = Quality::Bad;
    bad.reason = Reason::ChecksumFailed;
    bad.value = NAN;
    bad.timestamp_ms = 419000;
    snap.measurements[1] = bad;
    snap.measurement_count = 2;

    snap.inside_trend.direction = TrendDirection::Rising;
    snap.inside_trend.slope_per_hour = 2.5f;
    snap.inside_trend.r_squared = 0.94f;
    snap.inside_trend.sample_count = 90;
    snap.inside_trend.minutes_to_limit = 32.5f;
    snap.inside_trend_limit_c = 30.0f;

    char buf[kMaxFrameLen];
    const size_t len = encodeTelemetry(snap, buf, sizeof(buf));

    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(contains(buf, "\"schema_version\":1"));
    TEST_ASSERT_TRUE(contains(buf, "\"device_id\":\"microhydros-01\""));
    TEST_ASSERT_TRUE(contains(buf, "\"seq\":7"));
    TEST_ASSERT_TRUE(contains(buf, "\"sensor_id\":\"air_in\""));
    TEST_ASSERT_TRUE(contains(buf, "\"unit\":\"degC\""));
    TEST_ASSERT_TRUE(contains(buf, "\"value\":24.125"));
    TEST_ASSERT_TRUE(contains(buf, "\"quality\":\"good\""));
    // Det underkanda vardet ska vara null med sin orsak kvar.
    TEST_ASSERT_TRUE(contains(buf, "\"value\":null"));
    TEST_ASSERT_TRUE(contains(buf, "\"reason\":\"checksum_failed\""));
    TEST_ASSERT_TRUE(contains(buf, "\"direction\":\"rising\""));
    TEST_ASSERT_TRUE(contains(buf, "\"minutes_to_limit\":32.5"));
}

void test_full_ram_far_plats_i_bufferten(void) {
    // Det har ar ett budgettest. Om det nagon gang gar sonder betyder det att
    // ramen har vuxit forbi kMaxFrameLen och att kön i sin tur inte langre haller
    // vad den lovar. Fyll pa med maximalt antal matningar och langsta stranger.
    TelemetrySnapshot snap;
    snap.device_id = "microhydros-999";
    snap.seq = 4294967295u;
    snap.uptime_ms = 4294967295u;
    snap.sample_interval_ms = 10000;

    for (size_t i = 0; i < kMaxMeasurements; ++i) {
        Measurement m;
        std::strncpy(m.sensor_id, "abcdefghijk", kSensorIdLen);
        m.sensor_id[kSensorIdLen - 1] = '\0';
        m.quantity = Quantity::VapourPressureDeficit;
        m.location = Location::Derived;
        m.quality = Quality::Suspect;
        m.reason = Reason::RateOfChangeExceeded;
        m.value = -12345.678f;
        m.timestamp_ms = 4294967295u;
        snap.measurements[i] = m;
    }
    snap.measurement_count = kMaxMeasurements;
    snap.inside_trend.direction = TrendDirection::Falling;
    snap.inside_trend.minutes_to_limit = 1234.5f;

    char buf[kMaxFrameLen];
    const size_t len = encodeTelemetry(snap, buf, sizeof(buf));
    TEST_ASSERT_TRUE_MESSAGE(len > 0, "telemetriramen far inte plats i kMaxFrameLen");
}

void test_statusram(void) {
    char buf[192];
    const size_t len = encodeStatus("microhydros-01", false, 123456, "1.0.0", buf, sizeof(buf));
    TEST_ASSERT_TRUE(len > 0);
    TEST_ASSERT_TRUE(contains(buf, "\"state\":\"offline\""));
    TEST_ASSERT_TRUE(contains(buf, "\"firmware\":\"1.0.0\""));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_enkelt_objekt);
    RUN_TEST(test_flyttal_med_decimaler);
    RUN_TEST(test_ledande_nollor_i_decimaldelen);
    RUN_TEST(test_nan_blir_null);
    RUN_TEST(test_for_liten_buffert_ger_noll);
    RUN_TEST(test_specialtecken_escapas);
    RUN_TEST(test_hel_telemetriram);
    RUN_TEST(test_full_ram_far_plats_i_bufferten);
    RUN_TEST(test_statusram);
    return UNITY_END();
}
