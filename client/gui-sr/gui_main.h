/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
***********************************************************************/

#ifndef FC__GUI_MAIN_H
#define FC__GUI_MAIN_H

#include "gui_main_g.h"

/* Drain the internal idle-callback queue. `gui_add_idle_callback`
   pushes callbacks here; the core client's `update_queue.c` uses this
   to defer per-packet UI updates (page transitions, focus changes,
   etc.). The Win32 message loop in sr_window.c calls this after every
   dispatched message so that update_queue drains continuously during
   a session, not just at quit time. */
void sr_drain_idle(void);

#endif				/* FC__GUI_MAIN_H */
