/*
 * CaDS Zero - the lab's hook dispatcher.
 *
 * The one strong definition of the driver's hook points
 * (cads/net/rnlab_hooks.h), fanning each call out to the lessons'
 * rnlab_lNN_hook_*() in order 01..11. Every lesson hook has a weak no-op
 * default below, so a lesson implements only the ones it needs, in its own
 * lNN_<slug>.c, and two lessons hooking the same point link side by side.
 *
 * Its own library (cads_rnlab_hooks), linked from modules/net: the driver
 * and lwIP's ip4.c reference these symbols, and nothing else pulls this
 * object in. Portable - the host test links it with fake lesson hooks
 * (tests/unit/test_rnlab_hooks.c); only the board's driver calls it.
 */

#include "cads/net/rnlab_hooks.h"
#include "rnlab/rnlab_lesson.h"

#define RNLAB_WEAK_LESSON_HOOKS(nn)                                                       \
    __attribute__((weak)) void rnlab_l##nn##_hook_rx_frame(const uint8_t* frame, size_t len) { \
        (void)frame;                                                                      \
        (void)len;                                                                        \
    }                                                                                     \
    __attribute__((weak)) bool rnlab_l##nn##_hook_rx_drop(const uint8_t* frame, size_t len) { \
        (void)frame;                                                                      \
        (void)len;                                                                        \
        return false;                                                                     \
    }                                                                                     \
    __attribute__((weak)) void rnlab_l##nn##_hook_tx_frame(const uint8_t* frame, size_t len) { \
        (void)frame;                                                                      \
        (void)len;                                                                        \
    }                                                                                     \
    __attribute__((weak)) bool rnlab_l##nn##_hook_tx_drop(const uint8_t* frame, size_t len) { \
        (void)frame;                                                                      \
        (void)len;                                                                        \
        return false;                                                                     \
    }                                                                                     \
    __attribute__((weak)) int rnlab_l##nn##_hook_ip4_input(struct pbuf* p, struct netif* inp) { \
        (void)p;                                                                          \
        (void)inp;                                                                        \
        return 0;                                                                         \
    }

RNLAB_WEAK_LESSON_HOOKS(01)
RNLAB_WEAK_LESSON_HOOKS(02)
RNLAB_WEAK_LESSON_HOOKS(03)
RNLAB_WEAK_LESSON_HOOKS(04)
RNLAB_WEAK_LESSON_HOOKS(05)
RNLAB_WEAK_LESSON_HOOKS(06)
RNLAB_WEAK_LESSON_HOOKS(07)
RNLAB_WEAK_LESSON_HOOKS(08)
RNLAB_WEAK_LESSON_HOOKS(09)
RNLAB_WEAK_LESSON_HOOKS(10)
RNLAB_WEAK_LESSON_HOOKS(11)

typedef void (*rnlab_frame_fn)(const uint8_t* frame, size_t len);
typedef bool (*rnlab_drop_fn)(const uint8_t* frame, size_t len);
typedef int (*rnlab_ip4_fn)(struct pbuf* p, struct netif* inp);

/* Tables in flash, in lesson order. Calling through them (rather than a
 * direct call per lesson) is also what keeps the compiler from assuming a
 * weak default's body. */
static const rnlab_frame_fn rnlab_rx_frame_hooks[] = {
    rnlab_l01_hook_rx_frame, rnlab_l02_hook_rx_frame, rnlab_l03_hook_rx_frame, rnlab_l04_hook_rx_frame,
    rnlab_l05_hook_rx_frame, rnlab_l06_hook_rx_frame, rnlab_l07_hook_rx_frame, rnlab_l08_hook_rx_frame,
    rnlab_l09_hook_rx_frame, rnlab_l10_hook_rx_frame, rnlab_l11_hook_rx_frame,
};
static const rnlab_drop_fn rnlab_rx_drop_hooks[] = {
    rnlab_l01_hook_rx_drop, rnlab_l02_hook_rx_drop, rnlab_l03_hook_rx_drop, rnlab_l04_hook_rx_drop,
    rnlab_l05_hook_rx_drop, rnlab_l06_hook_rx_drop, rnlab_l07_hook_rx_drop, rnlab_l08_hook_rx_drop,
    rnlab_l09_hook_rx_drop, rnlab_l10_hook_rx_drop, rnlab_l11_hook_rx_drop,
};
static const rnlab_frame_fn rnlab_tx_frame_hooks[] = {
    rnlab_l01_hook_tx_frame, rnlab_l02_hook_tx_frame, rnlab_l03_hook_tx_frame, rnlab_l04_hook_tx_frame,
    rnlab_l05_hook_tx_frame, rnlab_l06_hook_tx_frame, rnlab_l07_hook_tx_frame, rnlab_l08_hook_tx_frame,
    rnlab_l09_hook_tx_frame, rnlab_l10_hook_tx_frame, rnlab_l11_hook_tx_frame,
};
static const rnlab_drop_fn rnlab_tx_drop_hooks[] = {
    rnlab_l01_hook_tx_drop, rnlab_l02_hook_tx_drop, rnlab_l03_hook_tx_drop, rnlab_l04_hook_tx_drop,
    rnlab_l05_hook_tx_drop, rnlab_l06_hook_tx_drop, rnlab_l07_hook_tx_drop, rnlab_l08_hook_tx_drop,
    rnlab_l09_hook_tx_drop, rnlab_l10_hook_tx_drop, rnlab_l11_hook_tx_drop,
};
static const rnlab_ip4_fn rnlab_ip4_input_hooks[] = {
    rnlab_l01_hook_ip4_input, rnlab_l02_hook_ip4_input, rnlab_l03_hook_ip4_input, rnlab_l04_hook_ip4_input,
    rnlab_l05_hook_ip4_input, rnlab_l06_hook_ip4_input, rnlab_l07_hook_ip4_input, rnlab_l08_hook_ip4_input,
    rnlab_l09_hook_ip4_input, rnlab_l10_hook_ip4_input, rnlab_l11_hook_ip4_input,
};

#define RNLAB_HOOK_COUNT (sizeof(rnlab_rx_frame_hooks) / sizeof(rnlab_rx_frame_hooks[0]))

void rnlab_hook_rx_frame(const uint8_t* frame, size_t len) {
    for(size_t i = 0; i < RNLAB_HOOK_COUNT; i++) rnlab_rx_frame_hooks[i](frame, len);
}

/* Every lesson is asked, even after one said "drop" - a lesson counting
 * frames must not miss the ones another lesson discards. */
bool rnlab_hook_rx_drop(const uint8_t* frame, size_t len) {
    bool drop = false;
    for(size_t i = 0; i < RNLAB_HOOK_COUNT; i++) {
        if(rnlab_rx_drop_hooks[i](frame, len)) drop = true;
    }
    return drop;
}

void rnlab_hook_tx_frame(const uint8_t* frame, size_t len) {
    for(size_t i = 0; i < RNLAB_HOOK_COUNT; i++) rnlab_tx_frame_hooks[i](frame, len);
}

bool rnlab_hook_tx_drop(const uint8_t* frame, size_t len) {
    bool drop = false;
    for(size_t i = 0; i < RNLAB_HOOK_COUNT; i++) {
        if(rnlab_tx_drop_hooks[i](frame, len)) drop = true;
    }
    return drop;
}

/* Stops at the first taker: that lesson has already freed the pbuf, so no
 * later lesson may touch it. */
int rnlab_hook_ip4_input(struct pbuf* p, struct netif* inp) {
    for(size_t i = 0; i < RNLAB_HOOK_COUNT; i++) {
        int consumed = rnlab_ip4_input_hooks[i](p, inp);
        if(consumed != 0) return consumed;
    }
    return 0;
}
