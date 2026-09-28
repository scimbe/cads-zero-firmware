/*
 * CaDS Zero - rnlab L07 (UDP-Transport): sequence tracking, host-testable.
 * See l07_udp_transport_logic.h for the wire format and the semantics.
 */

#include "l07_udp_transport_logic.h"

bool rnlab_l07_parse_header(const uint8_t* data, size_t len, uint32_t* seq, uint64_t* sent_us) {
    /* TODO(L07): Kopf dekodieren.
     *  - weniger als RNLAB_L07_HEADER_LEN Byte: false, Ausgaben unveraendert
     *  - Byte 0..3 Sequenznummer, Byte 4..11 Zeitstempel, beide big-endian
     *    (Netzbyteordnung) - der Cortex-M4 ist little-endian. Byteweise
     *    zusammensetzen statt den Puffer zu casten (Ausrichtung!).
     *  - seq/sent_us duerfen NULL sein. */
    (void)data;
    (void)len;
    (void)seq;
    (void)sent_us;
    return false;
}

void rnlab_seq_reset(rnlab_seq_tracker_t* tracker) {
    if(!tracker) return;
    *tracker = (rnlab_seq_tracker_t){0};
}

rnlab_seq_event_t rnlab_seq_update(rnlab_seq_tracker_t* tracker, uint32_t seq) {
    /* TODO(L07): Sequenznummer verbuchen (Semantik: l07_udp_transport_logic.h).
     *  1. received zaehlt jeden Aufruf.
     *  2. Erster Aufruf seit reset: first = seq, next = seq + 1, window = 1
     *     -> RNLAB_SEQ_FIRST.
     *  3. ahead = (int32_t)(seq - next) - Serienarithmetik nach RFC 1982,
     *     damit der Ueberlauf 0xFFFFFFFF -> 0 kein Riesensprung wird.
     *     ahead >= 0: 'ahead' Nummern fehlen (lost), Fenster um ahead+1
     *     schieben, Bit 0 setzen, next = seq + 1 -> IN_ORDER bzw. GAP.
     *  4. ahead < 0: age = next - 1 - seq. age >= RNLAB_SEQ_WINDOW -> STALE;
     *     Bit 'age' gesetzt -> DUPLICATE; sonst Bit setzen, reordered++,
     *     lost-- (die Luecke war nur Umsortierung) -> LATE. */
    (void)tracker;
    (void)seq;
    return RNLAB_SEQ_ERROR;
}

uint32_t rnlab_seq_expected(const rnlab_seq_tracker_t* tracker) {
    if(!tracker || !tracker->started) return 0u;
    return tracker->next - tracker->first;
}

uint32_t rnlab_seq_loss_bp(const rnlab_seq_tracker_t* tracker) {
    uint32_t expected = rnlab_seq_expected(tracker);
    if(expected == 0u) return 0u;
    return (uint32_t)(((uint64_t)tracker->lost * 10000u) / expected);
}
