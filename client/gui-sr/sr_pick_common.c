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

#include <stdio.h>
#include <stdlib.h>

/* utility */
#include "fcintl.h"
#include "mem.h"
#include "support.h"

/* common */
#include "city.h"
#include "nation.h"
#include "player.h"

/* client */
#include "client_main.h"

#include "sr_pick_common.h"

void sr_pick_list_init(struct sr_pick_list *list, int cap)
{
  list->n = 0;
  list->cap = cap;
  list->ids = fc_calloc(cap, sizeof(*list->ids));
  list->labels = fc_calloc(cap, sizeof(*list->labels));
  list->label_ptrs = fc_calloc(cap, sizeof(*list->label_ptrs));
}

bool sr_pick_list_append(struct sr_pick_list *list, int id, const char *label)
{
  if (list->n >= list->cap) {
    return FALSE;
  }
  list->ids[list->n] = id;
  list->labels[list->n] = fc_strdup(label);
  list->label_ptrs[list->n] = list->labels[list->n];
  list->n++;
  return TRUE;
}

void sr_pick_list_free(struct sr_pick_list *list)
{
  int i;

  for (i = 0; i < list->n; i++) {
    free(list->labels[i]);
  }
  free(list->labels);
  free(list->label_ptrs);
  free(list->ids);
  list->labels = NULL;
  list->label_ptrs = NULL;
  list->ids = NULL;
  list->n = 0;
  list->cap = 0;
}

bool sr_pick_list_fill_known_cities(struct sr_pick_list *list)
{
  struct player *me = client_player();
  char buf[256];

  if (me == NULL) {
    return FALSE;
  }

  /* Own cities first — the most common target. */
  city_list_iterate(me->cities, c) {
    if (!sr_pick_list_append(list, c->id, city_name_get(c))) {
      return list->n > 0;
    }
  } city_list_iterate_end;

  players_iterate(p) {
    if (p == me) {
      continue;
    }
    city_list_iterate(p->cities, c) {
      fc_snprintf(buf, sizeof(buf), "%s (%s)",
                  city_name_get(c),
                  nation_adjective_for_player(p));
      if (!sr_pick_list_append(list, c->id, buf)) {
        return list->n > 0;
      }
    } city_list_iterate_end;
  } players_iterate_end;

  return list->n > 0;
}
