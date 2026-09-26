// Enhetstester for trendberakningen.
//
// Kunden ville bli varnad innan temperaturen nar en skadlig niva. De har testerna
// visar att lutningen blir ratt, att brus inte rackar upp falsklarm, och att
// forvarningstiden stammer med den fysik vi matar in.

#include <unity.h>

#include "mh/TrendEstimator.h"

#include <cmath>

using namespace mh;

void test_for_fa_punkter_ger_unknown(void) {
    TrendEstimator<32> t;
    t.add(20.0f, 0);
    t.add(20.1f, 10000);
    const Trend r = t.compute();
    TEST_ASSERT_EQUAL(TrendDirection::Unknown, r.direction);
}

void test_plan_serie_ar_stable(void) {
    TrendEstimator<64> t;
    for (int i = 0; i < 30; ++i) {
        t.add(22.0f, static_cast<uint32_t>(i) * 10000u);
    }
    const Trend r = t.compute();
    TEST_ASSERT_EQUAL(TrendDirection::Stable, r.direction);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, r.slope_per_hour);
}

void test_lutning_raknas_ratt(void) {
    // Bygg en serie som stiger exakt 3.0 C per timme och kontrollera att
    // skattningen traffar det. 10 s mellan punkterna, 60 punkter = 10 minuter.
    TrendEstimator<128> t;
    const float slope_per_hour = 3.0f;
    for (int i = 0; i < 60; ++i) {
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        const float v = 20.0f + slope_per_hour * (static_cast<float>(ms) / 3600000.0f);
        t.add(v, ms);
    }
    const Trend r = t.compute();
    TEST_ASSERT_EQUAL(TrendDirection::Rising, r.direction);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, slope_per_hour, r.slope_per_hour);
    // En perfekt rat linje ska ge r2 mycket nara 1.
    TEST_ASSERT_TRUE(r.r_squared > 0.999f);
}

void test_fallande_serie(void) {
    TrendEstimator<128> t;
    for (int i = 0; i < 60; ++i) {
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        t.add(25.0f - 4.0f * (static_cast<float>(ms) / 3600000.0f), ms);
    }
    const Trend r = t.compute();
    TEST_ASSERT_EQUAL(TrendDirection::Falling, r.direction);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, -4.0f, r.slope_per_hour);
}

void test_forvarningstid_stammer(void) {
    // Stiger 6 C per timme och ar just nu 24 C. Gransen ar 30 C, alltsa 6 C kvar,
    // vilket vid den lutningen ar exakt en timme. Systemet ska saga 60 minuter.
    TrendEstimator<128> t;
    for (int i = 0; i < 60; ++i) {
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        const float hours = static_cast<float>(ms) / 3600000.0f;
        t.add(23.0f + 6.0f * hours, ms);
    }
    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Rising, r.direction);
    // Sista vardet ar 23 + 6 * (590000/3600000) = 23.98 C, alltsa 6.02 C kvar,
    // vilket vid 6 C/h ar ca 60.2 minuter.
    TEST_ASSERT_FLOAT_WITHIN(1.0f, 60.2f, r.minutes_to_limit);
}

void test_ingen_forvarning_nar_vi_ror_oss_bort_fran_gransen(void) {
    TrendEstimator<128> t;
    for (int i = 0; i < 60; ++i) {
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        t.add(25.0f - 3.0f * (static_cast<float>(ms) / 3600000.0f), ms);
    }
    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Falling, r.direction);
    TEST_ASSERT_TRUE(r.minutes_to_limit < 0.0f); // ingen tid att rapportera
}

void test_brus_utan_lutning_ger_inte_falsklarm(void) {
    // Sagtandsbrus pa en halv grad utan nagon underliggande trend. Systemet far
    // inte kalla det stigande, for da larmar det hela tiden i en riktig odling.
    TrendEstimator<128> t;
    for (int i = 0; i < 90; ++i) {
        const float noise = ((i % 5) - 2) * 0.1f;
        t.add(22.0f + noise, static_cast<uint32_t>(i) * 10000u);
    }
    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Stable, r.direction);
}

void test_lag_r2_vid_spridda_punkter(void) {
    // Kraftigt brus runt en svag lutning ska ge lag forklaringsgrad, sa att
    // mottagaren kan valja att inte lita pa trenden.
    TrendEstimator<128> t;
    uint32_t seed = 12345;
    for (int i = 0; i < 90; ++i) {
        seed = seed * 1103515245u + 12345u;
        const float noise = (static_cast<float>((seed >> 16) & 0xFF) / 255.0f - 0.5f) * 8.0f;
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        t.add(22.0f + 1.0f * (static_cast<float>(ms) / 3600000.0f) + noise, ms);
    }
    const Trend r = t.compute();
    TEST_ASSERT_TRUE(r.r_squared < 0.5f);
}

void test_fonstret_glider(void) {
    // Bufferten far bara innehalla de senaste Capacity punkterna. Aldre data ska
    // trangas ut, annars skulle trenden bli langsammare och langsammare med
    // uptiden.
    TrendEstimator<10> t;
    for (int i = 0; i < 100; ++i) {
        t.add(20.0f, static_cast<uint32_t>(i) * 10000u);
    }
    TEST_ASSERT_EQUAL(10, t.size());
}


// --- Jamviktsmodellen: skiljer normal lampstart fran skenande uppvarmning ----
//
// De har tre testerna ar kärnan i kundens huvudkrav. Skillnaden mellan ett larm
// som ar vart nagot och ett som stangs av forsta veckan ligger har.

/// Hjalpare: fyller fonstret med en forsta ordningens invangning fran start mot
/// target med tidskonstanten tau_s, samplat var tionde sekund.
static void fillFirstOrder(TrendEstimator<128>& t, float start, float target,
                           float tau_s, int samples = 90) {
    for (int i = 0; i < samples; ++i) {
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        const float sec = static_cast<float>(ms) / 1000.0f;
        t.add(start + (target - start) * (1.0f - std::exp(-sec / tau_s)), ms);
    }
}

void test_normal_lampstart_ger_ingen_forvarning(void) {
    // Odlingslampan tander. Luften gar fran 21 C mot 27.5 C med tidskonstanten
    // 8 minuter. I borjan lutar det 47 C/h, och den forsta versionen av koden
    // extrapolerade rakt genom 30 C och larmade varje morgon.
    //
    // Jamviktsmodellen ser att forloppet ar pa vag mot 27.5 C, alltsa under
    // gransen, och sager darfor ingenting. Det ar hela poangen.
    TrendEstimator<128> t;
    fillFirstOrder(t, 21.0f, 27.5f, 480.0f);

    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Rising, r.direction);
    TEST_ASSERT_TRUE_MESSAGE(r.has_equilibrium, "modellen skulle ha identifierats");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.0f, 27.5f, r.projected_equilibrium,
        "jamvikten ska skattas till ungefar 27.5 C");
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2.0f, 8.0f, r.tau_hours * 60.0f,
        "tidskonstanten ska skattas till ungefar 8 minuter");
    TEST_ASSERT_TRUE_MESSAGE(r.minutes_to_limit < 0.0f,
        "ingen forvarning far ges nar jamvikten ligger under gransen");
    TEST_ASSERT_FALSE(r.limit_exceeded);
}

void test_skenande_uppvarmning_ger_forvarning(void) {
    // Motprovet, och det verkligt viktiga fallet. Kylningen slutar fungera och
    // luften ar nu pa vag mot 38.5 C. Forloppet bromsar in precis som vid en
    // normal lampstart, sa ett filter som bara tittar pa inbromsning hade tystat
    // aven det har. Jamviktsmodellen ser skillnaden: 38.5 ligger over 30.
    TrendEstimator<128> t;
    fillFirstOrder(t, 24.0f, 38.5f, 480.0f);

    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Rising, r.direction);
    TEST_ASSERT_TRUE(r.has_equilibrium);
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(2.0f, 38.5f, r.projected_equilibrium,
        "jamvikten ska skattas till ungefar 38.5 C");

    // Efter hela fonstret (890 s) ar temperaturen redan
    //   24 + 14.5 * (1 - exp(-890/480)) = 35.7 C
    // alltsa forbi gransen. Da ar det inte langre en forvarning utan ett
    // pagaende problem, och det ska flaggas som sadant.
    TEST_ASSERT_TRUE(r.limit_exceeded);

    // Det intressanta ar att systemet sager till INNAN gransen nas. Vi tittar
    // darfor pa ett tidigare skede av samma forlopp, tre minuter in, da
    // temperaturen fortfarande ar under 30 C.
    TrendEstimator<128> t2;
    fillFirstOrder(t2, 24.0f, 38.5f, 480.0f, 18); // 180 s
    const Trend r2 = t2.compute(30.0f);
    TEST_ASSERT_FALSE_MESSAGE(r2.limit_exceeded, "ska annu vara under gransen");
    TEST_ASSERT_TRUE_MESSAGE(r2.minutes_to_limit > 0.0f,
        "en uppvarmning mot 38.5 C maste ge forvarning innan gransen nas");

    // Analytiskt: 30 C nas nar 1 - exp(-t/480) = 6.0/14.5, alltsa vid t = 256 s.
    // Vid fonstrets slut har 170 s gatt, sa det aterstar ungefar 1.4 minuter.
    TEST_ASSERT_FLOAT_WITHIN_MESSAGE(1.5f, 1.4f, r2.minutes_to_limit,
        "forvarningstiden ska stamma med exponentialen, inte vara godtycklig");
}

void test_linjart_forlopp_faller_tillbaka_pa_extrapolering(void) {
    // Nar ingen jamvikt syns i fonstret finns inget som tyder pa att forloppet
    // planar ut. Da ska systemet varna, och gora det med linjar extrapolering.
    TrendEstimator<128> t;
    for (int i = 0; i < 90; ++i) {
        const uint32_t ms = static_cast<uint32_t>(i) * 10000u;
        const float hours = static_cast<float>(ms) / 3600000.0f;
        t.add(24.0f + 12.0f * hours, ms);
    }
    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Rising, r.direction);
    TEST_ASSERT_FALSE_MESSAGE(r.has_equilibrium,
        "ett rakt linjart forlopp har ingen jamvikt att skatta");
    TEST_ASSERT_TRUE(r.minutes_to_limit > 0.0f);
    // Sista vardet ar 24 + 12 * (890/3600) = 26.97 C, alltsa 3.03 C kvar,
    // vilket vid 12 C/h ar ca 15.2 minuter.
    TEST_ASSERT_FLOAT_WITHIN(1.5f, 15.2f, r.minutes_to_limit);
}

void test_redan_over_gransen_flaggas_separat(void) {
    // Nar temperaturen redan ar over gransen ar det inte en forvarning langre.
    // Mottagaren ska kunna skilja "det kommer bli for varmt" fran "det ar for
    // varmt nu", eftersom det ar tva olika meddelanden till odlaren.
    TrendEstimator<128> t;
    fillFirstOrder(t, 31.0f, 36.0f, 480.0f);

    const Trend r = t.compute(30.0f);
    TEST_ASSERT_TRUE(r.limit_exceeded);
    TEST_ASSERT_TRUE_MESSAGE(r.minutes_to_limit < 0.0f,
        "ingen tid till gransen ska rapporteras nar den redan ar passerad");
}

void test_fallande_temperatur_ger_ingen_ovre_forvarning(void) {
    // Regressionstest for ett fel som upptacktes i integrationskorningen: en
    // temperatur pa vag NER genom 30 C rapporterades som "nar 30 C om 59
    // minuter", trots negativ lutning. Det ar aterhamtningen efter ett problem,
    // inte ett nytt problem.
    TrendEstimator<128> t;
    fillFirstOrder(t, 28.0f, 22.0f, 480.0f);

    const Trend r = t.compute(30.0f);
    TEST_ASSERT_EQUAL(TrendDirection::Falling, r.direction);
    TEST_ASSERT_FALSE(r.limit_exceeded);
    TEST_ASSERT_TRUE_MESSAGE(r.minutes_to_limit < 0.0f,
        "en fallande temperatur far aldrig ge forvarning om en ovre grans");
}

void test_kvoten_ar_ett_nar_det_inte_lutar(void) {
    // Vid en plan serie finns ingen lutning att jamfora, och kvoten ska da vara
    // neutral istallet for att bli ett slumptal av brus delat med brus.
    TrendEstimator<128> t;
    for (int i = 0; i < 60; ++i) {
        t.add(22.0f, static_cast<uint32_t>(i) * 10000u);
    }
    const Trend r = t.compute();
    TEST_ASSERT_EQUAL(TrendDirection::Stable, r.direction);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, r.slope_ratio);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_for_fa_punkter_ger_unknown);
    RUN_TEST(test_plan_serie_ar_stable);
    RUN_TEST(test_lutning_raknas_ratt);
    RUN_TEST(test_fallande_serie);
    RUN_TEST(test_forvarningstid_stammer);
    RUN_TEST(test_ingen_forvarning_nar_vi_ror_oss_bort_fran_gransen);
    RUN_TEST(test_brus_utan_lutning_ger_inte_falsklarm);
    RUN_TEST(test_lag_r2_vid_spridda_punkter);
    RUN_TEST(test_fonstret_glider);
    RUN_TEST(test_normal_lampstart_ger_ingen_forvarning);
    RUN_TEST(test_skenande_uppvarmning_ger_forvarning);
    RUN_TEST(test_linjart_forlopp_faller_tillbaka_pa_extrapolering);
    RUN_TEST(test_redan_over_gransen_flaggas_separat);
    RUN_TEST(test_fallande_temperatur_ger_ingen_ovre_forvarning);
    RUN_TEST(test_kvoten_ar_ett_nar_det_inte_lutar);
    return UNITY_END();
}
