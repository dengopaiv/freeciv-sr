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
  TreeView model: the primary navigation surface of the gui-sr window.

  Each of seven top-level sections (Game, Units, Cities, Review cursor,
  Known nations, Research, Pregame) gets one HTREEITEM; its children
  are rebuilt in place whenever the client core fires the corresponding
  callback. First-cut implementation is a simple clear-and-repopulate;
  that keeps the code readable at the cost of losing expansion state
  across updates. Diff-based refinement is a future optimisation.
*/

#ifndef FC__SR_TREE_H
#define FC__SR_TREE_H

#ifdef _WIN32
#include <windows.h>
#endif

#include "sr_window.h"   /* enum sr_tree_section */

struct tile;

/* Called once from sr_window after the TreeView is created. Inserts
   the seven section items in order. Safe to call more than once — it
   skips work if already initialised. */
void sr_tree_create_sections(void);

/* Per-section refreshers. Called from the matching client hook:
     game        → mapview.c start_turn, update_info_label
     units       → mapview.c update_unit_info_label, gui_real_focus_units_changed
     cities      → citydlg.c real_city_dialog_refresh, cityrep.c
     review      → sr_cmd.c when the review cursor moves (NULL hides section)
     nations     → plrdlg.c real_players_dialog_update
     research    → repodlgs.c real_science_report_dialog_update
     pregame     → connectdlg / gui_real_conn_list_dialog_update
*/
void sr_tree_rebuild_game(void);
void sr_tree_rebuild_units(void);
void sr_tree_rebuild_cities(void);
void sr_tree_rebuild_review(const struct tile *ptile);
void sr_tree_rebuild_nations(void);
void sr_tree_rebuild_research(void);
void sr_tree_rebuild_pregame(void);

/* Refresh every section. Cheap enough to call at major transitions
   like "game started" and "turn ended". */
void sr_tree_rebuild_all(void);

/* Move keyboard focus to the named section's header. Used by the
   F1–F6 accelerators. */
void sr_tree_focus_section(enum sr_tree_section section);

#endif  /* FC__SR_TREE_H */
