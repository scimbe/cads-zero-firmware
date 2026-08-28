/* apps/marauder's join-by-SSID scan-and-match state machine - see
 * apps/marauder/cads_marauder_join.h for the full protocol reasoning. */

#include "unity.h"

#include "cads_marauder_join.h"

void setUp(void) {
}

void tearDown(void) {
}

static void test_start_idle_on_empty_ssid(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_IDLE, st.status);
    cads_marauder_join_start(&st, NULL);
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_IDLE, st.status);
}

static void test_start_is_searching(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "MyNetwork");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_SEARCHING, st.status);
    TEST_ASSERT_EQUAL_UINT32(0u, st.ap_count);
}

/* Real hardware capture shape, 2026-08-27/28: the trailing two hex bytes
 * are Marauder's own beacon-interval fields, always present. */
static void test_finds_first_matching_ap(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "persepolis-XI");
    cads_marauder_join_feed_line(&st, "-76 Ch: 2 fc:34:97:30:ad:21 ESSID: persepolis-XI 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_FOUND, st.status);
    TEST_ASSERT_EQUAL_UINT32(0u, st.found_index);
}

static void test_index_counts_ap_lines_only_not_station_lines(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "Target");
    /* Interleaved AP + station-association lines, matching the exact shape
     * a live "scanall" burst produces - station lines ("N: ap: -> sta:")
     * must NOT advance the AP index. */
    cads_marauder_join_feed_line(&st, "-76 Ch: 2 fc:34:97:30:ad:21 ESSID: FirstNet 11 14");
    cads_marauder_join_feed_line(&st, "1: ap: 7c:10:c9:5a:35:81 -> sta: 01:00:5e:7f:ff:fa");
    cads_marauder_join_feed_line(&st, "-43 Ch: 2 7c:10:c9:5a:35:81 ESSID: SecondNet 11 14");
    cads_marauder_join_feed_line(&st, "2: ap: fc:34:97:30:ad:21 -> sta: 01:00:5e:00:00:fa");
    cads_marauder_join_feed_line(&st, "-78 Ch: 2 fc:34:97:30:ad:11 ESSID: Target 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_FOUND, st.status);
    TEST_ASSERT_EQUAL_UINT32(2u, st.found_index); /* 3rd AP line, 0-based */
}

static void test_prefix_ssid_does_not_false_match(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "Home");
    cads_marauder_join_feed_line(&st, "-50 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: HomeNetwork 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_SEARCHING, st.status); /* not FOUND */
    TEST_ASSERT_EQUAL_UINT32(1u, st.ap_count); /* line still counted as an AP */
}

static void test_no_match_stays_searching(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "NotThere");
    cads_marauder_join_feed_line(&st, "-50 Ch: 6 aa:bb:cc:dd:ee:ff ESSID: SomeOtherNet 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_SEARCHING, st.status);
}

static void test_non_ap_lines_ignored(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "Target");
    cads_marauder_join_feed_line(&st, "#scanall");
    cads_marauder_join_feed_line(&st, "Scanning for APs and Stations. Stop with stopscan");
    cads_marauder_join_feed_line(&st, "> ");
    TEST_ASSERT_EQUAL_UINT32(0u, st.ap_count);
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_SEARCHING, st.status);
}

static void test_feed_after_found_is_noop(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "First");
    cads_marauder_join_feed_line(&st, "-50 Ch: 1 aa:aa:aa:aa:aa:aa ESSID: First 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_FOUND, st.status);
    TEST_ASSERT_EQUAL_UINT32(0u, st.found_index);
    /* A later line, even a real match, must not disturb the first find. */
    cads_marauder_join_feed_line(&st, "-40 Ch: 3 bb:bb:bb:bb:bb:bb ESSID: First 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_FOUND, st.status);
    TEST_ASSERT_EQUAL_UINT32(0u, st.found_index);
}

static void test_timeout_only_while_searching(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "Target");
    cads_marauder_join_check_timeout(&st, 1000u, 500u); /* now past deadline */
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_TIMED_OUT, st.status);

    /* Once FOUND, a timeout check must not override it. */
    cads_marauder_join_start(&st, "Target");
    cads_marauder_join_feed_line(&st, "-50 Ch: 1 aa:aa:aa:aa:aa:aa ESSID: Target 11 14");
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_FOUND, st.status);
    cads_marauder_join_check_timeout(&st, 999999u, 500u);
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_FOUND, st.status);
}

static void test_timeout_not_yet_reached(void) {
    cads_marauder_join_state_t st;
    cads_marauder_join_start(&st, "Target");
    cads_marauder_join_check_timeout(&st, 100u, 500u);
    TEST_ASSERT_EQUAL_INT(CADS_MARAUDER_JOIN_SEARCHING, st.status);
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_start_idle_on_empty_ssid);
    RUN_TEST(test_start_is_searching);
    RUN_TEST(test_finds_first_matching_ap);
    RUN_TEST(test_index_counts_ap_lines_only_not_station_lines);
    RUN_TEST(test_prefix_ssid_does_not_false_match);
    RUN_TEST(test_no_match_stays_searching);
    RUN_TEST(test_non_ap_lines_ignored);
    RUN_TEST(test_feed_after_found_is_noop);
    RUN_TEST(test_timeout_only_while_searching);
    RUN_TEST(test_timeout_not_yet_reached);
    return UNITY_END();
}
