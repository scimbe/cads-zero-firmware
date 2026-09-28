/*
 * CaDS Zero - crash-loop guard.
 *
 * Every fault and panic ends with the IWDG resetting the board, which is the
 * right answer for a one-off. It is the wrong one when the cause is persisted
 * state that the next boot runs straight into again (the historical
 * `net.dhcp=1` stack overflow was exactly that): boot.autostart re-enters the
 * app tree every 2-3 s, the console never gets a window to change the
 * setting, and after six cycles the forensic ring has evicted the first -
 * root-cause - record.
 *
 * This counts consecutive watchdog resets in CCM (survives a warm reset,
 * like the forensic ring) and lets the boot path skip autostart once the
 * count reaches CADS_BOOTGUARD_LIMIT. Any other reset cause (NRST, power-on,
 * a software reset) starts the count over, so pressing reset is always a
 * clean retry, and a session that stays up long enough calls
 * cads_bootguard_stable() to forgive an earlier, unrelated watchdog reset.
 *
 * No locks, no allocation; call cads_bootguard_boot() exactly once per boot
 * before consulting cads_bootguard_tripped().
 */

#ifndef CADS_DIAG_BOOTGUARD_H
#define CADS_DIAG_BOOTGUARD_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Consecutive watchdog resets after which autostart is skipped. */
#define CADS_BOOTGUARD_LIMIT 3u

/** Uptime after which a session counts as stable (see cads_bootguard_stable). */
#define CADS_BOOTGUARD_STABLE_MS 60000u

/** Record this boot. `watchdog_reset`: the reset cause was the IWDG. */
void cads_bootguard_boot(bool watchdog_reset);

/** Consecutive watchdog resets, including this boot. */
uint32_t cads_bootguard_count(void);

/** True once the count has reached CADS_BOOTGUARD_LIMIT. */
bool cads_bootguard_tripped(void);

/** This boot has run long enough to be trusted: start the count over. */
void cads_bootguard_stable(void);

#ifdef __cplusplus
}
#endif

#endif /* CADS_DIAG_BOOTGUARD_H */
