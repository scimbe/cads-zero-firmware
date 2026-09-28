/*
 * CaDS Zero - rnlab L08 (TCP-Flusskontrolle): throughput model and the
 * application's read-rate limiter. See l08_tcp_flusskontrolle_logic.h.
 */

#include "l08_tcp_flusskontrolle_logic.h"

uint64_t rnlab_l08_window_limit_bps(uint32_t window_bytes, uint32_t rtt_us) {
    /* TODO(L08): Fenstergrenze W/RTT in bit/s.
     *  - rtt_us == 0: 0 zurueckgeben
     *  - Achtung Ueberlauf: W * 8 * 10^6 passt nicht in 32 Bit -> uint64_t */
    (void)window_bytes;
    (void)rtt_us;
    return 0u;
}

uint64_t rnlab_l08_link_limit_bps(uint32_t link_bps, uint32_t mss) {
    /* TODO(L08): Goodput der Leitung fuer volle Segmente:
     *  link_bps * mss / (mss + RNLAB_L08_WIRE_OVERHEAD); mss == 0 -> 0 */
    (void)link_bps;
    (void)mss;
    return 0u;
}

uint64_t rnlab_l08_predict_bps(
    uint32_t window_bytes, uint32_t rtt_us, uint32_t link_bps, uint32_t mss, uint32_t app_bytes_per_s) {
    /* TODO(L08): Minimum aus Fenstergrenze, Leitungsgrenze und Leserate der
     *  Anwendung (app_bytes_per_s in Byte/s, 0 = keine Grenze).
     *  rtt_us == 0 oder mss == 0 -> 0. */
    (void)window_bytes;
    (void)rtt_us;
    (void)link_bps;
    (void)mss;
    (void)app_bytes_per_s;
    return 0u;
}

void rnlab_l08_limiter_init(rnlab_l08_limiter_t* limiter, uint32_t rate, uint32_t now_ms) {
    if(!limiter) return;
    limiter->rate = rate;
    limiter->last_ms = now_ms;
    limiter->credit_mb = 0u;
}

uint32_t rnlab_l08_limiter_release(rnlab_l08_limiter_t* limiter, uint32_t now_ms, uint32_t pending) {
    /* TODO(L08): Wie viele der 'pending' Byte hat die Anwendung bis now_ms gelesen?
     *  - limiter NULL -> 0; rate 0 -> alles (pending)
     *  - vergangene Zeit: now_ms - last_ms (vorzeichenlos: uebersteht den
     *    Ueberlauf des ms-Zaehlers), dann last_ms = now_ms
     *  - Guthaben in Milli-Byte: credit_mb += rate * vergangene ms, gedeckelt
     *    auf rate * RNLAB_L08_BUCKET_MS, mindestens aber 1000 (ein Byte)
     *  - freigeben: min(ganze Byte im Guthaben, pending); vom Guthaben abziehen
     * Der Stub gibt nie etwas frei: mit gesetzter Leserate bleibt das Fenster
     * dann fuer immer zu. */
    (void)limiter;
    (void)now_ms;
    (void)pending;
    return 0u;
}
