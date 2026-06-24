#ifndef KOLIBRIARM_GUI_DRAG_POLICY_H
#define KOLIBRIARM_GUI_DRAG_POLICY_H

/*
 * gui.c historically exposes these entry points as normal functions and calls
 * them internally. Mark those definitions weak so gui_drag_policy.c can provide
 * the window-manager/input policy without a broad gui.c rewrite.
 */
#pragma weak gui_drag_start
#pragma weak gui_window_push_event

#endif
