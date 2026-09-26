// Integrationstester: hela applikationen mot den simulerade odlingen.
//
// De har testerna kor MicroHydrosApp precis som den kors pa hardvaran, men mot
// GrowBoxModel och simulerade givare, och med en klocka som testet stegar sjalvt.
// Ett dygns drift tar nagra hundradels sekund att simulera, vilket gor att
// beteenden som annars bara visar sig efter lang tid gar att verifiera i varje
// testkorning.
//
// Det ar det har som ersatter "vi lat den sta och gick hem", och det ar samtidigt
// mer systematiskt: felen kommer nar vi vill och gar att reproducera exakt.

#include <unity.h>

#include "mh/MicroHydrosApp.h"
#include "mh/sim/GrowBoxModel.h"
#include "mh/sim/SimSensors.h"

#include <cmath>
#include <cstring>
#include <vector>
#include <string>

using namespace mh;
using namespace mh::sim;

// --- Testdubbel for transporten -------------------------------------------

class FakeTransport : public ITransport {
public:
    bool connect() override {
        connected_ = allow_connect_;
        return connected_;
    }
    bool isConnected() const override { return connected_; }

    bool publish(const char* topic, const char* payload, size_t len) override {
        (void)len;
        if (!connected_) return false;
        if (fail_next_ > 0) {
            --fail_next_;
            return false;
        }
        published.push_back(std::string(payload));
        topics.push_back(std::string(topic));
        return true;
    }

    void setOnline(bool online) {
        allow_connect_ = online;
        if (!online) connected_ = false;
    }
    void failNextPublishes(int n) { fail_next_ = n; }

    std::vector<std::string> published;
    std::vector<std::string> topics;

private:
    bool connected_ = false;
    bool allow_connect_ = true;
    int  fail_next_ = 0;
};

/// Hjalpare som bygger upp en komplett rigg.
struct Rig {
    GrowBoxModel model;
    SimClock     clock;
    Rng          rng{0xC0FFEEu};
    FakeTransport transport;

    SimSht45Temperature air_in{model, clock, rng};
    SimSht40Temperature air_out{model, clock, rng};
    SimDs18b20          water{model, clock, rng};
    SimSht45Humidity    rh_in{model, clock, rng};

    AppConfig cfg;
    MicroHydrosApp app;

    Rig() : app(cfg, clock, transport) {
        app.attachSensor(kChAirInside, &air_in);
        app.attachSensor(kChAirOutside, &air_out);
        app.attachSensor(kChWater, &water);
        app.attachSensor(kChHumidityInside, &rh_in);
    }

    /// Kör riggen ett antal simulerade sekunder. Modellen stegas en sekund i
    /// taget, applikationens loop() anropas i samma takt, precis som pa
    /// hardvaran dar loopen snurrar betydligt oftare an sensorerna lases.
    void run(uint32_t seconds) {
        for (uint32_t i = 0; i < seconds; ++i) {
            model.step(1.0f);
            clock.advance(1000);
            app.loop();
        }
    }
};

static bool contains(const std::string& h, const char* n) {
    return h.find(n) != std::string::npos;
}

// --- Test 1: normaldrift ---------------------------------------------------

void test_normaldrift_ger_fyra_matpunkter_och_ramar(void) {
    Rig r;
    TEST_ASSERT_TRUE(r.app.begin());

    r.run(600); // tio minuter

    // Med 60 sekunders publiceringsintervall ska det ha blivit ungefar tio ramar.
    TEST_ASSERT_TRUE(r.app.stats().frames_built >= 9);
    TEST_ASSERT_TRUE(r.transport.published.size() >= 9);

    // Alla fyra obligatoriska matpunkter ska finnas i ramen, plus de tva
    // harledda.
    const std::string& last = r.transport.published.back();
    TEST_ASSERT_TRUE(contains(last, "\"sensor_id\":\"air_in\""));
    TEST_ASSERT_TRUE(contains(last, "\"sensor_id\":\"air_out\""));
    TEST_ASSERT_TRUE(contains(last, "\"sensor_id\":\"water\""));
    TEST_ASSERT_TRUE(contains(last, "\"sensor_id\":\"rh_in\""));
    TEST_ASSERT_TRUE(contains(last, "\"sensor_id\":\"dew_in\""));
    TEST_ASSERT_TRUE(contains(last, "\"sensor_id\":\"vpd_in\""));
}

void test_normaldrift_ger_inga_underkanda_varden(void) {
    // Motprovet till alla felinjektionstester. Om valideringen larmar pa friska
    // sensorer ar den vardelos, oavsett hur bra den fangar riktiga fel.
    Rig r;
    r.app.begin();
    r.run(3600); // en timme

    const AppStats& s = r.app.stats();
    TEST_ASSERT_TRUE(s.samples_taken > 300);
    TEST_ASSERT_EQUAL_UINT32(0, s.samples_bad);
    // Nagra enstaka Suspect kan forekomma i uppstarten innan lampan och luften
    // har hittat sin jamvikt, men andelen ska vara forsumbar.
    TEST_ASSERT_TRUE(s.samples_suspect * 100 < s.samples_taken);
}

void test_matvardena_ar_fysiskt_rimliga(void) {
    // Rimlighetsresonemanget som testkod. Efter ett dygns drift ska alla kanaler
    // ligga dar fysiken sager att de ska ligga.
    Rig r;
    r.app.begin();
    r.run(86400);

    const float t_in = r.app.lastMeasurement(kChAirInside).value;
    const float t_out = r.app.lastMeasurement(kChAirOutside).value;
    const float t_water = r.app.lastMeasurement(kChWater).value;
    const float rh = r.app.lastMeasurement(kChHumidityInside).value;

    // Inomhusklimat.
    TEST_ASSERT_TRUE(t_out > 15.0f && t_out < 27.0f);
    TEST_ASSERT_TRUE(t_in > 15.0f && t_in < 35.0f);
    // Vattnet ska ligga inom nagra grader fran luften, aldrig langt ifran.
    TEST_ASSERT_TRUE(std::fabs(t_in - t_water) < 8.0f);
    // Relativ fuktighet i ett slutet skap med oppet vatten.
    TEST_ASSERT_TRUE(rh > 20.0f && rh <= 100.0f);
}

void test_vattnet_slapar_efter_luften(void) {
    // Fysikalisk kontroll: nar lampan tander stiger luften snabbt och vattnet
    // langsamt. Om vattnet foljer luften direkt ar antingen modellen eller
    // tolkningen av matvardena fel.
    Rig r;
    r.app.begin();
    r.model.forceLamp(false);
    r.run(7200); // lat allt komma till vila i morker

    const float air_before = r.app.lastMeasurement(kChAirInside).value;
    const float water_before = r.app.lastMeasurement(kChWater).value;

    r.model.forceLamp(true);
    r.run(900); // femton minuter med lampan pa

    const float air_rise = r.app.lastMeasurement(kChAirInside).value - air_before;
    const float water_rise = r.app.lastMeasurement(kChWater).value - water_before;

    TEST_ASSERT_TRUE(air_rise > 2.0f);
    TEST_ASSERT_TRUE(water_rise < air_rise / 2.0f);
}

// --- Test 2: felinjektion --------------------------------------------------

void test_lossnad_vattensond_upptacks_i_drift(void) {
    // DS18B20 som tappar kontakt och rapporterar sitt registervarde 85.0 C.
    Rig r;
    r.app.begin();
    r.run(600);
    TEST_ASSERT_EQUAL(Quality::Good, r.app.lastMeasurement(kChWater).quality);

    r.water.setFault(FaultMode::Ds18b20PowerOnValue, 1.0f);
    r.run(30);

    const Measurement& m = r.app.lastMeasurement(kChWater);
    TEST_ASSERT_NOT_EQUAL(Quality::Good, m.quality);
    // Antingen derivatan eller korskontrollen mot luften ska ha fangat det.
    TEST_ASSERT_TRUE(m.reason == Reason::RateOfChangeExceeded ||
                     m.reason == Reason::CrossCheckFailed ||
                     m.reason == Reason::OutOfPlausibleRange);
}

void test_crc_fel_ger_bad_och_null_i_ramen(void) {
    Rig r;
    r.app.begin();
    r.run(120);

    r.rh_in.setFault(FaultMode::CrcFailure, 1.0f);
    r.run(120);

    TEST_ASSERT_EQUAL(Quality::Bad, r.app.lastMeasurement(kChHumidityInside).quality);
    TEST_ASSERT_EQUAL(Reason::ChecksumFailed, r.app.lastMeasurement(kChHumidityInside).reason);

    const std::string& last = r.transport.published.back();
    TEST_ASSERT_TRUE(contains(last, "\"reason\":\"checksum_failed\""));
    TEST_ASSERT_TRUE(contains(last, "\"value\":null"));
}

void test_sensor_som_slutar_svara_utloser_omstart_av_bussen(void) {
    Rig r;
    r.app.begin();
    r.run(60);

    const uint32_t reinits_before = r.app.stats().bus_reinits;
    r.air_in.setFault(FaultMode::NoResponse, 1.0f);
    // Trosk. ar 5 bussfel i rad, vid 10 s mellan avlasningarna behovs ca 60 s.
    r.run(120);

    TEST_ASSERT_TRUE(r.app.stats().bus_reinits > reinits_before);
}

void test_fastnad_sensor_upptacks(void) {
    Rig r;
    r.app.begin();
    r.run(300);

    r.air_in.setFault(FaultMode::StuckValue, 1.0f);
    // stuck_limit ar 30 avlasningar a 10 s = 300 s, plus marginal.
    r.run(400);

    const Measurement& m = r.app.lastMeasurement(kChAirInside);
    TEST_ASSERT_EQUAL(Quality::Suspect, m.quality);
    TEST_ASSERT_EQUAL(Reason::StuckValue, m.reason);
}

void test_systemet_aterhamtar_sig_nar_felet_forsvinner(void) {
    // Lika viktigt som att upptacka felet: systemet far inte fastna i larmlage.
    Rig r;
    r.app.begin();
    r.run(300);

    r.water.setFault(FaultMode::Ds18b20PowerOnValue, 1.0f);
    r.run(60);
    TEST_ASSERT_NOT_EQUAL(Quality::Good, r.app.lastMeasurement(kChWater).quality);

    r.water.clearFault();
    r.run(300);
    TEST_ASSERT_EQUAL(Quality::Good, r.app.lastMeasurement(kChWater).quality);
}

// --- Test 3: kommunikationsavbrott ----------------------------------------

void test_ramar_buffras_under_natavbrott_och_skickas_sedan(void) {
    // Kunden sa att historiska matdata ar viktiga. Ett avbrott i wifi far darfor
    // inte betyda att timmen ar borta.
    Rig r;
    r.app.begin();
    r.run(120);

    const size_t sent_before = r.transport.published.size();
    TEST_ASSERT_TRUE(sent_before >= 2);

    r.transport.setOnline(false);
    r.run(600); // tio minuter utan nat

    TEST_ASSERT_EQUAL(sent_before, r.transport.published.size()); // inget gick ivag
    TEST_ASSERT_TRUE(r.app.queuedFrames() >= 9);                  // men allt ar kvar

    r.transport.setOnline(true);
    r.run(120);

    TEST_ASSERT_EQUAL(0, r.app.queuedFrames());                   // kön ar tomd
    TEST_ASSERT_TRUE(r.transport.published.size() >= sent_before + 9);
}

void test_lopnummer_ar_monotona_och_utan_hal(void) {
    // Utan lopnummer ar tappad data osynlig. Testet kontrollerar att seq raknas
    // upp med exakt ett per ram, aven over ett natavbrott.
    Rig r;
    r.app.begin();
    r.run(180);
    r.transport.setOnline(false);
    r.run(300);
    r.transport.setOnline(true);
    r.run(180);

    uint32_t expected = 1;
    for (const std::string& frame : r.transport.published) {
        if (!contains(frame, "\"seq\":")) continue;
        if (contains(frame, "\"state\":")) continue; // statusramar har ingen seq
        const size_t pos = frame.find("\"seq\":") + 6;
        const uint32_t seq = static_cast<uint32_t>(std::stoul(frame.substr(pos)));
        TEST_ASSERT_EQUAL_UINT32(expected, seq);
        ++expected;
    }
    TEST_ASSERT_TRUE(expected > 9);
}

void test_kon_tappar_aldst_forst_nar_den_ar_full(void) {
    // Ett langt avbrott far inte fa enheten att sluta fungera. Den ska tappa de
    // aldsta ramarna och rapportera hur manga.
    Rig r;
    r.app.begin();
    r.transport.setOnline(false);
    r.run(60 * (kOutboxDepth + 10)); // langre avbrott an kön racker till

    TEST_ASSERT_EQUAL(kOutboxDepth, r.app.queuedFrames());
    TEST_ASSERT_TRUE(r.app.stats().frames_dropped > 0);

    r.transport.setOnline(true);
    r.run(300);
    TEST_ASSERT_EQUAL(0, r.app.queuedFrames());

    // Den forsta ramen efter aterkomsten ska rapportera att data gick forlorad.
    bool reported = false;
    for (const std::string& f : r.transport.published) {
        if (contains(f, "\"dropped_frames\":") && !contains(f, "\"dropped_frames\":0")) {
            reported = true;
        }
    }
    TEST_ASSERT_TRUE(reported);
}

// --- Test 4: forvarning ----------------------------------------------------

void test_trenden_varnar_innan_gransen_nas(void) {
    // Det har ar kundens huvudkrav omsatt till ett test: systemet ska saga till
    // i god tid innan temperaturen blir skadlig, inte nar den redan ar det.
    Rig r;
    r.app.begin();
    r.model.forceLamp(true);
    r.run(3600); // lat klimatet stabilisera sig med lampan pa

    const float before = r.app.lastMeasurement(kChAirInside).value;
    TEST_ASSERT_TRUE(before < r.cfg.inside_high_limit_c);

    // Simulera att kylningen slutar fungera, till exempel en stannad flakt.
    r.model.injectHeatStep(9.0f);
    r.run(900); // femton minuter

    const Trend t = r.app.insideTrend();
    TEST_ASSERT_EQUAL(TrendDirection::Rising, t.direction);
    TEST_ASSERT_TRUE(t.slope_per_hour > 1.0f);

    // Vi ska ha fatt en forvarning medan temperaturen fortfarande ar under
    // gransen, alltsa ett positivt antal minuter kvar.
    const float now_c = r.app.lastMeasurement(kChAirInside).value;
    if (now_c < r.cfg.inside_high_limit_c) {
        TEST_ASSERT_TRUE(t.minutes_to_limit > 0.0f);
    }
}

void test_ingen_forvarning_vid_normal_fotoperiod(void) {
    // Motprovet. Den dagliga uppvarmningen nar lampan tander far inte generera
    // larm, annars kommer kunden att stanga av notifieringarna forsta veckan.
    Rig r;
    r.app.begin();
    r.model.forceLamp(false);
    r.run(3600);
    r.model.forceLamp(true);
    r.run(3600 * 3); // tre timmar in i ljusperioden, klimatet har satt sig

    const Trend t = r.app.insideTrend();
    const float now_c = r.app.lastMeasurement(kChAirInside).value;
    TEST_ASSERT_TRUE(now_c < r.cfg.inside_high_limit_c);
    // Nar jamvikten ar nadd ska trenden vara lugn igen.
    TEST_ASSERT_EQUAL(TrendDirection::Stable, t.direction);
}

// --- Test 5: langtidsstabilitet -------------------------------------------

void test_sju_dygns_drift_utan_avvikelser(void) {
    // Sju simulerade dygn med en avlasning var tionde sekund ar drygt 60 000
    // avlasningar per kanal. Syftet ar att fanga det som bara syns over tid:
    // rakneverk som svammar over, en kö som laker, en validerare som glider.
    Rig r;
    r.app.begin();
    r.run(7 * 86400);

    const AppStats& s = r.app.stats();
    TEST_ASSERT_TRUE(s.samples_taken > 240000); // 4 kanaler * 7 dygn / 10 s
    TEST_ASSERT_EQUAL_UINT32(0, s.samples_bad);
    TEST_ASSERT_TRUE(s.frames_built > 10000);
    // Allt ska ha gatt ivag, ingenting ska ligga kvar i kön.
    TEST_ASSERT_EQUAL(0, r.app.queuedFrames());
    TEST_ASSERT_EQUAL_UINT32(0, s.frames_dropped);

    // Klimatet ska fortfarande vara rimligt efter en vecka, inte ha glidit ivag.
    const float t_in = r.app.lastMeasurement(kChAirInside).value;
    TEST_ASSERT_TRUE(t_in > 15.0f && t_in < 35.0f);
}

void test_drift_over_millis_varvet(void) {
    // Enheten ska overleva att millis() slar runt efter 49.7 dygn. Vi startar
    // klockan strax fore varvet och kor forbi det.
    Rig r;
    r.clock.setNow(0xFFFFFFFFu - 120000u); // tva minuter kvar till varvet
    r.app.begin();

    const size_t before = r.transport.published.size();
    r.run(300); // fem minuter, alltsa forbi varvet

    // Enheten ska ha fortsatt publicera precis som vanligt.
    TEST_ASSERT_TRUE(r.transport.published.size() >= before + 4);
    TEST_ASSERT_EQUAL(0, r.app.queuedFrames());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_normaldrift_ger_fyra_matpunkter_och_ramar);
    RUN_TEST(test_normaldrift_ger_inga_underkanda_varden);
    RUN_TEST(test_matvardena_ar_fysiskt_rimliga);
    RUN_TEST(test_vattnet_slapar_efter_luften);
    RUN_TEST(test_lossnad_vattensond_upptacks_i_drift);
    RUN_TEST(test_crc_fel_ger_bad_och_null_i_ramen);
    RUN_TEST(test_sensor_som_slutar_svara_utloser_omstart_av_bussen);
    RUN_TEST(test_fastnad_sensor_upptacks);
    RUN_TEST(test_systemet_aterhamtar_sig_nar_felet_forsvinner);
    RUN_TEST(test_ramar_buffras_under_natavbrott_och_skickas_sedan);
    RUN_TEST(test_lopnummer_ar_monotona_och_utan_hal);
    RUN_TEST(test_kon_tappar_aldst_forst_nar_den_ar_full);
    RUN_TEST(test_trenden_varnar_innan_gransen_nas);
    RUN_TEST(test_ingen_forvarning_vid_normal_fotoperiod);
    RUN_TEST(test_sju_dygns_drift_utan_avvikelser);
    RUN_TEST(test_drift_over_millis_varvet);
    return UNITY_END();
}
