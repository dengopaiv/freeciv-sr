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
#include <string.h>

/* utility */
#include "fcintl.h"
#include "mem.h"
#include "support.h"

/* common */
#include "extras.h"
#include "game.h"
#include "map.h"
#include "player.h"
#include "terrain.h"
#include "tile.h"
#include "unitlist.h"

/* client */
#include "chatline_common.h"       /* send_chat */
#include "client_main.h"
#include "connectdlg_common.h"     /* send_save_game */
#include "pages_g.h"               /* client_start_server_and_set_page */

/* gui-sr */
#include "sr_cmd.h"                /* sr_cmd_target_tile */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_pick_common.h"
#include "sr_speech.h"

#include "sr_mapedit.h"

#ifdef _WIN32

static bool require_connected(void)
{
  if (!client.conn.established) {
    sr_say(_("Not connected to a server."), 1);
    return FALSE;
  }
  return TRUE;
}

static bool require_edit_mode(void)
{
  if (!require_connected()) {
    return FALSE;
  }
  if (!game.info.is_edit_mode) {
    sr_say(_("Edit mode is off. Use 'editor' to toggle."), 1);
    return FALSE;
  }
  return TRUE;
}

static struct tile *require_target_tile(void)
{
  struct tile *ptile = sr_cmd_target_tile();

  if (ptile == NULL) {
    sr_say(_("No target tile. Use 'look' or focus a unit first."), 1);
  }
  return ptile;
}

/* Returns the first ocean terrain in the current ruleset, or NULL. */
static struct terrain *any_ocean_terrain(void)
{
  terrain_type_iterate(pterrain) {
    if (is_ocean(pterrain)) {
      return pterrain;
    }
  } terrain_type_iterate_end;
  return NULL;
}

/* ---------------------------------------------------------------------
   Edit mode toggle (PACKET_EDIT_MODE).
   ------------------------------------------------------------------ */

void sr_mapedit_toggle(void)
{
  bool new_state;
  char buf[120];

  if (!require_connected()) {
    return;
  }
  new_state = !game.info.is_edit_mode;
  dsend_packet_edit_mode(&client.conn, new_state);
  fc_snprintf(buf, sizeof(buf),
              new_state ? _("Edit mode requested: ON.")
                        : _("Edit mode requested: OFF."));
  sr_say(buf, 1);
}

/* ---------------------------------------------------------------------
   Terrain picker with brush-size follow-up.

   Chain: pick terrain -> pick size -> apply. The size is the side of a
   square centered on the target tile; PACKET_EDIT_TILE_TERRAIN's UINT8
   size field drives the server-side multi-tile fill in one packet.
   ------------------------------------------------------------------ */

struct terrain_pick {
  struct sr_pick_list terrains;      /* id = terrain_number */
  int tile_index;
  int chosen_terrain_id;
};

static void terrain_pick_free(struct terrain_pick *st)
{
  sr_pick_list_free(&st->terrains);
  free(st);
}

static const char *const BRUSH_VALUES[] = {
  "1 tile", "3x3", "5x5", "7x7"
};
static const int BRUSH_SIZES[] = { 1, 3, 5, 7 };

static void on_terrain_brush(int idx, const char *value, void *ud)
{
  struct terrain_pick *st = ud;
  struct terrain *pterrain;
  struct tile *ptile;
  int size;
  char buf[160];

  (void)value;
  if (idx < 0 || idx >= (int)(sizeof(BRUSH_SIZES) / sizeof(BRUSH_SIZES[0]))) {
    sr_say(_("Canceled."), 1);
    terrain_pick_free(st);
    return;
  }
  size = BRUSH_SIZES[idx];
  pterrain = terrain_by_number(st->chosen_terrain_id);
  ptile = index_to_tile(&(wld.map), st->tile_index);
  if (pterrain == NULL || ptile == NULL) {
    sr_say(_("Target gone."), 1);
    terrain_pick_free(st);
    return;
  }
  dsend_packet_edit_tile_terrain(&client.conn, ptile->index,
                                 terrain_number(pterrain), size);
  fc_snprintf(buf, sizeof(buf),
              _("Set (%d,%d) terrain = %s (%s)."),
              index_to_map_pos_x(ptile->index),
              index_to_map_pos_y(ptile->index),
              terrain_name_translation(pterrain),
              BRUSH_VALUES[idx]);
  sr_say(buf, 1);
  terrain_pick_free(st);
}

static void on_terrain_pick(int idx, const char *value, void *ud)
{
  struct terrain_pick *st = ud;

  (void)value;
  if (idx < 0 || idx >= st->terrains.n) {
    sr_say(_("Canceled."), 1);
    terrain_pick_free(st);
    return;
  }
  st->chosen_terrain_id = st->terrains.ids[idx];

  sr_pick_enum(_("Brush size"), BRUSH_VALUES,
               (int)(sizeof(BRUSH_VALUES) / sizeof(BRUSH_VALUES[0])), 0,
               on_terrain_brush, st);
}

void sr_mapedit_terrain(void)
{
  struct tile *ptile;
  struct terrain_pick *st;
  int current_idx = 0;
  int i = 0;

  if (!require_edit_mode()) {
    return;
  }
  ptile = require_target_tile();
  if (ptile == NULL) {
    return;
  }

  st = fc_malloc(sizeof(*st));
  st->tile_index = ptile->index;
  st->chosen_terrain_id = -1;
  sr_pick_list_init(&st->terrains, terrain_count());

  terrain_type_iterate(pterrain) {
    sr_pick_list_append(&st->terrains, terrain_number(pterrain),
                        terrain_name_translation(pterrain));
    if (pterrain == tile_terrain(ptile)) {
      current_idx = i;
    }
    i++;
  } terrain_type_iterate_end;

  if (st->terrains.n == 0) {
    sr_say(_("No terrain types in this ruleset."), 1);
    terrain_pick_free(st);
    return;
  }

  sr_pick_enum(_("Terrain for target tile"),
               st->terrains.label_ptrs, st->terrains.n, current_idx,
               on_terrain_pick, st);
}

/* ---------------------------------------------------------------------
   Extra toggle picker (PACKET_EDIT_TILE_EXTRA).
   ------------------------------------------------------------------ */

struct extra_pick {
  struct sr_pick_list extras;         /* id = extra_number */
  bool *was_present;                  /* parallel to extras.ids */
  int tile_index;
};

static void extra_pick_free(struct extra_pick *st)
{
  sr_pick_list_free(&st->extras);
  free(st->was_present);
  free(st);
}

static void on_extra_pick(int idx, const char *value, void *ud)
{
  struct extra_pick *st = ud;
  struct extra_type *pextra;
  struct tile *ptile;
  bool removal;
  char buf[160];

  (void)value;
  if (idx < 0 || idx >= st->extras.n) {
    sr_say(_("Canceled."), 1);
    extra_pick_free(st);
    return;
  }
  pextra = extra_by_number(st->extras.ids[idx]);
  ptile = index_to_tile(&(wld.map), st->tile_index);
  if (pextra == NULL || ptile == NULL) {
    sr_say(_("Target gone."), 1);
    extra_pick_free(st);
    return;
  }
  removal = st->was_present[idx];
  dsend_packet_edit_tile_extra(&client.conn, ptile->index,
                               extra_number(pextra), removal, 0, 1);
  fc_snprintf(buf, sizeof(buf),
              removal ? _("Removed %s at (%d,%d).")
                      : _("Added %s at (%d,%d)."),
              extra_name_translation(pextra),
              index_to_map_pos_x(ptile->index),
              index_to_map_pos_y(ptile->index));
  sr_say(buf, 1);
  extra_pick_free(st);
}

void sr_mapedit_extra(void)
{
  struct tile *ptile;
  struct extra_pick *st;
  char buf[160];
  int cap;

  if (!require_edit_mode()) {
    return;
  }
  ptile = require_target_tile();
  if (ptile == NULL) {
    return;
  }

  cap = extra_count();
  st = fc_malloc(sizeof(*st));
  st->tile_index = ptile->index;
  sr_pick_list_init(&st->extras, cap);
  st->was_present = fc_calloc(cap, sizeof(*st->was_present));

  extra_type_iterate(pextra) {
    bool present = tile_has_extra(ptile, pextra);

    fc_snprintf(buf, sizeof(buf), "%s%s",
                present ? "-" : "+",
                extra_name_translation(pextra));
    if (sr_pick_list_append(&st->extras, extra_number(pextra), buf)) {
      st->was_present[st->extras.n - 1] = present;
    }
  } extra_type_iterate_end;

  if (st->extras.n == 0) {
    sr_say(_("No extras defined in this ruleset."), 1);
    extra_pick_free(st);
    return;
  }

  sr_pick_enum(_("Toggle extra (- removes, + adds)"),
               st->extras.label_ptrs, st->extras.n, 0,
               on_extra_pick, st);
}

/* ---------------------------------------------------------------------
   Paint-by-rule-name (letter shortcuts).

   Looks up a terrain by rule_name ("grassland", "plains", "ocean", ...)
   and paints the current target tile. Silent-fail by design: these are
   invoked from a static shortcut table in sr_cmd.c, and a ruleset that
   lacks a given terrain shouldn't spam the user — they can fall back
   to the full `terrain` picker.
   ------------------------------------------------------------------ */

void sr_mapedit_paint_by_rule(const char *rule_name)
{
  struct terrain *pterrain;
  struct tile *ptile;
  char buf[160];

  if (rule_name == NULL) {
    return;
  }
  if (!require_edit_mode()) {
    return;
  }
  ptile = require_target_tile();
  if (ptile == NULL) {
    return;
  }
  pterrain = terrain_by_rule_name(rule_name);
  if (pterrain == NULL) {
    fc_snprintf(buf, sizeof(buf),
                _("Terrain '%s' not in this ruleset."), rule_name);
    sr_say(buf, 1);
    return;
  }
  dsend_packet_edit_tile_terrain(&client.conn, ptile->index,
                                 terrain_number(pterrain), 1);
  fc_snprintf(buf, sizeof(buf),
              _("Set (%d,%d) terrain = %s."),
              index_to_map_pos_x(ptile->index),
              index_to_map_pos_y(ptile->index),
              terrain_name_translation(pterrain));
  sr_say(buf, 1);
}

/* ---------------------------------------------------------------------
   Oceanize: wipe every tile on the current map to an ocean terrain.
   ------------------------------------------------------------------ */

static void oceanize_map(void)
{
  struct terrain *ocean = any_ocean_terrain();
  int ocean_id;
  int count = 0;
  char buf[120];

  if (ocean == NULL) {
    sr_say(_("This ruleset has no ocean terrain."), 1);
    return;
  }
  ocean_id = terrain_number(ocean);

  whole_map_iterate(&(wld.map), ptile) {
    dsend_packet_edit_tile_terrain(&client.conn, ptile->index, ocean_id, 1);
    count++;
  } whole_map_iterate_end;

  fc_snprintf(buf, sizeof(buf),
              _("Oceanize: sent %d tile edits."), count);
  sr_say(buf, 1);
}

void sr_mapedit_oceanize(void)
{
  if (!require_edit_mode()) {
    return;
  }
  oceanize_map();
}

/* ---------------------------------------------------------------------
   Save current state to a named file.
   ------------------------------------------------------------------ */

static void on_savescen_name(const char *value, void *ud)
{
  char buf[240];

  (void)ud;
  if (value == NULL || value[0] == '\0') {
    sr_say(_("Save canceled."), 1);
    return;
  }
  send_save_game(value);
  fc_snprintf(buf, sizeof(buf), _("Save requested: %s"), value);
  sr_say(buf, 1);
}

void sr_mapedit_savescen(void)
{
  if (!require_connected()) {
    return;
  }
  sr_pick_string(_("Save filename"), "scenario.sav",
                 on_savescen_name, NULL);
}

/* ---------------------------------------------------------------------
   newedit: pick width + height, spawn server, queue settings, auto-
   enter edit mode + oceanize once game is running.
   ------------------------------------------------------------------ */

static struct {
  bool pending_pregame;    /* waiting for PAGE_START to flush settings */
  bool pending_game;       /* waiting for PAGE_GAME to edit-mode + wipe */
  int width;
  int height;
} g_newedit;

static void on_pick_height(int value, void *ud)
{
  (void)ud;
  if (value < 20 || value > 200) {
    sr_say(_("Canceled (height out of range)."), 1);
    return;
  }
  g_newedit.height = value;
  g_newedit.pending_pregame = TRUE;
  g_newedit.pending_game = TRUE;
  sr_say(_("Starting local server for editor."), 1);
  client_start_server_and_set_page(PAGE_START);
}

static void on_pick_width(int value, void *ud)
{
  (void)ud;
  if (value < 40 || value > 200) {
    sr_say(_("Canceled (width out of range)."), 1);
    return;
  }
  g_newedit.width = value;
  sr_say(_("Map height popup open. Enter value 20 to 200 (default 50)."), 1);
  sr_pick_int(_("Map height (20-200)"), 50, 20, 200,
              on_pick_height, NULL);
}

void sr_mapedit_newedit(void)
{
  memset(&g_newedit, 0, sizeof(g_newedit));
  sr_say(_("Map width popup open. Enter value 40 to 200 (default 80). "
           "Or cancel and type 'newedit W H' directly."), 1);
  sr_pick_int(_("Map width (40-200)"), 80, 40, 200,
              on_pick_width, NULL);
}

void sr_mapedit_newedit_direct(int width, int height)
{
  char buf[120];

  if (width < 40 || width > 200) {
    sr_say(_("Width must be 40 to 200."), 1);
    return;
  }
  if (height < 20 || height > 200) {
    sr_say(_("Height must be 20 to 200."), 1);
    return;
  }
  memset(&g_newedit, 0, sizeof(g_newedit));
  g_newedit.width = width;
  g_newedit.height = height;
  g_newedit.pending_pregame = TRUE;
  g_newedit.pending_game = TRUE;
  fc_snprintf(buf, sizeof(buf),
              _("newedit %dx%d: starting local server."), width, height);
  sr_say(buf, 1);
  client_start_server_and_set_page(PAGE_START);
}

void sr_mapedit_flush_at_pregame_if_pending(void)
{
  char buf[80];

  if (!g_newedit.pending_pregame) {
    return;
  }
  g_newedit.pending_pregame = FALSE;

  /* Push settings via server /set commands rather than the desired-
     options channel. The latter silently drops options the server
     hasn't sent the "initial" packet for yet (options.c:5788), which
     is exactly the state we're in at PAGE_START. Chat /set is
     synchronous on the server and applies immediately. */
  send_chat("/set mapsize XYSIZE");           /* use xsize/ysize directly */
  fc_snprintf(buf, sizeof(buf), "/set xsize %d", g_newedit.width);
  send_chat(buf);
  fc_snprintf(buf, sizeof(buf), "/set ysize %d", g_newedit.height);
  send_chat(buf);
  send_chat("/set aifill 0");                 /* no AI opponents */

  sr_say(_("Editor pregame. Type begin to start with ocean map."), 1);
  sr_log_append("newedit: applied mapsize/xsize/ysize/aifill, awaiting /start.");
}

void sr_mapedit_flush_at_game_if_pending(void)
{
  int units_removed = 0;

  if (!g_newedit.pending_game) {
    return;
  }
  g_newedit.pending_game = FALSE;

  /* Flip edit mode on so subsequent edit packets are accepted. */
  dsend_packet_edit_mode(&client.conn, TRUE);

  /* Tabula rasa: delete every unit on the map before we change the
     terrain underneath them. Without this, oceanize triggers a flood
     of "Moved due to changing terrain" / "Disbanded ..." messages as
     the server tries to relocate starting units off drowned land.
     Deleting first keeps the log clean and leaves the user with a
     truly empty canvas. */
  players_iterate(pplayer) {
    unit_list_iterate_safe(pplayer->units, punit) {
      dsend_packet_edit_unit_remove_by_id(&client.conn, punit->id);
      units_removed++;
    } unit_list_iterate_safe_end;
  } players_iterate_end;

  oceanize_map();

  if (units_removed > 0) {
    char buf[80];

    fc_snprintf(buf, sizeof(buf),
                _("Removed %d starting unit(s) for empty canvas."),
                units_removed);
    sr_log_append(buf);
  }
  sr_say(_("Ocean map ready. Use terrain, extra, savescen."), 1);
  sr_log_append("newedit: edit mode on, units cleared, map oceanized.");
}

bool sr_mapedit_is_newedit_pending(void)
{
  return g_newedit.pending_pregame || g_newedit.pending_game;
}

#else /* !_WIN32 */

void sr_mapedit_toggle(void) {}
void sr_mapedit_terrain(void) {}
void sr_mapedit_extra(void) {}
void sr_mapedit_paint_by_rule(const char *rule_name) { (void)rule_name; }
void sr_mapedit_oceanize(void) {}
void sr_mapedit_savescen(void) {}
void sr_mapedit_newedit(void) {}
void sr_mapedit_newedit_direct(int w, int h) { (void)w; (void)h; }
void sr_mapedit_flush_at_pregame_if_pending(void) {}
void sr_mapedit_flush_at_game_if_pending(void) {}
bool sr_mapedit_is_newedit_pending(void) { return FALSE; }

#endif /* _WIN32 */
