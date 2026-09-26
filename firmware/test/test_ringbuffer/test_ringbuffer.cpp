// Enhetstester for ringbufferten.
//
// Bufferten anvands bade som trendfonster och som kö for telemetri under
// natavbrott. Den far inte tappa rakningen nar den slar runt, och den maste
// rapportera hur mycket den har trangt undan, annars blir dataforlust osynlig.

#include <unity.h>

#include "mh/RingBuffer.h"

using namespace mh;

void test_tom_fran_borjan(void) {
    RingBuffer<int, 4> b;
    TEST_ASSERT_TRUE(b.empty());
    TEST_ASSERT_EQUAL(0, b.size());
    TEST_ASSERT_EQUAL(4, b.capacity());
    int out = 0;
    TEST_ASSERT_FALSE(b.pop(out));
    TEST_ASSERT_FALSE(b.peek(out));
}

void test_fifo_ordning(void) {
    RingBuffer<int, 4> b;
    b.push(1);
    b.push(2);
    b.push(3);
    TEST_ASSERT_EQUAL(3, b.size());

    int out = 0;
    TEST_ASSERT_TRUE(b.pop(out));
    TEST_ASSERT_EQUAL(1, out);
    TEST_ASSERT_TRUE(b.pop(out));
    TEST_ASSERT_EQUAL(2, out);
    TEST_ASSERT_EQUAL(1, b.size());
}

void test_peek_tar_inte_bort(void) {
    RingBuffer<int, 4> b;
    b.push(7);
    int out = 0;
    TEST_ASSERT_TRUE(b.peek(out));
    TEST_ASSERT_EQUAL(7, out);
    TEST_ASSERT_EQUAL(1, b.size());
}

void test_overskrivning_raknas(void) {
    // Fyll bufferten och skriv over tre gamla element. Rakneverket ska visa
    // exakt tre, sa att telemetrin kan rapportera hur mycket som gick forlorat.
    RingBuffer<int, 4> b;
    for (int i = 1; i <= 7; ++i) b.push(i);

    TEST_ASSERT_TRUE(b.full());
    TEST_ASSERT_EQUAL(4, b.size());
    TEST_ASSERT_EQUAL_UINT32(3, b.overwritten());

    // De fyra som finns kvar ska vara 4, 5, 6, 7 i den ordningen.
    int out = 0;
    b.pop(out); TEST_ASSERT_EQUAL(4, out);
    b.pop(out); TEST_ASSERT_EQUAL(5, out);
    b.pop(out); TEST_ASSERT_EQUAL(6, out);
    b.pop(out); TEST_ASSERT_EQUAL(7, out);
    TEST_ASSERT_TRUE(b.empty());
}

void test_indexering_fran_aldst(void) {
    RingBuffer<int, 4> b;
    for (int i = 1; i <= 6; ++i) b.push(i);
    // Innehallet ar nu 3, 4, 5, 6.
    TEST_ASSERT_EQUAL(3, b.at(0));
    TEST_ASSERT_EQUAL(6, b.at(3));
    TEST_ASSERT_EQUAL(6, b.newest());
}

void test_varvar_manga_ganger(void) {
    // Kör igenom bufferten tusentals ganger och kontrollera att den inte glider
    // ur takt. En av fa buggar som bara visar sig efter lang drifttid.
    RingBuffer<int, 8> b;
    for (int i = 0; i < 10000; ++i) {
        b.push(i);
    }
    TEST_ASSERT_EQUAL(8, b.size());
    TEST_ASSERT_EQUAL(9999, b.newest());
    TEST_ASSERT_EQUAL(9992, b.at(0));
    TEST_ASSERT_EQUAL_UINT32(10000 - 8, b.overwritten());
}

void test_blandad_push_och_pop(void) {
    RingBuffer<int, 4> b;
    int out = 0;
    b.push(1); b.push(2);
    b.pop(out);
    b.push(3); b.push(4); b.push(5);
    TEST_ASSERT_EQUAL(4, b.size());
    TEST_ASSERT_EQUAL_UINT32(0, b.overwritten());
    b.pop(out); TEST_ASSERT_EQUAL(2, out);
    b.pop(out); TEST_ASSERT_EQUAL(3, out);
}

void test_clear(void) {
    RingBuffer<int, 4> b;
    b.push(1); b.push(2);
    b.clear();
    TEST_ASSERT_TRUE(b.empty());
    b.push(9);
    int out = 0;
    TEST_ASSERT_TRUE(b.pop(out));
    TEST_ASSERT_EQUAL(9, out);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tom_fran_borjan);
    RUN_TEST(test_fifo_ordning);
    RUN_TEST(test_peek_tar_inte_bort);
    RUN_TEST(test_overskrivning_raknas);
    RUN_TEST(test_indexering_fran_aldst);
    RUN_TEST(test_varvar_manga_ganger);
    RUN_TEST(test_blandad_push_och_pop);
    RUN_TEST(test_clear);
    return UNITY_END();
}
