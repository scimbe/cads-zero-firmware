/*
 * CaDS Zero - screen streaming over TCP (board side).
 *
 * WIRE PROTOCOL (documented here because there is no reference viewer in
 * this repository yet - see the roadmap bullet this implements: it asks
 * for the firmware to stream frames a host viewer COULD consume, not for
 * the viewer itself, and this project does not build things it cannot
 * verify - see this file's own "VERIFIED" note in docs/ROADMAP.md on why a
 * live remote client could not be tested from here):
 *
 *   Once per connection, a header:
 *     bytes 0-3   magic "CDZ1" (also the protocol version)
 *     bytes 4-5   width,  u16 little-endian
 *     bytes 6-7   height, u16 little-endian
 *     byte  8     bits per pixel (4)
 *     bytes 9-40  palette, 16 entries x 2 bytes RGB565 little-endian
 *   Then forever, one frame at a time:
 *     bytes 0-3   frame length in bytes, u32 little-endian (constant in
 *                 this v1 - always CADS_CANVAS_STRIDE * CADS_CANVAS_HEIGHT -
 *                 but sent explicitly so a future delta/dirty-rect encoding
 *                 does not need a protocol version bump)
 *     bytes 4..   exactly that many bytes: the packed 4bpp framebuffer,
 *                 row-major, two pixels per byte high-nibble-first - the
 *                 same layout cads_canvas_draw_bitmap4() documents, read
 *                 straight out of cads_canvas_buffer() with no conversion.
 *
 * NOT DOUBLE BUFFERED. A frame is written to the TCP send window over
 * several tcp_write() calls as space frees up, and cads_canvas_buffer() can
 * legitimately change in between two of them - drawing and streaming run in
 * the same single task here, exactly like every other bounded explorer demo
 * (explorer_gui_demo.c, explorer_app_demo.c), so nothing races in the C
 * sense, but a frame CAN show part-old, part-new pixels if something drew
 * mid-send. This device has no spare RAM for a second 76 KB buffer to fix
 * that properly (see gui/canvas.h's own header on why 4bpp exists at all -
 * leaving room for lwIP was the point), so occasional tearing in the stream
 * is an accepted, documented v1 limitation rather than a bug to chase.
 */

#include "explorer_screencast_demo.h"

#include <string.h>

#include "canvas.h"
#include "cads/net/net.h"
#include "cads_hal.h"
#include "explorer_eth.h" /* cads_explorer_net_mac() */
#include "input_probe.h"
#include "tasks.h" /* cads_tasks_redraw_sync() */

#include "lwip/tcp.h"

#define CADS_SCREENCAST_PORT        4244u
#define CADS_SCREENCAST_FRAME_SIZE  (CADS_CANVAS_STRIDE * CADS_CANVAS_HEIGHT)
#define CADS_SCREENCAST_HEADER_SIZE 41u
/* Cap per tcp_write() call - keeps each pbuf allocation modest rather than
 * asking lwIP's pool for one huge chunk every time there happens to be a
 * lot of send window free. */
#define CADS_SCREENCAST_CHUNK_MAX 1024u
/* Safety bound on how many chunks one cads_screencast_pump() call will
 * write before returning - lwIP's own send window is far smaller than this
 * in practice, so it is a backstop against an unforeseen huge window, not
 * something normal operation is expected to hit. */
#define CADS_SCREENCAST_MAX_CHUNKS_PER_PUMP 64u

typedef enum {
    CadsScreencastIdle = 0,
    CadsScreencastHeader,
    CadsScreencastFrameLength,
    CadsScreencastFrameBody,
} cads_screencast_phase_t;

typedef struct {
    struct tcp_pcb* pcb;
    bool connected;
    cads_screencast_phase_t phase;
    uint8_t header[CADS_SCREENCAST_HEADER_SIZE];
    uint8_t frame_length_bytes[4];
    uint32_t offset; /* bytes of the current phase's buffer already handed to tcp_write() */
    uint32_t frames_sent;
} cads_screencast_t;

static cads_screencast_t s_cast;
static struct tcp_pcb* s_listen_pcb = NULL;

static void cads_screencast_build_header(uint8_t* out) {
    out[0] = 'C';
    out[1] = 'D';
    out[2] = 'Z';
    out[3] = '1';
    out[4] = (uint8_t)(CADS_CANVAS_WIDTH & 0xFFu);
    out[5] = (uint8_t)((CADS_CANVAS_WIDTH >> 8) & 0xFFu);
    out[6] = (uint8_t)(CADS_CANVAS_HEIGHT & 0xFFu);
    out[7] = (uint8_t)((CADS_CANVAS_HEIGHT >> 8) & 0xFFu);
    out[8] = 4u;

    const uint16_t* palette = cads_canvas_palette_rgb565();
    for(uint32_t i = 0; i < CADS_PALETTE_SIZE; i++) {
        out[9u + i * 2u] = (uint8_t)(palette[i] & 0xFFu);
        out[9u + i * 2u + 1u] = (uint8_t)((palette[i] >> 8) & 0xFFu);
    }
}

/* Writes at most one chunk, bounded by both CADS_SCREENCAST_CHUNK_MAX and
 * whatever send window lwIP currently has free. Returns the new offset -
 * unchanged from `offset` when there was no room to write anything. */
static uint32_t
    cads_screencast_write_chunk(const uint8_t* data, uint32_t total, uint32_t offset) {
    u16_t available = tcp_sndbuf(s_cast.pcb);
    if(available == 0u) return offset;

    uint32_t remaining = total - offset;
    uint32_t chunk = remaining < CADS_SCREENCAST_CHUNK_MAX ? remaining : CADS_SCREENCAST_CHUNK_MAX;
    if((uint32_t)available < chunk) chunk = (uint32_t)available;
    if(chunk == 0u) return offset;

    if(tcp_write(s_cast.pcb, data + offset, (u16_t)chunk, TCP_WRITE_FLAG_COPY) != ERR_OK) {
        return offset;
    }
    tcp_output(s_cast.pcb);
    return offset + chunk;
}

static void cads_screencast_pump(void) {
    if(!s_cast.connected) return;

    for(uint32_t step = 0; step < CADS_SCREENCAST_MAX_CHUNKS_PER_PUMP; step++) {
        uint32_t before = s_cast.offset;

        switch(s_cast.phase) {
        case CadsScreencastHeader:
            s_cast.offset =
                cads_screencast_write_chunk(s_cast.header, CADS_SCREENCAST_HEADER_SIZE, s_cast.offset);
            if(s_cast.offset >= CADS_SCREENCAST_HEADER_SIZE) {
                s_cast.phase = CadsScreencastFrameLength;
                s_cast.offset = 0u;
            }
            break;

        case CadsScreencastFrameLength: {
            uint32_t length = CADS_SCREENCAST_FRAME_SIZE;
            s_cast.frame_length_bytes[0] = (uint8_t)(length & 0xFFu);
            s_cast.frame_length_bytes[1] = (uint8_t)((length >> 8) & 0xFFu);
            s_cast.frame_length_bytes[2] = (uint8_t)((length >> 16) & 0xFFu);
            s_cast.frame_length_bytes[3] = (uint8_t)((length >> 24) & 0xFFu);
            s_cast.offset = cads_screencast_write_chunk(s_cast.frame_length_bytes, 4u, s_cast.offset);
            if(s_cast.offset >= 4u) {
                s_cast.phase = CadsScreencastFrameBody;
                s_cast.offset = 0u;
            }
            break;
        }

        case CadsScreencastFrameBody:
            s_cast.offset = cads_screencast_write_chunk(
                cads_canvas_buffer(), CADS_SCREENCAST_FRAME_SIZE, s_cast.offset);
            if(s_cast.offset >= CADS_SCREENCAST_FRAME_SIZE) {
                s_cast.frames_sent++;
                s_cast.phase = CadsScreencastFrameLength; /* loop: next frame, freshest buffer content */
                s_cast.offset = 0u;
            }
            break;

        default:
            return;
        }

        if(s_cast.offset == before) return; /* send window is full - wait for tcp_sent() to fire */
    }
}

static err_t cads_screencast_sent(void* arg, struct tcp_pcb* pcb, u16_t length) {
    (void)arg;
    (void)pcb;
    (void)length;
    cads_screencast_pump();
    return ERR_OK;
}

static err_t cads_screencast_recv(void* arg, struct tcp_pcb* pcb, struct pbuf* p, err_t err) {
    (void)arg;
    (void)err;
    if(!p) {
        tcp_close(pcb);
        s_cast.connected = false;
        return ERR_OK;
    }
    /* One-way stream: whatever the client sends is discarded, not fed to
     * anything - there is no command channel here, just pixels out. */
    tcp_recved(pcb, p->tot_len);
    pbuf_free(p);
    return ERR_OK;
}

static void cads_screencast_error(void* arg, err_t err) {
    (void)arg;
    (void)err;
    s_cast.connected = false;
}

static err_t cads_screencast_accept(void* arg, struct tcp_pcb* new_pcb, err_t err) {
    (void)arg;
    if(err != ERR_OK || new_pcb == NULL) return ERR_VAL;

    if(s_cast.connected) {
        /* One viewer at a time - a second connection would otherwise
         * interleave with the first mid frame, which is not "two streams",
         * it is one corrupted one. */
        tcp_close(new_pcb);
        return ERR_OK;
    }

    memset(&s_cast, 0, sizeof(s_cast));
    s_cast.pcb = new_pcb;
    s_cast.connected = true;
    s_cast.phase = CadsScreencastHeader;
    cads_screencast_build_header(s_cast.header);

    tcp_arg(new_pcb, NULL);
    tcp_recv(new_pcb, cads_screencast_recv);
    tcp_sent(new_pcb, cads_screencast_sent);
    tcp_err(new_pcb, cads_screencast_error);

    cads_screencast_pump();
    return ERR_OK;
}

static bool cads_screencast_start(uint16_t port) {
    if(s_listen_pcb != NULL) return true;

    struct tcp_pcb* pcb = tcp_new();
    if(!pcb) return false;

    if(tcp_bind(pcb, IP_ADDR_ANY, port) != ERR_OK) {
        tcp_close(pcb);
        return false;
    }

    struct tcp_pcb* listening = tcp_listen(pcb);
    if(!listening) {
        tcp_close(pcb);
        return false;
    }

    s_listen_pcb = listening;
    tcp_accept(s_listen_pcb, cads_screencast_accept);
    return true;
}

/* A small moving marker rather than a static pattern - proves the stream is
 * reading a genuinely changing buffer, not one frame cached at connect
 * time. Kept to a small damaged rectangle deliberately: gui/canvas.h's own
 * header explains why a full-screen flush costs ~440 ms on this panel, and
 * this demo wants visible motion on the physical panel too, not just in
 * the TCP stream - cads_tasks_redraw_sync() waits for the real ui task's
 * own flush rather than flushing here itself, the same rule
 * apps/bringup/explorer.c's cads_pattern() already follows. */
#define CADS_SCREENCAST_MARKER_SIZE 24
static void cads_screencast_draw_marker(uint32_t now_ms) {
    static int16_t last_x = -1;
    int16_t x = (int16_t)((now_ms / 20u) % (CADS_CANVAS_WIDTH - CADS_SCREENCAST_MARKER_SIZE));
    if(x == last_x) return;

    if(last_x >= 0) {
        cads_canvas_fill_rect(last_x, 40, CADS_SCREENCAST_MARKER_SIZE, CADS_SCREENCAST_MARKER_SIZE,
            CadsColorBackground);
    }
    cads_canvas_fill_rect(x, 40, CADS_SCREENCAST_MARKER_SIZE, CADS_SCREENCAST_MARKER_SIZE, CadsColorAccent);
    last_x = x;
    cads_tasks_redraw_sync(500u);
}

void cads_explorer_screencast_demo(uint32_t seconds) {
    if(!seconds) seconds = 30u;

    cads_net_init(cads_explorer_net_mac());
    bool started = cads_screencast_start(CADS_SCREENCAST_PORT);
    cads_probe_puts(started ? "# screencast: listening on TCP :4244\r\n" :
                               "# screencast: failed to start listener\r\n");

    cads_canvas_clear(CadsColorBackground);
    cads_tasks_redraw_sync(2000u);

    cads_probe_puts("# screencast: running for ");
    cads_probe_put_uint(seconds);
    cads_probe_puts("s\r\n");

    uint32_t start = cads_hal_ticks_ms();
    while(cads_hal_ticks_ms() - start < seconds * 1000u) {
        cads_net_poll();
        cads_screencast_pump();
        cads_screencast_draw_marker(cads_hal_ticks_ms());
        cads_hal_delay_ms(10u);
    }

    cads_probe_puts("# screencast: done, ");
    cads_probe_put_uint(s_cast.frames_sent);
    cads_probe_puts(" full frame(s) sent to the last/only viewer\r\n");
}
