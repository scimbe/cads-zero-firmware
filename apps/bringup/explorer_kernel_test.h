#ifndef CADS_EXPLORER_KERNEL_TEST_H
#define CADS_EXPLORER_KERNEL_TEST_H

/**
 * Exercise cads_timer and cads_event under the real, running scheduler and
 * report pass/fail.
 *
 * A one-shot timer signals an event bit from the FreeRTOS timer service task;
 * this function waits on that bit from the console task. Passing proves three
 * things at once: the timer actually fires, its callback runs where the
 * kernel says it does (a different task than the caller), and the event
 * rendezvous between them works - which a unit test cannot exercise, since it
 * would need a real scheduler with real task preemption, not a fake.
 */
void cads_explorer_kernel_test(void);

#endif /* CADS_EXPLORER_KERNEL_TEST_H */
