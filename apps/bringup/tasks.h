#ifndef CADS_TASKS_H
#define CADS_TASKS_H

/** Create the firmware's tasks and start the scheduler. Does not return. */
void cads_tasks_start(void);

/** Print stack high-water marks, heap usage and input counters. Exposed so the
 *  console can ask for it, which is how the static stack sizes stay honest. */
void cads_tasks_report(void);

#endif /* CADS_TASKS_H */
