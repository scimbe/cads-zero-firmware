/*
 * targets/itsboard/hal/hal_eth_aneg.c against a fake MDIO bus: no real PHY,
 * no board, just Registers 4 and 5 scripted per test.
 */

#include "unity.h"

#include "fake_mdio.h"
#include "hal_eth_aneg.h"

#define PHY_ADDR 0u
#define REG_ANAR 4u
#define REG_ANLPAR 5u

void setUp(void) {
    cads_fake_mdio_reset();
}

void tearDown(void) {
}

/* --- decoding and resolution ------------------------------------------------- */

static void test_both_sides_agree_on_100_full_with_pause(void) {
    /* 100BASE-TX FD (0x0100) + 10BASE-T FD (0x0040) + symmetric pause (0x0400)
     * advertised on both sides. */
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0540u);
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x0540u);

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));
    TEST_ASSERT_TRUE(report.completed);

    TEST_ASSERT_TRUE(report.local.full_100);
    TEST_ASSERT_TRUE(report.local.full_10);
    TEST_ASSERT_TRUE(report.local.pause);
    TEST_ASSERT_FALSE(report.local.half_100);
    TEST_ASSERT_FALSE(report.local.pause_asym);

    TEST_ASSERT_TRUE(report.partner.full_100);
    TEST_ASSERT_EQUAL_INT(CadsEthAnegMode100Full, report.resolved);

    TEST_ASSERT_EQUAL_HEX16(0x0540u, report.anar_raw);
    TEST_ASSERT_EQUAL_HEX16(0x0540u, report.anlpar_raw);
}

static void test_resolution_falls_back_to_the_only_shared_mode(void) {
    /* Local offers everything up to 100 full duplex; the partner only ever
     * offers 10 half - the mismatch a "why is this link so slow" ticket is
     * actually asking about. */
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x01E0u); /* 10H,10F,100H,100F */
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x0020u); /* 10H only */

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));

    TEST_ASSERT_TRUE(report.local.full_100);
    TEST_ASSERT_FALSE(report.partner.full_100);
    TEST_ASSERT_FALSE(report.partner.half_100);
    TEST_ASSERT_TRUE(report.partner.half_10);
    TEST_ASSERT_EQUAL_INT(CadsEthAnegMode10Half, report.resolved);
}

static void test_no_shared_capability_resolves_to_none(void) {
    /* Contrived - a real partner always shares at least 10 half - but the
     * decode must not fabricate a mode that was never actually negotiated. */
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0080u); /* 100H only */
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x0040u); /* 10F only */

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));
    TEST_ASSERT_EQUAL_INT(CadsEthAnegModeNone, report.resolved);
}

static void test_100_full_outranks_100_half_even_when_both_are_shared(void) {
    /* Both sides advertise both 100 half and 100 full - the priority order
     * must not stop at the first shared bit it happens to check. */
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0180u);   /* 100H, 100F */
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x0180u); /* 100H, 100F */

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));
    TEST_ASSERT_EQUAL_INT(CadsEthAnegMode100Full, report.resolved);
}

static void test_asymmetric_pause_and_remote_fault_decode_independently(void) {
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0000u);
    /* Asymmetric pause (0x0800) and remote fault (0x2000) on the partner,
     * symmetric pause left off - the two pause bits must not alias. */
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x2800u);

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));

    TEST_ASSERT_TRUE(report.partner.pause_asym);
    TEST_ASSERT_FALSE(report.partner.pause);
    TEST_ASSERT_TRUE(report.partner.remote_fault);
}

static void test_partner_acknowledge_is_read_from_anlpar_bit_14(void) {
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0000u);
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x4000u); /* ACK only */

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));
    TEST_ASSERT_TRUE(report.partner_acknowledged);
}

/* --- MDIO failure handling ---------------------------------------------------- */

static void test_a_silent_bus_reports_incomplete_and_returns_false(void) {
    /* Neither register scripted: cads_hal_eth_mdio_read() returns false, same
     * as no PHY answering. */
    cads_eth_aneg_report_t report;
    TEST_ASSERT_FALSE(cads_hal_eth_aneg_report(PHY_ADDR, &report));
    TEST_ASSERT_FALSE(report.completed);
}

static void test_anlpar_failing_after_anar_succeeds_still_reports_incomplete(void) {
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0080u);
    /* ANLPAR left unset on purpose. */

    cads_eth_aneg_report_t report;
    TEST_ASSERT_FALSE(cads_hal_eth_aneg_report(PHY_ADDR, &report));
    TEST_ASSERT_FALSE(report.completed);
}

static void test_reads_exactly_one_register_four_and_one_register_five(void) {
    cads_fake_mdio_set(PHY_ADDR, REG_ANAR, 0x0000u);
    cads_fake_mdio_set(PHY_ADDR, REG_ANLPAR, 0x0000u);

    cads_eth_aneg_report_t report;
    TEST_ASSERT_TRUE(cads_hal_eth_aneg_report(PHY_ADDR, &report));

    TEST_ASSERT_EQUAL_UINT32(1u, cads_fake_mdio_read_count(PHY_ADDR, REG_ANAR));
    TEST_ASSERT_EQUAL_UINT32(1u, cads_fake_mdio_read_count(PHY_ADDR, REG_ANLPAR));
}

int main(void) {
    UNITY_BEGIN();
    RUN_TEST(test_both_sides_agree_on_100_full_with_pause);
    RUN_TEST(test_resolution_falls_back_to_the_only_shared_mode);
    RUN_TEST(test_no_shared_capability_resolves_to_none);
    RUN_TEST(test_100_full_outranks_100_half_even_when_both_are_shared);
    RUN_TEST(test_asymmetric_pause_and_remote_fault_decode_independently);
    RUN_TEST(test_partner_acknowledge_is_read_from_anlpar_bit_14);
    RUN_TEST(test_a_silent_bus_reports_incomplete_and_returns_false);
    RUN_TEST(test_anlpar_failing_after_anar_succeeds_still_reports_incomplete);
    RUN_TEST(test_reads_exactly_one_register_four_and_one_register_five);
    return UNITY_END();
}
