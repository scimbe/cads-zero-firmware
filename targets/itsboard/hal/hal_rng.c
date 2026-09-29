/*
 * CaDS Zero - hardware RNG driver (RM0090 ch. 24). See cads_hal.h's own
 * "hardware random number generator" section for why this exists and what
 * it guarantees; this file is the register-level implementation of that
 * contract, following RM0090 24.3.1/24.3.2's documented procedure exactly:
 *
 *   1. Enable the clock (RCC_AHB2ENR.RNGEN) and the peripheral (RNG_CR.RNGEN).
 *   2. Per FIPS PUB 140-2 (quoted verbatim in RM0090 24.3.1): "the first
 *      random number generated after setting the RNGEN bit should not be
 *      used, but saved for comparison with the next generated random
 *      number. Each subsequent generated random number has to be compared
 *      with the previously generated number. The test fails if any two
 *      compared numbers are equal." That comparison runs on every word this
 *      driver ever reads, for the life of the peripheral being enabled -
 *      not just once at startup - because a stuck-at fault developing later
 *      is exactly the failure mode the test exists to catch.
 *   3. Check SECS/CECS (current error status) before trusting a word, not
 *      just DRDY - RM0090 24.3.2 is explicit that a word sitting in RNG_DR
 *      while SECS=1 "must not be used because it may not have enough
 *      entropy", even though DRDY might still read 1.
 */
#include "cads_hal.h"

#include "board.h"

/* Bounded spin so a genuine hardware fault reports failure instead of
 * hanging the caller - same convention as hal_clock.c's CADS_CLOCK_TIMEOUT,
 * chosen the same way: generously past any plausible real latency (RNG
 * words arrive every few dozen RNG_CLK cycles per RM0090's own timing
 * figures), not tuned to a measured worst case. */
#define CADS_RNG_WORD_TIMEOUT 0x00080000u

static bool s_rng_ready;
static uint32_t s_rng_previous_word;
static bool s_rng_have_previous;

static void cads_hal_rng_enable(void) {
    RCC->AHB2ENR |= RCC_AHB2ENR_RNGEN;
    (void)RCC->AHB2ENR;
    RNG->CR |= RNG_CR_RNGEN;
    s_rng_have_previous = false; /* re-arms the continuous-test discard rule */
}

/* Reads one fresh, error-checked, continuous-test-passed 32-bit word.
 * Returns false on any hardware fault or timeout - the caller must not use
 * `*word` in that case. */
static bool cads_hal_rng_word(uint32_t* word) {
    if(!s_rng_ready) {
        cads_hal_rng_enable();
        s_rng_ready = true;
    }

    /* One budget for the whole call, not per attempt: every path back to
     * the top of the loop (discarded word, seed/clock error) spends it, so
     * a peripheral that keeps failing ends in `false`, never a hang. */
    uint32_t guard = 0u;
    for(;;) {
        if(++guard > CADS_RNG_WORD_TIMEOUT) return false;
        while((RNG->SR & RNG_SR_DRDY) == 0u) {
            if(RNG->SR & (RNG_SR_SEIS | RNG_SR_CEIS)) {
                /* RM0090 24.3.2: a seed error needs a real reinit (clear
                 * SEIS, then clear+set RNGEN) before the peripheral will
                 * generate again; a clock error just needs SEIS/CEIS
                 * cleared. Reinitializing unconditionally covers both and
                 * is safe either way - it just re-arms the continuous test,
                 * which is exactly what a fresh start after a fault should
                 * do regardless of which error fired. */
                RNG->SR &= ~(RNG_SR_SEIS | RNG_SR_CEIS);
                RNG->CR &= ~RNG_CR_RNGEN;
                cads_hal_rng_enable();
            }
            if(++guard > CADS_RNG_WORD_TIMEOUT) return false;
        }

        /* SECS/CECS: current-status bits, not the latched interrupt-status
         * ones above - RM0090 24.3.2's explicit warning that a word can sit
         * in RNG_DR with DRDY=1 while SECS is *currently* 1 and must still
         * be discarded, not just when the fault first triggered SEIS. */
        if(RNG->SR & (RNG_SR_SECS | RNG_SR_CECS)) {
            /* Discard the word and reinitialise, as for SEIS/CEIS above.
             * A bare `continue` left DRDY set with the fault still
             * standing, so the wait above was skipped and this spun forever
             * on the console task - which the tick-fed IWDG never sees. */
            (void)RNG->DR;
            RNG->SR &= ~(RNG_SR_SEIS | RNG_SR_CEIS);
            RNG->CR &= ~RNG_CR_RNGEN;
            cads_hal_rng_enable();
            continue;
        }

        uint32_t candidate = RNG->DR;

        if(!s_rng_have_previous) {
            /* FIPS 140-2 continuous-test rule: the very first word after
             * (re)enabling is never usable output, only a baseline. */
            s_rng_previous_word = candidate;
            s_rng_have_previous = true;
            continue;
        }
        if(candidate == s_rng_previous_word) {
            /* The test failing IS the fault RM0090 describes it catching -
             * treat it the same as a hardware error, not a one-off retry. */
            return false;
        }
        s_rng_previous_word = candidate;
        *word = candidate;
        return true;
    }
}

bool cads_hal_rng_bytes(uint8_t* out, size_t len) {
    while(len > 0u) {
        uint32_t word;
        if(!cads_hal_rng_word(&word)) return false;

        size_t chunk = len < sizeof(word) ? len : sizeof(word);
        for(size_t i = 0; i < chunk; i++) {
            out[i] = (uint8_t)(word >> (8u * i));
        }
        out += chunk;
        len -= chunk;
    }
    return true;
}
