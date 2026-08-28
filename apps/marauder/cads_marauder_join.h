/*
 * CaDS Zero - Marauder join-by-SSID: scan-and-match state machine.
 *
 * Marauder's `join -a <index> -p <password>` indexes into the ACCESS_POINTS
 * list a real scan populated (confirmed against the pinned ESP32Marauder
 * commit's CommandLine.cpp - `join -a` reads access_points->get(index)),
 * and there is no CLI command to set the ClientSSID/ClientPW string
 * settings `join -s` reads instead (`settings -s` only ever calls
 * saveSetting<bool>()). So joining a configured network by name means:
 * start a scan, watch each AP-format line Marauder prints as it discovers
 * networks, count AP lines in arrival order (== access_points list order,
 * since scan population only ever appends - WiFiScan.cpp's
 * `access_points->add(ap)`), and the moment a line's ESSID matches the
 * target, that count IS the list index to join with.
 *
 * Line format (WiFiScan.cpp, the promiscuous beacon-frame handler):
 *   "<rssi> Ch: <channel> <bssid> ESSID: <essid> <hex><hex>"
 * e.g. "-76 Ch: 2 fc:34:97:30:ad:21 ESSID: persepolis-XI 11 14" - the
 * trailing two space-separated 2-hex-digit tokens are the AP's beacon
 * interval bytes (sprintf("%02X", ap.beacon[i])), always exactly two,
 * always right after the ESSID. This module does not need to parse them
 * out precisely, only decide whether the text right after "ESSID: " is the
 * target SSID at a clean boundary (see cads_marauder_join_feed_line's own
 * comment on the boundary rule) - it never needs the RSSI/channel/BSSID
 * fields either, only whether a line counts as "one more AP" at all.
 *
 * Deliberately independent of the display ring (cads_marauder_reader.h) so
 * it can watch every line the scan produces, not just the last
 * CADS_MARAUDER_OUT_LINES kept for the panel - wired via that reader's
 * line_cb hook, not by re-reading the UART itself (there is only one
 * reader; see cads_marauder.c).
 */
#ifndef CADS_MARAUDER_JOIN_H
#define CADS_MARAUDER_JOIN_H

#include <stdbool.h>
#include <stdint.h>

#define CADS_MARAUDER_JOIN_SSID_MAX 33u /* matches CADS_CONFIG_SSID_MAX */

typedef enum {
    CADS_MARAUDER_JOIN_IDLE = 0,
    CADS_MARAUDER_JOIN_SEARCHING,
    CADS_MARAUDER_JOIN_FOUND,
    CADS_MARAUDER_JOIN_TIMED_OUT,
} cads_marauder_join_status_t;

typedef struct {
    char target_ssid[CADS_MARAUDER_JOIN_SSID_MAX];
    cads_marauder_join_status_t status;
    uint32_t ap_count;      /* AP-format lines seen so far this search      */
    uint32_t found_index;   /* valid only when status == FOUND              */
} cads_marauder_join_state_t;

/** Begin a search for `ssid` (copied in, truncated to SSID_MAX-1 if
 *  longer). Resets ap_count to 0 and status to SEARCHING. A NULL or empty
 *  `ssid` leaves the state IDLE instead. */
void cads_marauder_join_start(cads_marauder_join_state_t* st, const char* ssid);

/**
 * Feed one completed output line. If it looks like an AP-format line
 * ("<rssi> Ch: <n> <bssid> ESSID: ..." - checked by the presence of both
 * " Ch: " and "ESSID: ", which no other Marauder CLI output line produces),
 * ap_count is incremented (this line's index is ap_count-1 - the value
 * BEFORE incrementing is what identifies THIS line, so found_index is set
 * to the pre-increment count). The ESSID is matched against target_ssid at
 * a boundary: the text right after "ESSID: " must equal target_ssid for
 * its exact length, followed by a space or end of line - a plain substring
 * search would wrongly match "Home" inside "HomeNetwork". No-op once status
 * is no longer SEARCHING (a prior FOUND/TIMED_OUT sticks until the next
 * cads_marauder_join_start()).
 */
void cads_marauder_join_feed_line(cads_marauder_join_state_t* st, const char* line);

/** Call periodically while status == SEARCHING; moves to TIMED_OUT once
 *  `deadline_ms` (an absolute cads_hal_ticks_ms() value, set by the caller
 *  at search start) has passed. No-op in any other status. */
void cads_marauder_join_check_timeout(cads_marauder_join_state_t* st, uint32_t now_ms, uint32_t deadline_ms);

#endif /* CADS_MARAUDER_JOIN_H */
