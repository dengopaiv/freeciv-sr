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

/*
  stdin command processor for gui-sr.

  A reader thread owns stdin and pushes complete lines into a small
  ring buffer. The main (event-loop) thread calls sr_cmd_poll() once
  per loop iteration to drain and dispatch them.
*/

#ifndef FC__SR_CMD_H
#define FC__SR_CMD_H

void sr_cmd_init(void);
void sr_cmd_poll(void);

/* Run one command line through the dispatcher. Entry point for callers
   that already have a line in hand (the Win32 command EDIT, the menu
   translator, unit tests) — same parser as the stdin path. */
void sr_cmd_dispatch_line(const char *line);

/* Non-zero when the player is aiming a goto destination: the Map View
   uses this to reinterpret arrow/Enter while it is active. */
int sr_cmd_is_goto_active(void);

/* Current "pointed-at" tile: review_cursor if the user is exploring,
   else the focused unit's tile, else NULL. Exposed so dialogs like the
   rally-point picker can use the exploration cursor as a target. */
struct tile *sr_cmd_target_tile(void);

/* Flip the tile-readout verbosity (terse <-> verbose) and announce
   the new state. Used by the menu's "Toggle verbose readout" item. */
void sr_cmd_toggle_terse(void);

#endif  /* FC__SR_CMD_H */
