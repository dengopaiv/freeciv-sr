/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdlib.h>

/* utility */
#include "fcintl.h"
#include "mem.h"

/* common */
#include "city.h"
#include "game.h"

/* client */
#include "client_main.h"
#include "mapview_common.h"      /* center_tile_mapcanvas */

/* gui-sr */
#include "sr_pick.h"
#include "sr_pick_common.h"
#include "sr_speech.h"

/* gui main header */
#include "gui_sr.h"

#include "finddlg.h"

/* ---------------------------------------------------------------------
   Find-city dialog: lists every city the local player knows about (own
   + foreign visible) as a keyboard-pickable enum. Selection centers the
   map on the picked city; no unit movement (use the sibling Goto-city
   dialog for that).

   The listbox has Win32's standard letter-jump behavior, so a player
   with a long city list can type the first letter of a name to skip.
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_FIND_CITIES 1024

static void find_on_pick(int idx, const char *value, void *ud)
{
  struct sr_pick_list *list = ud;
  struct city *pcity;

  (void) value;
  if (idx < 0 || idx >= list->n) {
    sr_say(_("Cancelled."), 1);
    goto done;
  }
  pcity = game_city_by_number(list->ids[idx]);
  if (pcity == NULL || pcity->tile == NULL) {
    sr_say(_("That city is no longer visible."), 1);
    goto done;
  }
  center_tile_mapcanvas(pcity->tile);
  sr_say(city_name_get(pcity), 1);

done:
  sr_pick_list_free(list);
  free(list);
}

/**********************************************************************//**
  Popup a dialog to ask for a city to find. Centers the map on it.
**************************************************************************/
void popup_find_dialog(void)
{
  struct sr_pick_list *list;

  if (client_player() == NULL) {
    sr_say(_("Not in a game."), 1);
    return;
  }

  list = fc_malloc(sizeof(*list));
  sr_pick_list_init(list, MAX_FIND_CITIES);

  if (!sr_pick_list_fill_known_cities(list)) {
    sr_say(_("No known cities."), 1);
    sr_pick_list_free(list);
    free(list);
    return;
  }

  sr_pick_enum(_("Find city"), list->label_ptrs, list->n, 0,
               find_on_pick, list);
}

#else  /* !_WIN32 */

void popup_find_dialog(void)
{
  /* Non-Windows builds: no-op stub. */
}

#endif
