// Enhetstester for fuktberakningarna.
//
// Referensvardena ar hamtade ur standardtabeller for mattnadsangtryck och
// daggpunkt. Toleranserna ar satta efter vad Magnus-approximationen utlovar,
// alltsa under 0.4 procents fel i vart temperaturintervall.

#include <unity.h>

#include "mh/Psychrometrics.h"

#include <cmath>

using namespace mh::psychro;

void test_mattnadsangtryck_vid_kanda_temperaturer(void) {
    // Tabellvarden: 0.6112 kPa vid 0 C, 2.339 kPa vid 20 C, 4.246 kPa vid 30 C.
    TEST_ASSERT_FLOAT_WITHIN(0.005f, 0.6112f, saturationVapourPressureKPa(0.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.010f, 2.339f, saturationVapourPressureKPa(20.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.020f, 4.246f, saturationVapourPressureKPa(30.0f));
}

void test_daggpunkt_vid_full_mattnad_ar_lufttemperaturen(void) {
    // Vid 100 procent RH ar daggpunkten per definition lika med lufttemperaturen.
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 20.0f, dewPointC(20.0f, 100.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 5.0f, dewPointC(5.0f, 100.0f));
}

void test_daggpunkt_kanda_varden(void) {
    // 20 C och 50 procent RH ger daggpunkt ca 9.3 C.
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 9.3f, dewPointC(20.0f, 50.0f));
    // 25 C och 80 procent RH ger ca 21.3 C.
    TEST_ASSERT_FLOAT_WITHIN(0.2f, 21.3f, dewPointC(25.0f, 80.0f));
}

void test_daggpunkt_ligger_alltid_under_lufttemperaturen(void) {
    // Den har egenskapen ar vad korskontrollen i applikationen vilar pa, sa den
    // ar vard ett eget test over hela arbetsomradet.
    for (float t = 0.0f; t <= 45.0f; t += 1.0f) {
        for (float rh = 5.0f; rh <= 100.0f; rh += 5.0f) {
            const float dp = dewPointC(t, rh);
            TEST_ASSERT_TRUE(std::isfinite(dp));
            TEST_ASSERT_TRUE(dp <= t + 0.001f);
        }
    }
}

void test_ogiltig_fuktighet_ger_nan(void) {
    TEST_ASSERT_TRUE(std::isnan(dewPointC(20.0f, 0.0f)));
    TEST_ASSERT_TRUE(std::isnan(dewPointC(20.0f, -5.0f)));
    TEST_ASSERT_TRUE(std::isnan(dewPointC(20.0f, 150.0f)));
}

void test_vpd_vid_mattnad_ar_noll(void) {
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, vapourPressureDeficitKPa(22.0f, 100.0f));
}

void test_vpd_i_odlingens_arbetsomrade(void) {
    // 22 C och 65 procent RH ar ett typiskt bra lage for sallad och ger ca
    // 0.93 kPa, alltsa mitt i det rekommenderade intervallet 0.8 till 1.2.
    const float vpd = vapourPressureDeficitKPa(22.0f, 65.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.93f, vpd);
    TEST_ASSERT_TRUE(vpd > 0.8f && vpd < 1.2f);
}

void test_vpd_stiger_nar_luften_torkar(void) {
    const float fuktig = vapourPressureDeficitKPa(22.0f, 80.0f);
    const float torr = vapourPressureDeficitKPa(22.0f, 40.0f);
    TEST_ASSERT_TRUE(torr > fuktig);
}

void test_absolut_fuktighet(void) {
    // 20 C och 50 procent RH ger ca 8.6 g/m3.
    TEST_ASSERT_FLOAT_WITHIN(0.3f, 8.6f, absoluteHumidityGm3(20.0f, 50.0f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_mattnadsangtryck_vid_kanda_temperaturer);
    RUN_TEST(test_daggpunkt_vid_full_mattnad_ar_lufttemperaturen);
    RUN_TEST(test_daggpunkt_kanda_varden);
    RUN_TEST(test_daggpunkt_ligger_alltid_under_lufttemperaturen);
    RUN_TEST(test_ogiltig_fuktighet_ger_nan);
    RUN_TEST(test_vpd_vid_mattnad_ar_noll);
    RUN_TEST(test_vpd_i_odlingens_arbetsomrade);
    RUN_TEST(test_vpd_stiger_nar_luften_torkar);
    RUN_TEST(test_absolut_fuktighet);
    return UNITY_END();
}
