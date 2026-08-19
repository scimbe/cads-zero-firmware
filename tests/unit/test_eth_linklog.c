/*
 * targets/itsboard/hal/hal_eth_linklog.c against a fake MDIO bus and a fake
 * clock: no real PHY, no board, no waiting for an actual cable event.
 */

#include "unity.h"

#include "fake_hal.h"
#include "fake_mdio.h"
#include "hal_eth_linklog.h"

#define PHY_ADDR 0u
#define REG_ISF 29u

#define ISF_LINK_DOWN_IT         0x0010u /* INT4 */
#define ISF_REMOTE_FAULT_IT      0x0020u /* INT5 */
#define ISF_AUTONEGO_COMPLETE_IT 0x0040u /* INT6 */
#define ISF_ENERGYON_IT          0x0080u /* INT7, not a logged event type */

static cads_eth_linklog_t event_log;

void setUp(void) {
    cads_fake_mdio_reset();
    cads_fake_reset();
    cads_eth_linklog_init(&event_log);
}

void tearDown(void) {
}

/* --- the empty and single-event cases ----------------------------------------- */

static void test_a_zero_register_appends_nothing(void) {
    cads_fake_mdio_set(PHY_ADDR, REG_ISF, 0u);

    TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_eth_linklog_count(&event_log));
    TEST_ASSERT_EQUAL_UINT32(0u, event_log.dropped);
}

static void test_a_single_latched_bit_becomes_one_timestamped_entry(void) {
    cads_fake_set_ms(1234u);
    /* A real register still answers 0 once cleared; the baseline models
     * that, and queue_once overrides it for exactly the first read - this
     * poll finds the bit set, the next finds it already clear. */
    cads_fake_mdio_set(PHY_ADDR, REG_ISF, 0u);
    cads_fake_mdio_queue_once(PHY_ADDR, REG_ISF, ISF_LINK_DOWN_IT);

    TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_eth_linklog_count(&event_log));

    const cads_eth_link_event_t* event = cads_eth_linklog_at(&event_log, 0u);
    TEST_ASSERT_NOT_NULL(event);
    TEST_ASSERT_EQUAL_INT(CadsEthLinkEventDown, event->type);
    TEST_ASSERT_EQUAL_UINT32(1234u, event->timestamp_ms);

    /* queue_once already fell back to the zero baseline, so the next poll
     * must find nothing left latched. */
    cads_fake_set_ms(1300u);
    TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_eth_linklog_count(&event_log));
}

static void test_an_irrelevant_bit_logs_nothing(void) {
    /* ENERGYON is a real LAN8742A interrupt source, but not one this module
     * claims to track - it must not silently become a phantom event. */
    cads_fake_mdio_set(PHY_ADDR, REG_ISF, ISF_ENERGYON_IT);

    TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_eth_linklog_count(&event_log));
}

/* --- several events latched between two polls --------------------------------- */

static void test_three_events_latched_at_once_all_appear_link_down_first(void) {
    cads_fake_set_ms(500u);
    cads_fake_mdio_queue_once(
        PHY_ADDR, REG_ISF, (uint16_t)(ISF_LINK_DOWN_IT | ISF_REMOTE_FAULT_IT | ISF_AUTONEGO_COMPLETE_IT));

    TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    TEST_ASSERT_EQUAL_UINT32(3u, cads_eth_linklog_count(&event_log));

    TEST_ASSERT_EQUAL_INT(CadsEthLinkEventDown, cads_eth_linklog_at(&event_log, 0u)->type);
    TEST_ASSERT_EQUAL_INT(CadsEthLinkEventRemoteFault, cads_eth_linklog_at(&event_log, 1u)->type);
    TEST_ASSERT_EQUAL_INT(CadsEthLinkEventAnegComplete, cads_eth_linklog_at(&event_log, 2u)->type);

    /* All three were noticed at the same poll, so they share one timestamp -
     * this module cannot invent finer resolution than the register gives. */
    TEST_ASSERT_EQUAL_UINT32(500u, cads_eth_linklog_at(&event_log, 0u)->timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(500u, cads_eth_linklog_at(&event_log, 2u)->timestamp_ms);
}

/* --- the ring: overflow and ordering ------------------------------------------- */

static void test_the_ring_keeps_the_most_recent_entries_and_counts_drops(void) {
    /* Left permanently set (not queue_once) so every poll below sees the
     * event still latched - this drives the ring past capacity on purpose,
     * it does not claim to model a PHY that keeps re-triggering on its own. */
    cads_fake_mdio_set(PHY_ADDR, REG_ISF, ISF_LINK_DOWN_IT);

    for(uint32_t i = 0u; i < CADS_ETH_LINKLOG_CAPACITY + 5u; i++) {
        cads_fake_set_ms(i);
        TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    }

    TEST_ASSERT_EQUAL_UINT32(CADS_ETH_LINKLOG_CAPACITY, cads_eth_linklog_count(&event_log));
    TEST_ASSERT_EQUAL_UINT32(5u, event_log.dropped);

    /* The oldest surviving entry is from poll #5 (0-indexed), the first five
     * having been evicted. */
    TEST_ASSERT_EQUAL_UINT32(5u, cads_eth_linklog_at(&event_log, 0u)->timestamp_ms);
    TEST_ASSERT_EQUAL_UINT32(
        CADS_ETH_LINKLOG_CAPACITY + 4u, cads_eth_linklog_at(&event_log, CADS_ETH_LINKLOG_CAPACITY - 1u)->timestamp_ms);
}

static void test_index_at_or_past_count_returns_null(void) {
    cads_fake_mdio_queue_once(PHY_ADDR, REG_ISF, ISF_LINK_DOWN_IT);
    TEST_ASSERT_TRUE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));

    TEST_ASSERT_NOT_NULL(cads_eth_linklog_at(&event_log, 0u));
    TEST_ASSERT_NULL(cads_eth_linklog_at(&event_log, 1u));
    TEST_ASSERT_NULL(cads_eth_linklog_at(&event_log, 999u));
}

/* --- MDIO failure -------------------------------------------------------------- */

static void test_a_silent_bus_returns_false_and_logs_nothing(void) {
    /* Register left unset entirely: cads_hal_eth_mdio_read() fails. */
    TEST_ASSERT_FALSE(cads_hal_eth_linklog_poll(PHY_ADDR, &event_log));
    TEST_ASSERT_EQUAL_UINT32(0u, cads_eth_linklog_count(&event_log));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_a_zero_register_appends_nothing);
    RUN_TEST(test_a_single_latched_bit_becomes_one_timestamped_entry);
    RUN_TEST(test_an_irrelevant_bit_logs_nothing);
    RUN_TEST(test_three_events_latched_at_once_all_appear_link_down_first);
    RUN_TEST(test_the_ring_keeps_the_most_recent_entries_and_counts_drops);
    RUN_TEST(test_index_at_or_past_count_returns_null);
    RUN_TEST(test_a_silent_bus_returns_false_and_logs_nothing);
    return UNITY_END();
}
