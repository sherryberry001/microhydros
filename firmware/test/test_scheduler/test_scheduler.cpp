// Enhetstester for schemalaggaren.
//
// Det viktiga testet har ar overflow-testet. millis() pa en ESP32 slar runt efter
// 49.7 dygn, och ett odlingssystem ska sta langre an sa. En schemalaggare skriven
// med (now >= deadline) slutar fungera helt vid varvet och enheten slutar tyst
// att skicka data. Det ar en klassisk bugg som inte gar att upptacka pa en
// demonstration, bara efter sju veckors drift.

#include <unity.h>

#include "mh/Scheduler.h"

using namespace mh;

void test_uppgift_forfaller_efter_sin_period(void) {
    Scheduler s;
    uint8_t due[kMaxTasks];

    s.addTask(1, 1000, 0);
    TEST_ASSERT_EQUAL(1, s.tick(0, due, kMaxTasks)); // forsta forfaller direkt
    TEST_ASSERT_EQUAL(0, s.tick(500, due, kMaxTasks));
    TEST_ASSERT_EQUAL(1, s.tick(1000, due, kMaxTasks));
    TEST_ASSERT_EQUAL(1, due[0]);
}

void test_flera_uppgifter_med_olika_takt(void) {
    Scheduler s;
    uint8_t due[kMaxTasks];

    s.addTask(1, 1000, 0, 1000);   // var sekund
    s.addTask(2, 5000, 0, 5000);   // var femte sekund

    TEST_ASSERT_EQUAL(0, s.tick(0, due, kMaxTasks));
    TEST_ASSERT_EQUAL(1, s.tick(1000, due, kMaxTasks));

    // Vid 5000 ms ska bada forfalla i samma varv.
    for (uint32_t t = 2000; t < 5000; t += 1000) {
        s.tick(t, due, kMaxTasks);
    }
    const size_t n = s.tick(5000, due, kMaxTasks);
    TEST_ASSERT_EQUAL(2, n);
}

void test_forsta_fordrojning_sprider_ut_starten(void) {
    Scheduler s;
    uint8_t due[kMaxTasks];
    s.addTask(1, 10000, 0, 200);
    TEST_ASSERT_EQUAL(0, s.tick(100, due, kMaxTasks));
    TEST_ASSERT_EQUAL(1, s.tick(200, due, kMaxTasks));
}

void test_missade_perioder_kors_inte_ikapp(void) {
    // Om enheten har varit upptagen i en minut ska den inte skicka sextio
    // telemetriramar pa en gang. Den ska hoppa fram och fortsatta i takt.
    Scheduler s;
    uint8_t due[kMaxTasks];
    s.addTask(1, 1000, 0, 1000);

    // Ett enda tick langt in i framtiden.
    TEST_ASSERT_EQUAL(1, s.tick(60000, due, kMaxTasks));
    // Nasta forfallotid ska ligga en period framat fran nu, inte vid 2000 ms.
    TEST_ASSERT_EQUAL(0, s.tick(60500, due, kMaxTasks));
    TEST_ASSERT_EQUAL(1, s.tick(61000, due, kMaxTasks));
}

void test_overlever_att_millis_slar_runt(void) {
    // Detta ar testet som motiverar att jamforelsen ar skriven som
    // (int32_t)(now - deadline) >= 0 och inte som now >= deadline.
    Scheduler s;
    uint8_t due[kMaxTasks];

    const uint32_t near_wrap = 0xFFFFFF00u; // 256 ms innan varvet
    s.addTask(1, 1000, near_wrap, 1000);    // forfaller vid 0xFFFFF4E4... efter varvet

    TEST_ASSERT_EQUAL(0, s.tick(near_wrap + 500, due, kMaxTasks));

    // Klockan slar runt. now ar nu ett litet tal medan deadline ar ett stort.
    // En naiv jamforelse skulle har svara "inte dags an" for alltid.
    const uint32_t after_wrap = near_wrap + 1000; // slar runt till 0x000002E8
    TEST_ASSERT_TRUE(after_wrap < near_wrap);     // bekrafta att vi faktiskt slog runt
    TEST_ASSERT_EQUAL(1, s.tick(after_wrap, due, kMaxTasks));

    // Och den fortsatter fungera efter varvet.
    TEST_ASSERT_EQUAL(0, s.tick(after_wrap + 500, due, kMaxTasks));
    TEST_ASSERT_EQUAL(1, s.tick(after_wrap + 1000, due, kMaxTasks));
}

void test_avstangd_uppgift_forfaller_inte(void) {
    Scheduler s;
    uint8_t due[kMaxTasks];
    s.addTask(1, 1000, 0, 1000);
    s.setEnabled(1, false);
    TEST_ASSERT_EQUAL(0, s.tick(5000, due, kMaxTasks));
    s.setEnabled(1, true);
    TEST_ASSERT_EQUAL(1, s.tick(5000, due, kMaxTasks));
}

void test_tid_till_nasta_uppgift(void) {
    Scheduler s;
    s.addTask(1, 10000, 0, 10000);
    s.addTask(2, 60000, 0, 60000);
    TEST_ASSERT_EQUAL_UINT32(10000, s.msUntilNext(0));
    TEST_ASSERT_EQUAL_UINT32(4000, s.msUntilNext(6000));
}

void test_full_tabell_avvisar_fler(void) {
    Scheduler s;
    for (size_t i = 0; i < kMaxTasks; ++i) {
        TEST_ASSERT_TRUE(s.addTask(static_cast<uint8_t>(i + 1), 1000, 0));
    }
    TEST_ASSERT_FALSE(s.addTask(99, 1000, 0));
    TEST_ASSERT_EQUAL(kMaxTasks, s.taskCount());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_uppgift_forfaller_efter_sin_period);
    RUN_TEST(test_flera_uppgifter_med_olika_takt);
    RUN_TEST(test_forsta_fordrojning_sprider_ut_starten);
    RUN_TEST(test_missade_perioder_kors_inte_ikapp);
    RUN_TEST(test_overlever_att_millis_slar_runt);
    RUN_TEST(test_avstangd_uppgift_forfaller_inte);
    RUN_TEST(test_tid_till_nasta_uppgift);
    RUN_TEST(test_full_tabell_avvisar_fler);
    return UNITY_END();
}
