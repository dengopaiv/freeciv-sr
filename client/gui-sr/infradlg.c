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
#include "extras.h"
#include "map.h"
#include "player.h"
#include "tile.h"

/* client */
#include "client_main.h"
#include "mapview_common.h"        /* client_infratile_set */

/* gui-sr */
#include "gui_sr.h"
#include "sr_cmd.h"                /* sr_cmd_target_tile */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_pick_common.h"
#include "sr_speech.h"

#include "infradlg.h"

/* ---------------------------------------------------------------------
   Infrastructure-points placement.

   The gtk dialog is two-phase (pick tile by clicking, then pick extra).
   The SR client picks the tile from the existing review cursor / focus
   unit (sr_cmd_target_tile()), so we go directly to the extra picker.
   On commit, sends the same dsend_packet_player_place_infra that
   gui-gtk-3.22/infradlg.c sends.
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_INFRA_EXTRAS 64

struct infra_pick {
  struct sr_pick_list extras;    /* id = extra_id */
  int tile_index;
};

static void infra_free(struct infra_pick *st)
{
  sr_pick_list_free(&st->extras);
  client_infratile_set(NULL);
  free(st);
}

static void infra_on_pick(int idx, const char *value, void *ud)
{
  struct infra_pick *st = ud;
  struct extra_type *pextra;
  struct tile *ptile;
  char buf[160];

  (void) value;
  if (idx < 0 || idx >= st->extras.n) {
    sr_say(_("Canceled."), 1);
    infra_free(st);
    return;
  }
  pextra = extra_by_number(st->extras.ids[idx]);
  ptile = index_to_tile(&(wld.map), st->tile_index);
  if (pextra == NULL || ptile == NULL) {
    sr_say(_("Placement target gone."), 1);
    infra_free(st);
    return;
  }
  dsend_packet_player_place_infra(&client.conn, ptile->index, pextra->id);
  fc_snprintf(buf, sizeof(buf),
              _("Requested placement of %s at (%d,%d). %d infrapoint(s) left."),
              extra_name_translation(pextra),
              index_to_map_pos_x(ptile->index),
              index_to_map_pos_y(ptile->index),
              client.conn.playing != NULL
                ? client.conn.playing->economic.infra_points : 0);
  sr_say(buf, 1);
  sr_log_append(buf);
  infra_free(st);
}

#endif /* _WIN32 */

void infra_dialog_popup(void)
{
#ifdef _WIN32
  struct tile *ptile = sr_cmd_target_tile();
  struct player *me = client_player();
  struct infra_pick *st;
  char buf[160];

  if (me == NULL || !can_client_issue_orders()) {
    sr_say(_("Can't place infra right now."), 1);
    return;
  }
  if (ptile == NULL) {
    sr_say(_("No target tile. Use 'look' to aim, then 'infra'."), 1);
    return;
  }
  if (!client_map_is_known_and_seen(ptile, me, V_MAIN)) {
    sr_say(_("Target tile not visible."), 1);
    return;
  }

  client_infratile_set(ptile);

  st = fc_malloc(sizeof(*st));
  st->tile_index = ptile->index;
  sr_pick_list_init(&st->extras, MAX_INFRA_EXTRAS);

  extra_type_iterate(pextra) {
    if (player_can_place_extra(pextra, me, ptile)) {
      sr_pick_list_append(&st->extras, pextra->id,
                          extra_name_translation(pextra));
    }
  } extra_type_iterate_end;

  if (st->extras.n == 0) {
    sr_say(_("No infra can be placed on that tile."), 1);
    infra_free(st);
    return;
  }

  fc_snprintf(buf, sizeof(buf),
              _("%d infrapoint(s) available; %d placement option(s) for "
                "(%d,%d)."),
              me->economic.infra_points, st->extras.n,
              index_to_map_pos_x(ptile->index),
              index_to_map_pos_y(ptile->index));
  sr_say(buf, 1);
  sr_log_append(buf);

  sr_pick_enum(_("Place which extra?"),
               st->extras.label_ptrs, st->extras.n, 0, infra_on_pick, st);
#endif /* _WIN32 */
}

/************************************************************************//**
  Refresh infra dialog. Nothing persistent to refresh — the picker is
  one-shot and reads infrapoints fresh when it opens.
****************************************************************************/
void gui_update_infra_dialog(void)
{
}
