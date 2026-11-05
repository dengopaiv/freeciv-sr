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
#include "game.h"                  /* game_city_by_number */
#include "map.h"                   /* index_to_map_pos_x/y */
#include "player.h"
#include "tile.h"

/* client */
#include "client_main.h"
#include "goto.h"                  /* send_rally_tile */

/* gui-sr */
#include "sr_cmd.h"                /* sr_cmd_target_tile */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"

#include "sr_rallypoint.h"

#ifdef _WIN32

#define MAX_RALLY_CITIES 128

struct rally_state {
  struct tile *target;
  int n;
  int city_ids[MAX_RALLY_CITIES];
  char *names[MAX_RALLY_CITIES];
  const char *name_ptrs[MAX_RALLY_CITIES];
  int chosen_city;
};

static void rally_free(struct rally_state *st)
{
  int i;

  for (i = 0; i < st->n; i++) {
    free(st->names[i]);
  }
  free(st);
}

static void rally_on_persistent(int idx, const char *value, void *ud)
{
  struct rally_state *st = ud;
  struct city *pcity = game_city_by_number(st->chosen_city);
  bool persistent = (idx == 0);
  char buf[160];

  (void) value;
  if (pcity == NULL || st->target == NULL) {
    sr_say(_("Rally target no longer valid."), 1);
    rally_free(st);
    return;
  }

  if (send_rally_tile(pcity, st->target, persistent)) {
    fc_snprintf(buf, sizeof(buf),
                _("Rally set for %s -> (%d,%d)%s."),
                city_name_get(pcity),
                index_to_map_pos_x(tile_index(st->target)),
                index_to_map_pos_y(tile_index(st->target)),
                persistent ? _(" (persistent)") : "");
  } else {
    fc_snprintf(buf, sizeof(buf),
                _("Rally rejected for %s."), city_name_get(pcity));
  }
  sr_say(buf, 1);
  sr_log_append(buf);
  rally_free(st);
}

static void rally_on_city(int idx, const char *value, void *ud)
{
  struct rally_state *st = ud;
  static const char *const yn[2] = { "Yes", "No" };

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    rally_free(st);
    return;
  }
  st->chosen_city = st->city_ids[idx];
  sr_pick_enum(_("Persistent rally point?"), yn, 2, 1,
               rally_on_persistent, st);
}

#endif /* _WIN32 */

void sr_rallypoint_open(void)
{
#ifdef _WIN32
  struct rally_state *st;
  struct player *me = client_player();
  struct tile *ptarget = sr_cmd_target_tile();

  if (me == NULL || !can_client_issue_orders()) {
    sr_say(_("Can't set rally points right now."), 1);
    return;
  }
  if (ptarget == NULL) {
    sr_say(_("No target tile. Use 'look' to aim, then 'rally'."), 1);
    return;
  }
  if (city_list_size(me->cities) == 0) {
    sr_say(_("You have no cities."), 1);
    return;
  }

  st = fc_malloc(sizeof(*st));
  st->target = ptarget;
  st->n = 0;
  st->chosen_city = -1;

  city_list_iterate(me->cities, pcity) {
    if (st->n >= MAX_RALLY_CITIES) {
      break;
    }
    st->city_ids[st->n] = pcity->id;
    st->names[st->n] = fc_strdup(city_name_get(pcity));
    st->name_ptrs[st->n] = st->names[st->n];
    st->n++;
  } city_list_iterate_end;

  {
    char buf[128];

    fc_snprintf(buf, sizeof(buf),
                _("Rally target (%d,%d). Pick source city."),
                index_to_map_pos_x(tile_index(ptarget)),
                index_to_map_pos_y(tile_index(ptarget)));
    sr_log_append(buf);
    sr_say(buf, 1);
  }

  sr_pick_enum(_("Rally from which city?"),
               st->name_ptrs, st->n, 0, rally_on_city, st);
#endif /* _WIN32 */
}
