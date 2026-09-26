// Enhetstester for rimlighetskontrollen.
//
// Det har ar den viktigaste testfilen i projektet, eftersom det var just
// tillforlitligheten over tid kunden var missnojd med i sin forra prototyp. Varje
// test motsvarar ett fel som en riktig givare faktiskt gor.

#include <unity.h>

#include "mh/Validator.h"
#include "mh/Measurement.h"

#include <cmath>

using namespace mh;

// --- L1: transportfel fran drivrutinen ------------------------------------

void test_crc_fel_ger_bad(void) {
    ChannelValidator v(ChannelLimits::airInside());
    const Verdict r = v.evaluate(RawReading::fail(Reason::ChecksumFailed, 1000));
    TEST_ASSERT_EQUAL(Quality::Bad, r.quality);
    TEST_ASSERT_EQUAL(Reason::ChecksumFailed, r.reason);
}

void test_ingen_respons_ger_bad(void) {
    ChannelValidator v(ChannelLimits::airInside());
    const Verdict r = v.evaluate(RawReading::fail(Reason::NoResponse, 1000));
    TEST_ASSERT_EQUAL(Quality::Bad, r.quality);
    TEST_ASSERT_EQUAL(Reason::NoResponse, r.reason);
}

// --- L2 och L3: spann ------------------------------------------------------

void test_normalt_varde_ar_good(void) {
    ChannelValidator v(ChannelLimits::airInside());
    const Verdict r = v.evaluate(RawReading::good(22.5f, 1000));
    TEST_ASSERT_EQUAL(Quality::Good, r.quality);
    TEST_ASSERT_EQUAL(Reason::None, r.reason);
}

void test_utanfor_sensorspann_ger_bad(void) {
    ChannelValidator v(ChannelLimits::airInside());
    // -60 C ligger utanfor SHT45:s -40 C. Da laser vi inte sensorn langre.
    const Verdict r = v.evaluate(RawReading::good(-60.0f, 1000));
    TEST_ASSERT_EQUAL(Quality::Bad, r.quality);
    TEST_ASSERT_EQUAL(Reason::OutOfSensorRange, r.reason);
}

void test_nan_ger_bad(void) {
    ChannelValidator v(ChannelLimits::airInside());
    const Verdict r = v.evaluate(RawReading::good(0.0f / 0.0f, 1000));
    TEST_ASSERT_EQUAL(Quality::Bad, r.quality);
}

void test_orimligt_men_giltigt_ger_suspect(void) {
    ChannelValidator v(ChannelLimits::airInside());
    // 70 C ligger innanfor databladet men ar orimligt for ett odlingsskap.
    // Vardet ska darfor markas Suspect och fortfarande na fram till gatewayen,
    // inte kastas bort.
    const Verdict r = v.evaluate(RawReading::good(70.0f, 1000));
    TEST_ASSERT_EQUAL(Quality::Suspect, r.quality);
    TEST_ASSERT_EQUAL(Reason::OutOfPlausibleRange, r.reason);
}

// --- L4: derivata ----------------------------------------------------------

void test_vatten_hopp_till_85_grader_fangas(void) {
    // Det har ar fallet som faktiskt intraffar i falt: en DS18B20 med glapp i
    // kabeln rapporterar sitt registervarde +85.0 C. Talet ar helt giltigt enligt
    // databladet, det ligger innanfor sensorspannet, och det ser normalt ut. Bara
    // derivatakontrollen kan se att nagra liter vatten omojligt kan bli 60 grader
    // varmare pa tio sekunder.
    ChannelValidator v(ChannelLimits::waterTemperature());
    TEST_ASSERT_EQUAL(Quality::Good, v.evaluate(RawReading::good(21.0f, 0)).quality);

    const Verdict r = v.evaluate(RawReading::good(85.0f, 10000));
    TEST_ASSERT_EQUAL(Quality::Suspect, r.quality);
    TEST_ASSERT_EQUAL(Reason::RateOfChangeExceeded, r.reason);
}

void test_langsam_forandring_slapps_igenom(void) {
    // Motprovet: en langsam och verklig uppvarmning far inte larma.
    // 0.5 C pa en minut ligger under gransen 1.0 C/min for vatten.
    ChannelValidator v(ChannelLimits::waterTemperature());
    TEST_ASSERT_EQUAL(Quality::Good, v.evaluate(RawReading::good(21.0f, 0)).quality);
    const Verdict r = v.evaluate(RawReading::good(21.5f, 60000));
    TEST_ASSERT_EQUAL(Quality::Good, r.quality);
}

void test_spik_flyttar_inte_referensen(void) {
    // En ensam spik far inte gora att nasta korrekta varde ocksa underkands.
    // Referensen ska stanna kvar pa det senast godkanda vardet.
    ChannelValidator v(ChannelLimits::waterTemperature());
    v.evaluate(RawReading::good(21.0f, 0));
    const Verdict spike = v.evaluate(RawReading::good(85.0f, 10000));
    TEST_ASSERT_EQUAL(Quality::Suspect, spike.quality);

    const Verdict back = v.evaluate(RawReading::good(21.1f, 20000));
    TEST_ASSERT_EQUAL(Quality::Good, back.quality);
    TEST_ASSERT_EQUAL_FLOAT(21.1f, v.lastAccepted());
}

void test_sensorbrus_utloser_inte_derivatakontrollen(void) {
    // Regressionstest. I den forsta integrationskorningen underkandes 3.6 procent
    // av alla vattenmatningar i helt felfri drift, eftersom DS18B20:ns eget brus
    // och kvantisering rackte for att sla i derivatagransen vid tio sekunders
    // intervall. Se docs/testrapport.md test I-15.
    //
    // Har simuleras en stillastaende naringslosning med realistiskt brus:
    // standardavvikelse 0.05 C ovanpa kvantiseringssteg om 0.0625 C. Inte en enda
    // avlasning far underkannas.
    ChannelValidator v(ChannelLimits::waterTemperature());

    uint32_t seed = 987654321u;
    uint32_t t = 0;
    int flagged = 0;

    for (int i = 0; i < 2000; ++i) {
        // Enkel deterministisk pseudoslump, summan av tre likformiga ger en
        // grovt klockformad fordelning.
        float noise = 0.0f;
        for (int k = 0; k < 3; ++k) {
            seed = seed * 1664525u + 1013904223u;
            noise += (static_cast<float>((seed >> 16) & 0xFFFF) / 65535.0f) - 0.5f;
        }
        noise *= 0.058f; // ger ca 0.05 C standardavvikelse

        // Kvantisera till DS18B20:ns 12-bitarssteg.
        const float raw = 22.0f + noise;
        const float quantised = std::round(raw / 0.0625f) * 0.0625f;

        const Verdict r = v.evaluate(RawReading::good(quantised, t));
        if (r.reason == Reason::RateOfChangeExceeded) ++flagged;
        t += 10000;
    }

    TEST_ASSERT_EQUAL_MESSAGE(0, flagged,
        "brus fran en frisk givare far aldrig underkannas som for snabb forandring");
}

void test_bruset_doljer_inte_ett_verkligt_hopp(void) {
    // Motprovet. Bruttillagget far inte vara sa generost att den verkliga
    // feltypen slinker igenom. Marginalen ska vara storleksordningar, inte
    // procent.
    ChannelValidator v(ChannelLimits::waterTemperature());
    v.evaluate(RawReading::good(22.0f, 0));

    // 85 C ar over hundra ganger det tillatna steget pa 0.47 C.
    const Verdict r = v.evaluate(RawReading::good(85.0f, 10000));
    TEST_ASSERT_EQUAL(Quality::Suspect, r.quality);
    TEST_ASSERT_EQUAL(Reason::RateOfChangeExceeded, r.reason);

    // Aven ett betydligt mindre men anda orimligt hopp ska fangas.
    ChannelValidator v2(ChannelLimits::waterTemperature());
    v2.evaluate(RawReading::good(22.0f, 0));
    const Verdict r2 = v2.evaluate(RawReading::good(24.0f, 10000)); // 2 C pa 10 s
    TEST_ASSERT_EQUAL(Reason::RateOfChangeExceeded, r2.reason);
}

// --- L5: fastnat varde -----------------------------------------------------

void test_fastnat_varde_fangas_efter_gransen(void) {
    // En I2C-buss som hangt sig levererar samma register om och om igen. Vardet
    // ar rimligt, derivatan ar noll, och ingen annan kontroll reagerar. Efter
    // stuck_limit identiska avlasningar ska kanalen markas.
    ChannelValidator v(ChannelLimits::airInside());
    const uint8_t limit = v.limits().stuck_limit;

    uint32_t t = 0;
    for (uint8_t i = 0; i < limit; ++i) {
        const Verdict r = v.evaluate(RawReading::good(23.0f, t));
        t += 10000;
        if (i < limit - 1) {
            TEST_ASSERT_EQUAL(Quality::Good, r.quality);
        }
    }

    const Verdict stuck = v.evaluate(RawReading::good(23.0f, t));
    TEST_ASSERT_EQUAL(Quality::Suspect, stuck.quality);
    TEST_ASSERT_EQUAL(Reason::StuckValue, stuck.reason);
}

void test_brus_raknas_inte_som_fastnat(void) {
    // Motprovet: en frisk sensor brusar nagra hundradelar och far aldrig
    // klassas som fastnad, hur lange den an star still.
    ChannelValidator v(ChannelLimits::airInside());
    uint32_t t = 0;
    bool any_stuck = false;
    for (int i = 0; i < 100; ++i) {
        const float noise = (i % 2 == 0) ? 0.01f : -0.01f;
        const Verdict r = v.evaluate(RawReading::good(23.0f + noise, t));
        t += 10000;
        if (r.reason == Reason::StuckValue) any_stuck = true;
    }
    TEST_ASSERT_FALSE(any_stuck);
}

// --- Korskontroller --------------------------------------------------------

void test_daggpunkt_over_lufttemp_underkanns(void) {
    TEST_ASSERT_FALSE(crosscheck::dewPointConsistent(20.0f, 23.0f));
    TEST_ASSERT_TRUE(crosscheck::dewPointConsistent(20.0f, 14.0f));
    // Inom matosakerheten ska det tillatas, annars larmar systemet standigt vid
    // nastan mattad luft, vilket ar ett helt normalt tillstand i en odling.
    TEST_ASSERT_TRUE(crosscheck::dewPointConsistent(20.0f, 20.3f));
}

void test_lossnad_vattensond_upptacks(void) {
    TEST_ASSERT_TRUE(crosscheck::waterAirCoupled(24.0f, 22.0f));
    TEST_ASSERT_FALSE(crosscheck::waterAirCoupled(24.0f, 5.0f));
}

void test_forvaxlade_luftgivare_upptacks(void) {
    // Inne ska vara varmare an ute nar lampan lyser. Ar det tvartom med marginal
    // ar givarna sannolikt monterade at fel hall.
    TEST_ASSERT_TRUE(crosscheck::insideOutsideOrdering(26.0f, 21.0f));
    TEST_ASSERT_FALSE(crosscheck::insideOutsideOrdering(15.0f, 26.0f));
}

// --- Aterstallning ---------------------------------------------------------

void test_reset_nollstaller_historiken(void) {
    ChannelValidator v(ChannelLimits::waterTemperature());
    v.evaluate(RawReading::good(21.0f, 0));
    TEST_ASSERT_TRUE(v.hasHistory());
    v.reset();
    TEST_ASSERT_FALSE(v.hasHistory());
    // Efter reset finns ingen referens, sa ett hopp kan inte langre upptackas.
    // Det ar avsiktligt: efter en bussomstart vet vi inte vad som ar sant an.
    const Verdict r = v.evaluate(RawReading::good(40.0f, 10000));
    TEST_ASSERT_EQUAL(Quality::Good, r.quality);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_crc_fel_ger_bad);
    RUN_TEST(test_ingen_respons_ger_bad);
    RUN_TEST(test_normalt_varde_ar_good);
    RUN_TEST(test_utanfor_sensorspann_ger_bad);
    RUN_TEST(test_nan_ger_bad);
    RUN_TEST(test_orimligt_men_giltigt_ger_suspect);
    RUN_TEST(test_vatten_hopp_till_85_grader_fangas);
    RUN_TEST(test_langsam_forandring_slapps_igenom);
    RUN_TEST(test_spik_flyttar_inte_referensen);
    RUN_TEST(test_sensorbrus_utloser_inte_derivatakontrollen);
    RUN_TEST(test_bruset_doljer_inte_ett_verkligt_hopp);
    RUN_TEST(test_fastnat_varde_fangas_efter_gransen);
    RUN_TEST(test_brus_raknas_inte_som_fastnat);
    RUN_TEST(test_daggpunkt_over_lufttemp_underkanns);
    RUN_TEST(test_lossnad_vattensond_upptacks);
    RUN_TEST(test_forvaxlade_luftgivare_upptacks);
    RUN_TEST(test_reset_nollstaller_historiken);
    return UNITY_END();
}
