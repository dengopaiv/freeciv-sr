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
#include "support.h"

/* common */
#include "city.h"
#include "game.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "goto.h"                /* send_goto_tile */

/* gui-sr */
#include "sr_pick.h"
#include "sr_pick_common.h"
#include "sr_speech.h"

/* gui main header */
#include "gui_sr.h"

#include "gotodlg.h"

/* ---------------------------------------------------------------------
   Goto-city dialog: classic Civ II "Goto City" — pick a known city
   from a keyboard list, focused unit(s) auto-route there. Foreign
   cities are included so e.g. a diplomat can be sent at an enemy
   capital with one keystroke chain.
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_GOTO_CITIES 1024

static void goto_on_pick(int idx, const char *value, void *ud)
{
  struct sr_pick_list *list = ud;
  struct city *pcity;
  int sent = 0;
  char buf[160];

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
  unit_list_iterate(get_units_in_focus(), punit) {
    if (send_goto_tile(punit, pcity->tile)) {
      sent++;
    }
  } unit_list_iterate_end;
  if (sent == 0) {
    sr_say(_("No path to that city."), 1);
  } else {
    fc_snprintf(buf, sizeof(buf),
                PL_("Sent %d unit toward %s.",
                    "Sent %d units toward %s.", sent),
                sent, city_name_get(pcity));
    sr_say(buf, 1);
  }

done:
  sr_pick_list_free(list);
  free(list);
}

/**********************************************************************//**
  Popup a dialog to have the focus unit goto to a city.
**************************************************************************/
void popup_goto_dialog(void)
{
  struct sr_pick_list *list;

  if (client_player() == NULL) {
    sr_say(_("Not in a game."), 1);
    return;
  }
  if (get_num_units_in_focus() == 0) {
    sr_say(_("No unit selected."), 1);
    return;
  }

  list = fc_malloc(sizeof(*list));
  sr_pick_list_init(list, MAX_GOTO_CITIES);

  if (!sr_pick_list_fill_known_cities(list)) {
    sr_say(_("No known cities."), 1);
    sr_pick_list_free(list);
    free(list);
    return;
  }

  sr_pick_enum(_("Goto city"), list->label_ptrs, list->n, 0,
               goto_on_pick, list);
}

#else  /* !_WIN32 */

void popup_goto_dialog(void)
{
  /* Non-Windows builds: no-op stub. */
}

#endif
