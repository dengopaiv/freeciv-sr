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
  SR map/scenario editor entry points.

  These drive the PACKET_EDIT_* packets handled by server/edithand.c
  through accessible keyboard pickers. The editor operates on the
  current target tile (sr_cmd_target_tile()), same cursor used by the
  look / infra / goto pickers.

  Edit mode must be toggled on before tile edits take effect; the
  toggler sends PACKET_EDIT_MODE and announces the resulting state.
  Every other entry point refuses to act when edit mode is off.
*/

#ifndef FC__SR_MAPEDIT_H
#define FC__SR_MAPEDIT_H

/* Toggle edit mode on the connected server. */
void sr_mapedit_toggle(void);

/* Picker to change the terrain of the current target tile. Offers a
   brush-size follow-up (1 / 3 / 5 / 7) before sending. */
void sr_mapedit_terrain(void);

/* Picker to add or remove an extra (river, road, hut, base, resource,
   ...) on the current target tile. Prefix "-" in the label means the
   extra is currently present and picking it will remove; "+" means
   currently absent and picking it will add. */
void sr_mapedit_extra(void);

/* Paint the current target tile with the terrain identified by
   `rule_name` (e.g. "grassland", "ocean"). Returns silently if edit
   mode is off or the rule name is not known in this ruleset. Used by
   the letter-shortcut commands (tg, tp, to, ...). */
void sr_mapedit_paint_by_rule(const char *rule_name);

/* Wipe every tile on the current map to an ocean terrain. Requires
   edit mode on and a game running. */
void sr_mapedit_oceanize(void);

/* Prompt for a save filename, then save current game state. Usable as
   a scenario after editing (set the scenario flag via server command
   first if you want it formally marked as one). */
void sr_mapedit_savescen(void);

/* Start the "empty editor" flow: pick width, pick height, spawn the
   local server, queue xsize/ysize/aifill=0 into the pregame flush.
   User then types `begin` (or /start); on PAGE_GAME the pending flag
   triggers an auto edit-mode + oceanize pass. */
void sr_mapedit_newedit(void);

/* Same as sr_mapedit_newedit() but with width + height supplied as
   arguments — skips the picker popups and spawns the server directly.
   Wired to the `newedit W H` command form for CLI-driven workflows
   where the user would rather not Alt-Tab to a popup. */
void sr_mapedit_newedit_direct(int width, int height);

/* Called from gui_real_set_client_page(PAGE_START). If a newedit is
   pending, pushes xsize / ysize / aifill through the settable-option
   channel and announces the pregame hint. */
void sr_mapedit_flush_at_pregame_if_pending(void);

/* Called from gui_real_set_client_page(PAGE_GAME). If a newedit is
   pending, enables edit mode and oceanizes the fresh map. */
void sr_mapedit_flush_at_game_if_pending(void);

/* True while a newedit is between spawn-server and oceanize-done.
   popup_races_dialog consults this so it can auto-random the nation
   and skip the selection dialog entirely — you don't need a civ
   when you're just laying out scenario terrain. */
bool sr_mapedit_is_newedit_pending(void);

#endif /* FC__SR_MAPEDIT_H */
