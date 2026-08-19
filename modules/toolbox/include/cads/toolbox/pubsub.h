/*
 * CaDS Zero toolbox - publish/subscribe, decoupling a state change from
 * everyone who cares about it.
 *
 * The motivating case is docs/ROADMAP.md M5's "link state, status bar
 * indicator": the network task learns the PHY link came up; the status bar
 * widget wants to know. Without this, that means either the network task
 * reaching into gui/widgets directly (a portable-vs-target-specific layering
 * violation - the network stack is board-only, the status bar is portable),
 * or a polling loop somewhere. A subscription is neither: the publisher
 * names nothing about who is listening, and a listener can come and go
 * without the publisher's code changing.
 *
 * NOT THREAD SAFE, DELIBERATELY
 * ------------------------------
 * modules/toolbox sits at the bottom of the dependency graph so the host
 * unit tests can link it with nothing else - see modules/toolbox/README.md.
 * modules/kernel (cads_mutex) sits above it and cannot be depended on from
 * here without inverting that graph. A publisher and a subscriber running on
 * different FreeRTOS tasks must serialize their own access to a given
 * cads_pubsub_t, the same way any caller-owned, unsynchronized structure in
 * this codebase already requires (cads_ring_t is the precedent).
 *
 * NO HEAP
 * -------
 * A subscription is a caller-owned node threaded into an intrusive singly
 * linked list. subscribe() never allocates; it only links a node the caller
 * already owns (typically a field inside whatever object is subscribing).
 * The node must outlive the subscription - unsubscribe() before it is freed
 * or goes out of scope.
 */

#ifndef CADS_TOOLBOX_PUBSUB_H
#define CADS_TOOLBOX_PUBSUB_H

#include <stdbool.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*cads_pubsub_callback_t)(const void* message, void* context);

typedef struct cads_pubsub_subscription {
    /* --- private --- */
    cads_pubsub_callback_t callback;
    void* context;
    struct cads_pubsub_subscription* next;
} cads_pubsub_subscription_t;

typedef struct {
    /* --- private --- */
    cads_pubsub_subscription_t* head;
} cads_pubsub_t;

void cads_pubsub_init(cads_pubsub_t* pubsub);

/**
 * Subscribe `subscription` to `pubsub`.
 *
 * `subscription` is borrowed and must outlive the subscription (until
 * cads_pubsub_unsubscribe() or the pubsub itself goes away). Subscribing the
 * same node twice without unsubscribing first corrupts the list - callers
 * that might do this by accident should track their own subscribed state.
 */
void cads_pubsub_subscribe(
    cads_pubsub_t* pubsub,
    cads_pubsub_subscription_t* subscription,
    cads_pubsub_callback_t callback,
    void* context);

/** Remove `subscription`. Returns false if it was not subscribed to this
 *  pubsub (already removed, or never added) - a caller that unsubscribes
 *  twice by mistake finds out rather than corrupting the list. */
bool cads_pubsub_unsubscribe(cads_pubsub_t* pubsub, cads_pubsub_subscription_t* subscription);

/**
 * Call every subscribed callback with `message`, most recently subscribed
 * first.
 *
 * Delivery order is unspecified beyond that - nothing here promises FIFO,
 * and a callback that depends on ordering relative to another subscriber has
 * assumed something this module does not provide. A callback that
 * subscribes or unsubscribes *this* pubsub from within its own call is not
 * supported: the list is walked live, not snapshotted.
 */
void cads_pubsub_publish(cads_pubsub_t* pubsub, const void* message);

#ifdef __cplusplus
}
#endif

#endif /* CADS_TOOLBOX_PUBSUB_H */
