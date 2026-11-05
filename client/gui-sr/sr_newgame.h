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
  New-game setup dialog. Collects ruleset / AI count / map size / map
  generator / difficulty / barbarians / onset turn / start units, then
  starts the local server and applies the settings on the PAGE_START
  transition via sr_newgame_flush_if_pending().
*/

#ifndef FC__SR_NEWGAME_H
#define FC__SR_NEWGAME_H

/* Open (or re-focus) the New Game dialog. */
void sr_newgame_open(void);

/* Close without starting. Safe even if not open. */
void sr_newgame_close(void);

/* Called by gui_set_rulesets() when the server sends its ruleset list.
   Replaces the hard-coded bootstrap list. */
void sr_newgame_set_rulesets(int n, char **names);

/* Called once from gui_real_set_client_page(PAGE_START). If the user
   started the flow via this dialog, pushes the settings to the server
   via /set and /<difficulty>, selects the ruleset, then /start. */
void sr_newgame_flush_if_pending(void);

#endif  /* FC__SR_NEWGAME_H */
