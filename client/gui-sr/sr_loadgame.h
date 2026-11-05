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
  Load-saved-game dialog. Enumerates ".sav" files (including .sav.bz2 /
  .sav.gz / .sav.zst) under the standard save dirs, lets the user pick
  one with arrow + Enter, then spawns a local server and dispatches
  /load <filename> via sr_loadgame_flush_if_pending() on PAGE_LOAD.
*/

#ifndef FC__SR_LOADGAME_H
#define FC__SR_LOADGAME_H

/* Open (or re-focus) the Load Saved Game dialog. If no save files are
   found, speaks "No saved games found." and does not show the dialog. */
void sr_loadgame_open(void);

/* Close without loading. Safe even if not open. */
void sr_loadgame_close(void);

/* Called from gui_real_set_client_page(). When the page being entered
   is PAGE_LOAD and a load is pending, sends "/load <path>" to the
   newly-spawned server. On PAGE_MAIN clears any stale pending state. */
void sr_loadgame_flush_if_pending(int page);

/* Returns 1 (and clears the internal flag) iff a `--file` save-load
   spawn just completed and the client should auto-`/take -` to attach
   to a player. Called from PAGE_START handler. */
int sr_loadgame_consume_take_after_load(void);

#endif  /* FC__SR_LOADGAME_H */
