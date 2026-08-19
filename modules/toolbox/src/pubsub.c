#include "cads/toolbox/pubsub.h"

void cads_pubsub_init(cads_pubsub_t* pubsub) {
    if(!pubsub) return;
    pubsub->head = NULL;
}

void cads_pubsub_subscribe(
    cads_pubsub_t* pubsub,
    cads_pubsub_subscription_t* subscription,
    cads_pubsub_callback_t callback,
    void* context) {
    if(!pubsub || !subscription) return;

    subscription->callback = callback;
    subscription->context = context;
    subscription->next = pubsub->head;
    pubsub->head = subscription;
}

bool cads_pubsub_unsubscribe(cads_pubsub_t* pubsub, cads_pubsub_subscription_t* subscription) {
    if(!pubsub || !subscription) return false;

    cads_pubsub_subscription_t** link = &pubsub->head;
    while(*link) {
        if(*link == subscription) {
            *link = subscription->next;
            subscription->next = NULL;
            return true;
        }
        link = &(*link)->next;
    }
    return false;
}

void cads_pubsub_publish(cads_pubsub_t* pubsub, const void* message) {
    if(!pubsub) return;

    for(cads_pubsub_subscription_t* node = pubsub->head; node != NULL; node = node->next) {
        if(node->callback) node->callback(message, node->context);
    }
}
