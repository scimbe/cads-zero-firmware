/* CaDS Zero - Marauder join-by-SSID implementation. See the header for the
 * full protocol reasoning. */
#include "cads_marauder_join.h"
#include "cads_marauder_reader.h"

#include <string.h>

void cads_marauder_join_start(cads_marauder_join_state_t* st, const char* ssid) {
    memset(st, 0, sizeof(*st));
    if(ssid == NULL || ssid[0] == '\0') {
        st->status = CADS_MARAUDER_JOIN_IDLE;
        return;
    }
    size_t n = strlen(ssid);
    if(n >= CADS_MARAUDER_JOIN_SSID_MAX) n = CADS_MARAUDER_JOIN_SSID_MAX - 1u;
    memcpy(st->target_ssid, ssid, n);
    st->target_ssid[n] = '\0';
    st->status = CADS_MARAUDER_JOIN_SEARCHING;
}

/* Find " Ch: " and "ESSID: " in `line`; both present is the AP-line tell (no
 * other Marauder CLI output produces this exact pair). Returns a pointer
 * just past "ESSID: " (the start of the essid text) or NULL if this is not
 * an AP-format line. */
static const char* cads_marauder_ap_line_essid_start(const char* line) {
    if(strstr(line, " Ch: ") == NULL) return NULL;
    const char* tag = strstr(line, "ESSID: ");
    if(tag == NULL) return NULL;
    return tag + 7; /* strlen("ESSID: ") */
}

/* True if `essid_text` (from cads_marauder_ap_line_essid_start) is exactly
 * `target` at a clean boundary: target's full length, then end-of-string or
 * a space (the space before Marauder's own trailing beacon-interval hex
 * bytes) - not merely a prefix match, so "Home" cannot match "HomeNetwork". */
static bool cads_marauder_essid_matches(const char* line, const char* essid_text, const char* target) {
    size_t tlen = strlen(target);
    if(strncmp(essid_text, target, tlen) != 0) return false;
    char next = essid_text[tlen];
    if(next == ' ') return true;
    if(next != '\0') return false;
    /* End-of-string only counts as a boundary if the reader did not cut
     * the line there: a line that filled CADS_MARAUDER_LINE_LEN was
     * force-split, so "ESSID: HomeNetwork1" may really be the first half
     * of "HomeNetwork1-Guest". Treat that as not found (the documented
     * degradation), never as a match on the wrong AP. */
    return strlen(line) < CADS_MARAUDER_LINE_LEN - 1u;
}

void cads_marauder_join_feed_line(cads_marauder_join_state_t* st, const char* line) {
    if(st->status != CADS_MARAUDER_JOIN_SEARCHING) return;

    const char* essid_text = cads_marauder_ap_line_essid_start(line);
    if(essid_text == NULL) return;

    uint32_t this_index = st->ap_count;
    st->ap_count++;

    if(cads_marauder_essid_matches(line, essid_text, st->target_ssid)) {
        st->status = CADS_MARAUDER_JOIN_FOUND;
        st->found_index = this_index;
    }
}

void cads_marauder_join_check_timeout(cads_marauder_join_state_t* st, uint32_t now_ms, uint32_t deadline_ms) {
    if(st->status != CADS_MARAUDER_JOIN_SEARCHING) return;
    if((int32_t)(now_ms - deadline_ms) >= 0) {
        st->status = CADS_MARAUDER_JOIN_TIMED_OUT;
    }
}
