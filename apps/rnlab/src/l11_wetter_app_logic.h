/*
 * CaDS Zero - rnlab L11 (Wetter-App): pure logic, host-testable.
 *
 * No HAL, no lwIP here - tests/unit/test_rnlab_l11.c links this file
 * directly on the host. The GUI lives in apps/wetter (a view in the apps
 * menu), `lab 11` in l11_wetter_app.c; both drive the ONE controller
 * instance rnlab_l11_app() declared here, so the display and the console
 * always talk about the same state.
 *
 * The controller decides WHEN to fetch and WHAT to show; it never touches
 * the network or the panel itself. That split is what makes a refresh
 * policy with retries and backoff testable with a fake clock.
 */

#ifndef RNLAB_L11_WETTER_APP_LOGIC_H
#define RNLAB_L11_WETTER_APP_LOGIC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "l10_http_wetter_1_logic.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Lesson slug ("wetter-app"). */
const char* rnlab_l11_slug(void);

/* ------------------------------------------------------------------------- */
/* WMO weather code -> text and icon (open-meteo "weather_code")             */
/* ------------------------------------------------------------------------- */

typedef enum {
    RNLAB_WX_ICON_UNKNOWN = 0,
    RNLAB_WX_ICON_CLEAR,   /**< 0                                          */
    RNLAB_WX_ICON_PARTLY,  /**< 1, 2                                       */
    RNLAB_WX_ICON_CLOUDY,  /**< 3                                          */
    RNLAB_WX_ICON_FOG,     /**< 45, 48                                     */
    RNLAB_WX_ICON_DRIZZLE, /**< 51..57                                     */
    RNLAB_WX_ICON_RAIN,    /**< 61..67, 80..82                             */
    RNLAB_WX_ICON_SNOW,    /**< 71..77, 85, 86                             */
    RNLAB_WX_ICON_THUNDER, /**< 95..99                                     */
} rnlab_wx_icon_t;

/** Short German description ("Leichter Regen"), ASCII only (the panel's
 *  fonts have no umlauts), at most 23 characters. "Unbekannt (NN)" is not
 *  produced - unknown codes give "Unbekannt". */
const char* rnlab_wx_wmo_text(int32_t code);

rnlab_wx_icon_t rnlab_wx_wmo_icon(int32_t code);

/* ------------------------------------------------------------------------- */
/* Controller: refresh policy, retries with backoff, states                  */
/* ------------------------------------------------------------------------- */

#define RNLAB_WX_INTERVAL_DEFAULT_MS  (10u * 60u * 1000u) /* open-meteo updates every 15 min */
#define RNLAB_WX_INTERVAL_MIN_MS      (10u * 1000u)       /* be polite to a free API        */
#define RNLAB_WX_RETRY_FIRST_MS       (5u * 1000u)
#define RNLAB_WX_RETRY_MAX_MS         (5u * 60u * 1000u)

typedef struct {
    char host[RNLAB_HTTP_HOST_MAX]; /**< name or IPv4 literal                */
    uint16_t port;
    uint32_t interval_ms;           /**< period after a successful fetch    */
} rnlab_wx_config_t;

typedef enum {
    RNLAB_WX_NO_LINK = 0, /**< Ethernet link down                          */
    RNLAB_WX_NO_IP,       /**< link up, no address (DHCP not bound yet)    */
    RNLAB_WX_WAITING,     /**< nothing fetched yet, first fetch due        */
    RNLAB_WX_FETCHING,    /**< a fetch is running                          */
    RNLAB_WX_OK,          /**< fresh data                                  */
    RNLAB_WX_STALE,       /**< data, but older than 2 intervals            */
    RNLAB_WX_ERROR,       /**< last fetch failed, retry scheduled          */
} rnlab_wx_status_t;

typedef struct {
    rnlab_wx_config_t config;

    bool link_up;
    bool has_ip;
    bool fetching;

    bool have_data;
    rnlab_weather_t data;
    uint32_t data_ms;          /**< clock when `data` arrived              */

    uint32_t next_fetch_ms;    /**< clock of the next due fetch            */
    uint32_t backoff_ms;       /**< current retry delay (0 after success)  */
    uint8_t failures;          /**< consecutive failed fetches             */
    rnlab_fetch_error_t last_error;
    uint16_t last_http_status;

    uint32_t fetches;          /**< started                                */
    uint32_t failed;           /**< of those, failed                       */

    /* Filled in by the app (apps/wetter), read by `lab 11` */
    bool view_open;            /**< controller only runs while shown      */
    uint32_t redraws;          /**< frames the app drew                    */
    uint32_t last_redraw_px;   /**< pixels declared dirty in the last one  */
    uint32_t last_redraw_us;   /**< measured main-loop stretch it caused   */
    uint32_t max_redraw_us;
} rnlab_wx_t;

/** The one instance the app and `lab 11` share. */
rnlab_wx_t* rnlab_l11_app(void);

/** Defaults: open-meteo, port 80, 10 min; first fetch due at `now_ms`. */
void rnlab_wx_init(rnlab_wx_t* wx, uint32_t now_ms);

/**
 * Called every tick with the network state. True means: start a fetch now
 * (the caller does, and calls rnlab_wx_fetch_started()). Never true while
 * a fetch runs, while the link is down or there is no address. When the
 * link comes back, a fetch is due at once instead of waiting for a timer
 * that may still run for minutes.
 */
bool rnlab_wx_should_fetch(rnlab_wx_t* wx, uint32_t now_ms, bool link_up, bool has_ip);

void rnlab_wx_fetch_started(rnlab_wx_t* wx, uint32_t now_ms);

/**
 * A fetch ended. Success: keep the values, next fetch after the interval,
 * backoff reset. Failure: keep the OLD values (stale data beats no data),
 * retry after 5 s, 10 s, 20 s, ... capped at 5 min - so a dead server is
 * not hammered, and a short outage heals quickly.
 */
void rnlab_wx_fetch_done(rnlab_wx_t* wx, uint32_t now_ms, const rnlab_fetch_result_t* result);

/** Fetch as soon as possible (keypress, `lab 11 refresh`); also resets the
 *  backoff, because a human asked. */
void rnlab_wx_request_refresh(rnlab_wx_t* wx, uint32_t now_ms);

rnlab_wx_status_t rnlab_wx_status(const rnlab_wx_t* wx, uint32_t now_ms);

/** Age of the shown data in seconds (0 without data). */
uint32_t rnlab_wx_age_s(const rnlab_wx_t* wx, uint32_t now_ms);

/* ------------------------------------------------------------------------- */
/* View model: the strings on the panel, and what changed                     */
/* ------------------------------------------------------------------------- */

typedef enum {
    RNLAB_WX_LEVEL_OK = 0, /**< normal                                     */
    RNLAB_WX_LEVEL_BUSY,   /**< fetching / waiting                         */
    RNLAB_WX_LEVEL_WARN,   /**< stale, retrying with old data              */
    RNLAB_WX_LEVEL_ERROR,  /**< no data and no way to get it right now     */
} rnlab_wx_level_t;

typedef struct {
    char temperature[12]; /**< "23.2" or "--"  (unit drawn separately)      */
    char humidity[8];     /**< "54" or "--"                                 */
    char wind[12];        /**< "4.1" or "--"                                */
    char condition[24];   /**< rnlab_wx_wmo_text() or ""                    */
    rnlab_wx_icon_t icon;
    char status[48];      /**< one line: "vor 3 min aktualisiert", errors   */
    rnlab_wx_level_t level;
} rnlab_wx_view_t;

#define RNLAB_WX_DIRTY_TEMPERATURE 0x01u
#define RNLAB_WX_DIRTY_HUMIDITY    0x02u
#define RNLAB_WX_DIRTY_WIND        0x04u
#define RNLAB_WX_DIRTY_CONDITION   0x08u /**< text or icon                  */
#define RNLAB_WX_DIRTY_STATUS      0x10u /**< text or level                 */
#define RNLAB_WX_DIRTY_ALL         0x1Fu

/**
 * Fill the view model. The status line changes at most once a minute while
 * data is shown ("vor 3 min"), so an idle app costs one small blit per
 * minute - not one per second.
 */
void rnlab_wx_view(const rnlab_wx_t* wx, uint32_t now_ms, rnlab_wx_view_t* out);

/** Which fields differ (RNLAB_WX_DIRTY_* bits). Only those get redrawn -
 *  every blit stops the Ethernet receiver (PA7). */
uint8_t rnlab_wx_view_diff(const rnlab_wx_view_t* before, const rnlab_wx_view_t* after);

#ifdef __cplusplus
}
#endif

#endif /* RNLAB_L11_WETTER_APP_LOGIC_H */
