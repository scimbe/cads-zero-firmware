#ifndef CADS_EXPLORER_GUI_DEMO_H
#define CADS_EXPLORER_GUI_DEMO_H

#include <stdint.h>

/**
 * Run the real GUI stack (gui/view + gui/widgets + apps/gpio) on the panel for
 * `seconds`, then tear it down and hand input back to the input task's normal
 * callback.
 *
 * Exists to prove the multi-agent-built GUI stack works end to end on real
 * hardware, without wiring it into the production task set (apps/bringup/tasks.c)
 * before it has had that proof. See docs/ROADMAP.md M6.
 */
void cads_explorer_gui_demo(uint32_t seconds);

#endif /* CADS_EXPLORER_GUI_DEMO_H */
