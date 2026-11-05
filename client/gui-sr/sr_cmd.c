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

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* dependencies/tinycthread */
#include "fc_tinycthread.h"

/* utility */
#include "fcintl.h"
#include "support.h"              /* fc_snprintf */

/* common */
#include "actions.h"              /* unit_can_add_or_build_city */
#include "base.h"                 /* get_base_by_gui_type, base_extra_get */
#include "extras.h"               /* next_extra_for_tile, EC_*, ERM_* */
#include "fc_types.h"             /* ACTIVITY_*, activity flags */
#include "game.h"                 /* game_unit_by_number */
#include "map.h"                  /* mapstep, real_map_distance */
#include "movement.h"             /* is_native_tile */
#include "terrain.h"              /* is_ocean_tile */
#include "tile.h"
#include "unit.h"                 /* can_unit_do_activity_client, unit_has_type_flag */
#include "world_object.h"         /* wld */

/* client */
#include "chatline_common.h"      /* send_chat */
#include "client_main.h"          /* start_quitting */
#include "connectdlg_common.h"    /* client_start_server */
#include "control.h"              /* key_end_turn, head_of_units_in_focus */
#include "goto.h"                 /* send_goto_tile */
#include "mapview_common.h"       /* center_tile_mapcanvas */
#include "pages_g.h"              /* set_client_page, client_start_server_and_set_page */
#include "dialogs.h"              /* popup_revolution_dialog */
#include "finddlg.h"              /* popup_find_dialog */
#include "gotodlg.h"              /* popup_goto_dialog */
#include "infradlg.h"             /* infra_dialog_popup */
#include "voteinfo.h"             /* voteinfo_do_vote, voteinfo_queue_get_current */
#include "messagedlg.h"           /* popup_messageopt_dialog */
#include "plrdlg.h"               /* popup_players_dialog */
#include "ratesdlg.h"             /* popup_rates_dialog, popup_multiplier_dialog */
#include "wldlg.h"                /* popup_worklists_report */
#include "text.h"                 /* unit_description */

/* gui-sr */
#include "sr_audio.h"
#include "sr_citizens.h"
#include "sr_citydlg.h"
#include "sr_citylist.h"
#include "sr_cma.h"
#include "sr_profile.h"
#include "sr_diplodlg.h"
#include "sr_economydlg.h"
#include "sr_happiness.h"
#include "sr_inteldlg.h"
#include "sr_log.h"
#include "sr_pick.h"              /* sr_pick_enum */
#include "sr_loadgame.h"
#include "sr_mapedit.h"
#include "sr_newgame.h"
#include "sr_optdlg.h"
#include "sr_rallypoint.h"
#include "sr_sciencedlg.h"
#include "sr_speech.h"
#include "sr_tile.h"
#include "sr_tree.h"
#include "sr_unitsdlg.h"

/* common */
#include "city.h"

#include "sr_cmd.h"

#define SR_LINE_MAX   512
#define SR_QUEUE_MAX  32

struct cmd_queue {
  char lines[SR_QUEUE_MAX][SR_LINE_MAX];
  int head, tail;
  mtx_t mutex;
};

static struct cmd_queue q;
static thrd_t reader_thread;
static int reader_started = 0;

/* Review-cursor state. NULL = review mode off; otherwise the virtual
   cursor tile, decoupled from unit focus, used for exploring the map
   without moving any unit. */
static struct tile *review_cursor = NULL;

/* Goto-cursor state. NULL = goto mode off; otherwise the tentative
   destination tile for the selected unit. Direction keys move it;
   Enter commits via send_goto_tile; Escape cancels. */
static struct tile *goto_cursor = NULL;

/* Default tile readout verbosity. FALSE = thorough (flattened
   popup_info_text), TRUE = terse one-liner. Toggled via
   "terse" / "verbose" commands. */
static bool terse_tile = FALSE;

struct tile *sr_cmd_target_tile(void)
{
  struct unit *punit;

  if (review_cursor != NULL) {
    return review_cursor;
  }
  /* get_num_units_in_focus() returns 0 when current_focus is NULL (pregame),
     avoiding the genlist NULL-assertion that head_of_units_in_focus() would
     trip. */
  if (get_num_units_in_focus() <= 0) {
    return NULL;
  }
  punit = head_of_units_in_focus();
  if (punit != NULL) {
    return unit_tile(punit);
  }
  return NULL;
}

static struct tile *current_target_tile(void)
{
  return sr_cmd_target_tile();
}

static void leave_review_mode(bool announce)
{
  if (review_cursor != NULL) {
    review_cursor = NULL;
    sr_tree_rebuild_review(NULL);
    if (announce) {
      sr_say(_("Review off."), 1);
    }
  }
}

static void leave_goto_mode(bool announce)
{
  if (goto_cursor != NULL) {
    goto_cursor = NULL;
    if (announce) {
      sr_say(_("Goto cancelled."), 1);
    }
  }
}

int sr_cmd_is_goto_active(void)
{
  return goto_cursor != NULL;
}

static int reader_fn(void *arg)
{
  char buf[SR_LINE_MAX];

  (void)arg;
  while (fgets(buf, sizeof(buf), stdin) != NULL) {
    size_t len = strlen(buf);
    int next;

    while (len > 0 && (buf[len - 1] == '\n' || buf[len - 1] == '\r')) {
      buf[--len] = '\0';
    }
    mtx_lock(&q.mutex);
    next = (q.tail + 1) % SR_QUEUE_MAX;
    if (next != q.head) {
      strncpy(q.lines[q.tail], buf, SR_LINE_MAX - 1);
      q.lines[q.tail][SR_LINE_MAX - 1] = '\0';
      q.tail = next;
    }
    mtx_unlock(&q.mutex);
  }
  return 0;
}

void sr_cmd_init(void)
{
  if (reader_started) {
    return;
  }
  q.head = q.tail = 0;
  mtx_init(&q.mutex, mtx_plain);
  if (thrd_create(&reader_thread, reader_fn, NULL) == thrd_success) {
    reader_started = 1;
    sr_say("Type help for commands. Input is line-buffered.", 0);
  } else {
    fprintf(stderr, "[sr] failed to start stdin reader thread.\n");
  }
}

static int str_eq(const char *a, const char *b)
{
  return strcmp(a, b) == 0;
}

static void say_current_unit(void)
{
  struct unit *punit;

  if (get_num_units_in_focus() <= 0) {
    sr_say("No unit selected.", 1);
    return;
  }
  punit = head_of_units_in_focus();
  if (punit == NULL) {
    sr_say("No unit selected.", 1);
  } else {
    const char *desc = unit_description(punit);
    sr_say(desc != NULL ? desc : "Unit selected.", 1);
  }
}

static void cmd_help(void)
{
  sr_say("Game: start, begin, end, quit. Unit: unit, next, prev, done, wait, "
         "fortify, sentry, wake, build, road, irrigate, mine, explore, auto, "
         "pillage, disband, upgrade, home, center, cancel. Move: n, ne, e, se, "
         "s, sw, w, nw. Map: tile (t) describes current tile; where (w) "
         "speaks coordinates; look (l) enters review mode so direction keys "
         "move a virtual cursor instead of the unit; jump (j) centers camera "
         "and exits review; cancel also exits review. Verbosity: terse, "
         "verbose (default). Reports: demographics (demo), topcities (top), "
         "wonders, achievements. Combat extras: patrol, paradrop, transform, "
         "clean, fortress, airbase, unloadall, unsentry, action (do). Select: "
         "seltile, selcont, selall. Battle groups: bg1..bg4 recall, bgsave1.."
         "bgsave4 assign. Cities: findcity (find) centers the map on a known "
         "city; gotocity (gc) sends focus units there. Voting: voteyes (yes),"
         " voteno (no), voteabstain (abstain) act on the current server vote."
         " Text: chat TEXT, server TEXT, slash for raw server command. "
         "Debug: say TEXT.", 1);
}

struct dir_entry {
  const char *word;
  enum direction8 dir;
};
static const struct dir_entry dir_table[] = {
  { "n",  DIR8_NORTH     }, { "north",     DIR8_NORTH     },
  { "ne", DIR8_NORTHEAST }, { "northeast", DIR8_NORTHEAST },
  { "e",  DIR8_EAST      }, { "east",      DIR8_EAST      },
  { "se", DIR8_SOUTHEAST }, { "southeast", DIR8_SOUTHEAST },
  { "s",  DIR8_SOUTH     }, { "south",     DIR8_SOUTH     },
  { "sw", DIR8_SOUTHWEST }, { "southwest", DIR8_SOUTHWEST },
  { "w",  DIR8_WEST      }, { "west",      DIR8_WEST      },
  { "nw", DIR8_NORTHWEST }, { "northwest", DIR8_NORTHWEST },
};

enum dir_mode { DIR_STEP, DIR_FAR, DIR_SAY };

/* Safety cap for edge-jump loop. No Freeciv map exceeds a few thousand
   tiles along any axis; 8000 is a comfortable ceiling. */
#define DIR_FAR_MAX_STEPS 8000

/* Speak a "can't go that way" message that distinguishes
   edge-of-map from direction-invalid-for-this-topology (e.g. hex
   maps don't support NE/SW). */
static void say_blocked_dir(const char *suffix, enum direction8 dir,
                            struct tile *ref)
{
  char buf[160];

  if (!is_valid_dir(dir)) {
    fc_snprintf(buf, sizeof(buf),
                _("Direction %s not valid on this map topology."),
                suffix);
  } else if (ref != NULL) {
    int x = tile_index(ref) % wld.map.xsize;
    int y = tile_index(ref) / wld.map.xsize;

    fc_snprintf(buf, sizeof(buf),
                _("Edge of map — cannot go %s from %d, %d."),
                suffix, x, y);
  } else {
    fc_snprintf(buf, sizeof(buf), _("Cannot go %s from here."), suffix);
  }
  sr_say(buf, 1);
}

static int try_direction_mode(const char *suffix, enum dir_mode mode)
{
  size_t i;

  for (i = 0; i < sizeof(dir_table) / sizeof(dir_table[0]); i++) {
    if (str_eq(suffix, dir_table[i].word)) {
      enum direction8 dir = dir_table[i].dir;

      if (mode == DIR_SAY) {
        /* Peek one step in direction from the best-available reference
           tile. No state changes anywhere. */
        struct tile *ref;
        struct tile *peek;

        if (review_cursor != NULL) {
          ref = review_cursor;
        } else if (goto_cursor != NULL) {
          ref = goto_cursor;
        } else if (head_of_units_in_focus() != NULL) {
          ref = unit_tile(head_of_units_in_focus());
        } else {
          sr_say(_("No reference tile."), 1);
          return 1;
        }
        peek = mapstep(&(wld.map), ref, dir);
        if (peek == NULL) {
          say_blocked_dir(suffix, dir, ref);
        } else {
          sr_tile_say(peek, terse_tile);
        }
        return 1;
      }

      if (mode == DIR_FAR) {
        if (review_cursor != NULL) {
          struct tile *cur = review_cursor;
          int steps = 0;

          while (steps < DIR_FAR_MAX_STEPS) {
            struct tile *next = mapstep(&(wld.map), cur, dir);

            if (next == NULL) { break; }
            cur = next;
            steps++;
          }
          review_cursor = cur;
          center_tile_mapcanvas(review_cursor);
          sr_tree_rebuild_review(review_cursor);
          sr_tile_say(review_cursor, terse_tile);
        } else if (goto_cursor != NULL) {
          struct tile *cur = goto_cursor;
          int steps = 0;

          while (steps < DIR_FAR_MAX_STEPS) {
            struct tile *next = mapstep(&(wld.map), cur, dir);

            if (next == NULL) { break; }
            cur = next;
            steps++;
          }
          goto_cursor = cur;
          center_tile_mapcanvas(goto_cursor);
          sr_tile_say(goto_cursor, terse_tile);
        } else if (head_of_units_in_focus() == NULL) {
          sr_say("No unit selected.", 1);
        } else {
          /* Ask the server pathfinder to route the focused unit as far
             as possible in the requested direction. */
          struct unit *punit = head_of_units_in_focus();
          struct tile *cur = unit_tile(punit);
          int steps = 0;

          while (steps < DIR_FAR_MAX_STEPS) {
            struct tile *next = mapstep(&(wld.map), cur, dir);

            if (next == NULL) { break; }
            cur = next;
            steps++;
          }
          if (cur == unit_tile(punit)) {
            sr_say(_("Edge of map."), 1);
          } else if (send_goto_tile(punit, cur)) {
            sr_say(_("Going to edge."), 1);
          } else {
            sr_say(_("No path to edge."), 1);
          }
        }
        return 1;
      }

      /* DIR_STEP — unchanged legacy behaviour. */
      if (review_cursor != NULL) {
        struct tile *next = mapstep(&(wld.map), review_cursor, dir);

        if (next == NULL) {
          say_blocked_dir(suffix, dir, review_cursor);
        } else {
          review_cursor = next;
          center_tile_mapcanvas(review_cursor);
          sr_tree_rebuild_review(review_cursor);
          sr_tile_say(review_cursor, terse_tile);
        }
      } else if (goto_cursor != NULL) {
        struct tile *next = mapstep(&(wld.map), goto_cursor, dir);

        if (next == NULL) {
          say_blocked_dir(suffix, dir, goto_cursor);
        } else {
          goto_cursor = next;
          center_tile_mapcanvas(goto_cursor);
          sr_tile_say(goto_cursor, terse_tile);
        }
      } else if (head_of_units_in_focus() == NULL) {
        sr_say("No unit selected.", 1);
      } else {
        key_unit_move(dir);
      }
      return 1;
    }
  }
  return 0;
}

static int try_direction(const char *head)
{
  if (strncmp(head, "far-", 4) == 0) {
    return try_direction_mode(head + 4, DIR_FAR);
  }
  if (strncmp(head, "say-", 4) == 0) {
    return try_direction_mode(head + 4, DIR_SAY);
  }
  return try_direction_mode(head, DIR_STEP);
}

/* Zero-argument handlers that need a focused unit. */
static void handle_wake(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit != NULL) {
    request_unit_wakeup(punit);
  } else {
    sr_say("No unit selected.", 1);
  }
}

static int g_disband_target_id = 0;
static const char *disband_opts[2];

static void disband_on_confirm(int idx, const char *v, void *ud)
{
  struct unit *punit;

  (void) v; (void) ud;
  punit = g_disband_target_id > 0 ? game_unit_by_number(g_disband_target_id)
                                  : NULL;
  g_disband_target_id = 0;
  if (idx != 0) {
    sr_say(_("Disband cancelled."), 1);
    return;
  }
  if (punit == NULL || punit != head_of_units_in_focus()) {
    sr_say(_("Disband cancelled — unit no longer selected."), 1);
    return;
  }
  request_unit_disband(punit);
  sr_say(_("Disbanded."), 1);
}

static void handle_disband(void)
{
  struct unit *punit = head_of_units_in_focus();
  char title[128];

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  g_disband_target_id = punit->id;
  disband_opts[0] = _("Yes, disband");
  disband_opts[1] = _("No, keep");
  fc_snprintf(title, sizeof(title), _("Disband %s?"),
              unit_name_translation(punit));
  sr_say(title, 1);
  /* Default cursor on "No" so an accidental Enter does nothing. */
  sr_pick_enum(title, disband_opts, 2, 1,
               disband_on_confirm, NULL);
}

static void handle_upgrade(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit != NULL) {
    request_unit_upgrade(punit);
  } else {
    sr_say("No unit selected.", 1);
  }
}

static void handle_next(void)
{
  unit_focus_advance(TRUE);
}

static void handle_tile(void)
{
  sr_tile_say(current_target_tile(), terse_tile);
}

static void handle_where(void)
{
  sr_tile_say_where(current_target_tile());
}

static void handle_look(void)
{
  if (review_cursor != NULL) {
    leave_review_mode(TRUE);
    return;
  }
  {
    struct unit *punit = head_of_units_in_focus();
    struct tile *start = punit != NULL ? unit_tile(punit)
                                       : get_center_tile_mapcanvas();

    if (start == NULL) {
      sr_say(_("Nothing to look at."), 1);
      return;
    }
    review_cursor = start;
    sr_tree_rebuild_review(review_cursor);
    sr_say(_("Review mode."), 1);
    sr_tile_say(review_cursor, terse_tile);
  }
}

static void handle_jump(void)
{
  if (review_cursor == NULL) {
    sr_say(_("Not in review mode."), 1);
    return;
  }
  center_tile_mapcanvas(review_cursor);
  sr_say(_("Centered."), 1);
  leave_review_mode(FALSE);
}

static void handle_terse(void)
{
  terse_tile = TRUE;
  sr_say(_("Terse tile readout."), 1);
}

static void handle_verbose(void)
{
  terse_tile = FALSE;
  sr_say(_("Thorough tile readout."), 1);
}

void sr_cmd_toggle_terse(void)
{
  if (terse_tile) {
    handle_verbose();
  } else {
    handle_terse();
  }
}

static void handle_cancel(void)
{
  if (review_cursor != NULL) {
    leave_review_mode(TRUE);
    return;
  }
  if (goto_cursor != NULL) {
    leave_goto_mode(TRUE);
    return;
  }
  key_cancel_action();
}

static void handle_goto(void)
{
  struct unit *punit;

  if (review_cursor != NULL) {
    leave_review_mode(FALSE);
  }
  if (get_num_units_in_focus() <= 0) {
    sr_say(_("No unit selected for goto."), 1);
    return;
  }
  punit = head_of_units_in_focus();
  if (punit == NULL) {
    sr_say(_("No unit selected for goto."), 1);
    return;
  }
  goto_cursor = unit_tile(punit);
  sr_say(_("Goto mode. Arrow keys to aim, Enter to commit, Escape to cancel."), 0);
  if (goto_cursor != NULL) {
    sr_tile_say(goto_cursor, terse_tile);
  }
}

static void handle_goto_commit(void)
{
  struct unit *punit;

  if (goto_cursor == NULL) {
    return;
  }
  punit = head_of_units_in_focus();
  if (punit == NULL) {
    sr_say(_("No unit selected for goto."), 1);
    leave_goto_mode(FALSE);
    return;
  }
  if (send_goto_tile(punit, goto_cursor)) {
    sr_say(_("Goto committed."), 1);
  } else if (is_ocean_tile(goto_cursor)
             && !is_native_tile(unit_type_get(punit), goto_cursor)) {
    sr_say(_("Cannot route: this unit cannot reach ocean tiles. Board a"
             " transport or pick a land destination."), 1);
  } else if (!is_ocean_tile(goto_cursor)
             && is_ocean_tile(unit_tile(punit))
             && !is_native_tile(unit_type_get(punit), unit_tile(punit))) {
    /* Unit is on a transport, destination is land — no path usually
       means no valid unload tile along the way. */
    sr_say(_("No path: no valid unload tile in reach."), 1);
  } else {
    sr_say(_("Goto rejected: no path. A transport may be needed to cross"
             " water."), 1);
  }
  leave_goto_mode(FALSE);
}

static void handle_research(void)
{
  sr_sciencedlg_open();
}

static void handle_report_request(enum report_type type, const char *label)
{
  char buf[96];

  if (client_player() == NULL) {
    sr_say(_("Not in a game."), 1);
    return;
  }
  send_report_request(type);
  fc_snprintf(buf, sizeof(buf), _("Requested %s report."), label);
  sr_say(buf, 1);
}

static void handle_demographics(void)
{
  handle_report_request(REPORT_DEMOGRAPHIC, _("demographics"));
}

static void handle_topcities(void)
{
  handle_report_request(REPORT_TOP_CITIES, _("top cities"));
}

static void handle_wonders(void)
{
  handle_report_request(REPORT_WONDERS_OF_THE_WORLD, _("wonders of the world"));
}

static void handle_achievements(void)
{
  handle_report_request(REPORT_ACHIEVEMENTS, _("achievements"));
}

/* ---- Phase 7b: new unit-action verbs ---------------------------------- */

static int require_focus_unit(void)
{
  if (head_of_units_in_focus() == NULL) {
    sr_say(_("No unit selected."), 1);
    return 0;
  }
  return 1;
}

static void handle_patrol(void)        { if (require_focus_unit()) key_unit_patrol(); }
static void handle_paradrop(void)      { if (require_focus_unit()) key_unit_paradrop(); }
static void handle_unload_all(void)    { if (require_focus_unit()) key_unit_unload_all(); }
static void handle_unsentry_all(void)  { if (require_focus_unit()) key_unit_wakeup_others(); }
static void handle_action_do(void)     { if (require_focus_unit()) key_unit_action_select_tgt(); }

/* ---- Activity actions with client-side pre-check + speech ----------- */

/* Try to start an "extras" activity (road, irrigation, mine) whose
   concrete target depends on the tile. Speak the refusal/success
   reason so the player always hears something. */
static void do_extra_activity(enum unit_activity act, enum extra_cause cause,
                              const char *refusal, const char *success)
{
  struct unit *punit = head_of_units_in_focus();
  struct extra_type *tgt;

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  tgt = next_extra_for_tile(unit_tile(punit), cause,
                            unit_owner(punit), punit);
  if (tgt == NULL
      || !can_unit_do_activity_targeted_client(punit, act, tgt)) {
    sr_say(refusal, 1);
    return;
  }
  request_new_unit_activity_targeted(punit, act, tgt);
  sr_say(success, 1);
}

static void handle_road(void)
{
  do_extra_activity(ACTIVITY_GEN_ROAD, EC_ROAD,
                    _("Cannot build a road here."),
                    _("Building road."));
}

static void handle_irrigate(void)
{
  do_extra_activity(ACTIVITY_IRRIGATE, EC_IRRIGATION,
                    _("Cannot irrigate here."),
                    _("Irrigating."));
}

static void handle_mine(void)
{
  do_extra_activity(ACTIVITY_MINE, EC_MINE,
                    _("Cannot mine here."),
                    _("Mining."));
}

static void handle_clean(void)
{
  struct unit *punit = head_of_units_in_focus();
  struct extra_type *tgt;

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  tgt = prev_extra_in_tile(unit_tile(punit), ERM_CLEAN,
                           unit_owner(punit), punit);
  if (tgt == NULL
      || !can_unit_do_activity_targeted_client(punit, ACTIVITY_CLEAN, tgt)) {
    sr_say(_("Nothing to clean here."), 1);
    return;
  }
  request_new_unit_activity_targeted(punit, ACTIVITY_CLEAN, tgt);
  sr_say(_("Cleaning."), 1);
}

static void handle_pillage(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  if (!can_unit_do_activity_client(punit, ACTIVITY_PILLAGE)) {
    sr_say(_("Cannot pillage here."), 1);
    return;
  }
  request_unit_pillage(punit);
  sr_say(_("Pillaging."), 1);
}

static void handle_transform(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  if (!can_unit_do_activity_client(punit, ACTIVITY_TRANSFORM)) {
    sr_say(_("Cannot transform terrain here."), 1);
    return;
  }
  request_new_unit_activity(punit, ACTIVITY_TRANSFORM);
  sr_say(_("Transforming terrain."), 1);
}

static void do_base_activity(enum base_gui_type gui_type,
                             const char *refusal, const char *success)
{
  struct unit *punit = head_of_units_in_focus();
  struct base_type *pbase;
  struct extra_type *pextra;

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  pbase = get_base_by_gui_type(gui_type, punit, unit_tile(punit));
  if (pbase == NULL) { sr_say(refusal, 1); return; }
  pextra = base_extra_get(pbase);
  if (pextra == NULL
      || !can_unit_do_activity_targeted_client(punit, ACTIVITY_BASE, pextra)) {
    sr_say(refusal, 1);
    return;
  }
  request_new_unit_activity_targeted(punit, ACTIVITY_BASE, pextra);
  sr_say(success, 1);
}

static void handle_fortress(void)
{
  do_base_activity(BASE_GUI_FORTRESS,
                   _("Cannot build a fortress here."),
                   _("Building fortress."));
}

static void handle_airbase(void)
{
  do_base_activity(BASE_GUI_AIRBASE,
                   _("Cannot build an airbase here."),
                   _("Building airbase."));
}

/* Fortify / sentry get spoken confirmation so the player hears
   something after pressing the key. Both accept if the unit is
   idle-enough; the pre-check mirrors the server's. */
static void handle_fortify_speak(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  if (!can_unit_do_activity_client(punit, ACTIVITY_FORTIFYING)) {
    sr_say(_("Cannot fortify here."), 1);
    return;
  }
  request_new_unit_activity(punit, ACTIVITY_FORTIFYING);
  sr_say(_("Fortifying."), 1);
}

static void handle_sentry_speak(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  if (!can_unit_do_activity_client(punit, ACTIVITY_SENTRY)) {
    sr_say(_("Cannot sentry here."), 1);
    return;
  }
  request_new_unit_activity(punit, ACTIVITY_SENTRY);
  sr_say(_("Sentry set."), 1);
}

/* Build city: gate on unit_can_add_or_build_city so non-settlers
   hear a clear refusal instead of silent no-op. */
static void handle_build(void)
{
  struct unit *punit = head_of_units_in_focus();

  if (punit == NULL) { sr_say(_("No unit selected."), 1); return; }
  if (!unit_can_add_or_build_city(&(wld.map), punit)) {
    sr_say(_("This unit cannot build a city."), 1);
    return;
  }
  key_unit_build_city();
}
static void handle_findcity(void)      { popup_find_dialog(); }
static void handle_gotocity(void)      { if (require_focus_unit()) popup_goto_dialog(); }

/* Smart `H`: if unit sits on a player-owned city, rehome there;
   otherwise find the nearest own city and goto. Civ-II convention. */
static void handle_home(void)
{
  struct unit *punit;
  struct player *pl;
  struct city *pcity_here;
  struct city *nearest = NULL;
  int best_dist = 0;
  char buf[160];

  if (!require_focus_unit()) { return; }
  punit = head_of_units_in_focus();
  pl = unit_owner(punit);

  pcity_here = tile_city(unit_tile(punit));
  if (pcity_here != NULL && city_owner(pcity_here) == pl) {
    key_unit_homecity();
    fc_snprintf(buf, sizeof(buf), _("Rehomed to %s."),
                city_name_get(pcity_here));
    sr_say(buf, 1);
    return;
  }

  if (pl == NULL || city_list_size(pl->cities) == 0) {
    sr_say(_("No cities to go to."), 1);
    return;
  }
  city_list_iterate(pl->cities, c) {
    int d = real_map_distance(unit_tile(punit), city_tile(c));

    if (nearest == NULL || d < best_dist) {
      nearest = c;
      best_dist = d;
    }
  } city_list_iterate_end;
  if (nearest == NULL) {
    sr_say(_("No cities to go to."), 1);
    return;
  }
  if (send_goto_tile(punit, city_tile(nearest))) {
    fc_snprintf(buf, sizeof(buf),
                _("Going to nearest city %s, %d tiles away."),
                city_name_get(nearest), best_dist);
    sr_say(buf, 1);
  } else {
    fc_snprintf(buf, sizeof(buf),
                _("No path to nearest city %s."),
                city_name_get(nearest));
    sr_say(buf, 1);
  }
}

static void cast_vote(enum client_vote_type kind, const char *spoken)
{
  struct voteinfo *vi = voteinfo_queue_get_current(NULL);

  if (vi == NULL) {
    sr_say(_("No vote in progress."), 1);
    return;
  }
  voteinfo_do_vote(vi->vote_no, kind);
  sr_say(spoken, 1);
}
static void handle_vote_yes(void)     { cast_vote(CVT_YES,     _("Voted yes."));     }
static void handle_vote_no(void)      { cast_vote(CVT_NO,      _("Voted no."));      }
static void handle_vote_abstain(void) { cast_vote(CVT_ABSTAIN, _("Abstained."));     }

static void do_select_same(enum unit_select_location_mode loc)
{
  if (!require_focus_unit()) { return; }
  request_unit_select(get_units_in_focus(), SELTYPE_SAME, loc);
}
static void handle_seltile(void) { do_select_same(SELLOC_TILE); }
static void handle_selcont(void) { do_select_same(SELLOC_CONT); }
static void handle_selall(void)  { do_select_same(SELLOC_WORLD); }

static void do_bg_select(int bg) { if (require_focus_unit()) key_unit_select_battlegroup(bg, FALSE); }
static void do_bg_assign(int bg) { if (require_focus_unit()) key_unit_assign_battlegroup(bg, FALSE); }
static void handle_bg1(void)     { do_bg_select(0); }
static void handle_bg2(void)     { do_bg_select(1); }
static void handle_bg3(void)     { do_bg_select(2); }
static void handle_bg4(void)     { do_bg_select(3); }
static void handle_bgsave1(void) { do_bg_assign(0); }
static void handle_bgsave2(void) { do_bg_assign(1); }
static void handle_bgsave3(void) { do_bg_assign(2); }
static void handle_bgsave4(void) { do_bg_assign(3); }

static void handle_nations(void)
{
  sr_diplodlg_open();
}

static void handle_economy(void)
{
  sr_economydlg_open();
}

static void handle_units_rep(void)
{
  sr_unitsdlg_open();
}

static void handle_options(void)
{
  sr_optdlg_open();
}

static void handle_sound(void)
{
  sr_audio_open();
}

static void handle_soundtags(void)
{
  sr_audio_tag_inspector_open();
}

static void handle_soundaudit(void)
{
  sr_audio_audit();
}

/* Split `rest` into a leading word (NAME) and the remainder (VALUE),
   writing trimmed NAME into `name_out`. Returns the start of VALUE
   inside `rest` (possibly ""); NULL if no NAME present. */
static const char *split_word(const char *rest, char *name_out,
                              size_t name_sz)
{
  size_t i = 0;

  if (rest == NULL) { name_out[0] = '\0'; return NULL; }
  while (*rest != '\0' && isspace((unsigned char) *rest)) { rest++; }
  if (*rest == '\0') { name_out[0] = '\0'; return NULL; }
  while (*rest != '\0' && !isspace((unsigned char) *rest)
         && i + 1 < name_sz) {
    name_out[i++] = *rest++;
  }
  name_out[i] = '\0';
  while (*rest != '\0' && isspace((unsigned char) *rest)) { rest++; }
  return rest;
}

static void handle_setopt(const char *rest)
{
  char name[96];
  const char *value;

  value = split_word(rest, name, sizeof(name));
  if (value == NULL) {
    sr_say(_("Usage: setopt NAME VALUE"), 1);
    return;
  }
  sr_profile_setopt(name, value);
}

static void handle_saveprofile(const char *rest)
{
  char name[96];

  (void) split_word(rest, name, sizeof(name));
  if (name[0] == '\0') {
    sr_say(_("Usage: saveprofile NAME"), 1);
    return;
  }
  sr_profile_save(name);
}

static void handle_loadprofile(const char *rest)
{
  char name[96];

  (void) split_word(rest, name, sizeof(name));
  if (name[0] == '\0') {
    sr_say(_("Usage: loadprofile NAME"), 1);
    return;
  }
  sr_profile_load(name);
}

static void handle_listprofiles(void)
{
  sr_profile_list();
}

static void handle_soundscaffold(const char *rest)
{
  char path[512];

  if (rest == NULL || *rest == '\0') {
    sr_say(_("Usage: soundscaffold PATH — writes a .soundspec scaffold."), 1);
    return;
  }
  fc_strlcpy(path, rest, sizeof(path));
  if (sr_audio_scaffold_write(path)) {
    char msg[600];
    fc_snprintf(msg, sizeof(msg), _("Wrote scaffold to %s."), path);
    sr_say(msg, 1);
    sr_log_append(msg);
  } else {
    char msg[600];
    fc_snprintf(msg, sizeof(msg), _("Could not write %s."), path);
    sr_say(msg, 1);
  }
}

static void handle_rates(void)
{
  popup_rates_dialog();
}

static void handle_policies(void)
{
  popup_multiplier_dialog();
}

static void handle_revolution(void)
{
  popup_revolution_dialog();
}

static void handle_worklists(void)
{
  popup_worklists_report();
}

/* Pick a city for the per-city dialogs (citizens / happiness / CMA).
   Prefer the city under the focused unit, then the unit's homecity,
   then the player's first city. NULL if there is none. */
static struct city *pick_dialog_city(void)
{
  struct player *pl = client_player();
  struct city *pcity = NULL;

  if (pl == NULL) {
    return NULL;
  }
  if (get_num_units_in_focus() > 0) {
    struct unit *punit = head_of_units_in_focus();

    if (punit != NULL) {
      pcity = tile_city(unit_tile(punit));
      if (pcity == NULL && punit->homecity > 0) {
        pcity = player_city_by_number(pl, punit->homecity);
      }
    }
  }
  if (pcity == NULL && city_list_size(pl->cities) > 0) {
    pcity = city_list_get(pl->cities, 0);
  }
  return pcity;
}

static void handle_citizens(void)
{
  struct city *pcity = pick_dialog_city();

  if (pcity == NULL) {
    sr_say("No city to inspect.", 1);
    return;
  }
  sr_citizens_open(pcity);
}

static void handle_happiness(void)
{
  struct city *pcity = pick_dialog_city();

  if (pcity == NULL) {
    sr_say("No city to inspect.", 1);
    return;
  }
  sr_happiness_open(pcity);
}

static void handle_cma(void)
{
  struct city *pcity = pick_dialog_city();

  if (pcity == NULL) {
    sr_say("No city to manage.", 1);
    return;
  }
  sr_cma_open(pcity);
}

static void handle_rally(void)
{
  sr_rallypoint_open();
}

static void handle_players(void)
{
  popup_players_dialog(FALSE);
}

static void handle_infra(void)
{
  infra_dialog_popup();
}

static void handle_msgopts(void)
{
  popup_messageopt_dialog();
}

static void handle_save(void)
{
  send_save_game(NULL);
  sr_log_append("Save requested.");
}

/* Intel needs a player; we offer the first alive non-self player we can
   find. Typically used from the Nations dialog which has a richer
   picker; the menu Ctrl+I path lands here as a fallback. */
static void handle_intel(void)
{
  struct player *me = client_player();
  struct player *target = NULL;

  if (me == NULL) {
    sr_say(_("Not in a game."), 1);
    return;
  }
  players_iterate_alive(pplayer) {
    if (pplayer != me && !is_barbarian(pplayer)) {
      target = pplayer;
      break;
    }
  } players_iterate_alive_end;

  if (target == NULL) {
    sr_say(_("No other nations to inspect."), 1);
    return;
  }
  sr_inteldlg_open(target);
}

static void handle_cities(void)
{
  struct player *pl = client_player();
  int n = (pl != NULL) ? city_list_size(pl->cities) : 0;

  if (n == 0) {
    sr_say(_("No cities to show."), 1);
  } else if (n == 1) {
    sr_citydlg_open(city_list_get(pl->cities, 0));
  } else {
    sr_citylist_open();
  }
}

/* Open the City dialog for the most relevant city: the one at the
   current unit's tile, else the selected unit's home city, else the
   first owned city. Announce and bail if there is none. */
static void handle_viewcity(void)
{
  struct player *pl = client_player();
  struct city *pcity = NULL;

  if (pl == NULL) {
    sr_say(_("No game."), 1);
    return;
  }
  if (get_num_units_in_focus() > 0) {
    struct unit *punit = head_of_units_in_focus();

    if (punit != NULL) {
      pcity = tile_city(unit_tile(punit));
      if (pcity == NULL && punit->homecity > 0) {
        pcity = player_city_by_number(pl, punit->homecity);
      }
    }
  }
  if (pcity == NULL && city_list_size(pl->cities) > 0) {
    pcity = city_list_get(pl->cities, 0);
  }
  if (pcity == NULL) {
    sr_say(_("No city to show."), 1);
    return;
  }
  sr_citydlg_open(pcity);
}

struct simple_cmd {
  const char *word;
  const char *alias;
  void (*fn)(void);
};

static const struct simple_cmd simple_cmds[] = {
  { "next",    ".",  handle_next                   },
  { "prev",    ",",  key_recall_previous_focus_unit },
  { "done",    "d",  key_unit_done                 },
  { "wait",    NULL, key_unit_wait                 },
  { "fortify", "f",  handle_fortify_speak          },
  { "sentry",  NULL, handle_sentry_speak           },
  { "wake",    NULL, handle_wake                   },
  { "build",   "b",  handle_build                  },
  { "road",    NULL, handle_road                   },
  { "irrigate",NULL, handle_irrigate               },
  { "mine",    NULL, handle_mine                   },
  { "explore", "x",  key_unit_auto_explore         },
  { "auto",    NULL, key_unit_auto_settle          },
  { "pillage", NULL, handle_pillage                },
  { "disband", NULL, handle_disband                },
  { "upgrade", NULL, handle_upgrade                },
  { "home",    NULL, handle_home                   },
  { "center",  NULL, request_center_focus_unit     },
  { "cancel",  "esc", handle_cancel                 },
  { "tile",    "t",   handle_tile                   },
  { "look",    "l",   handle_look                   },
  { "jump",    "j",   handle_jump                   },
  { "where",   "w",   handle_where                  },
  { "terse",   NULL,  handle_terse                  },
  { "verbose", NULL,  handle_verbose                },
  { "viewcity", "v",  handle_viewcity               },
  { "research", NULL, handle_research               },
  { "cities",   NULL, handle_cities                 },
  { "nations",  "diplomacy", handle_nations          },
  { "economy",  NULL, handle_economy                },
  { "units",    NULL, handle_units_rep              },
  { "intel",    NULL, handle_intel                  },
  { "options",  NULL, handle_options                },
  { "sound",    "audio", handle_sound               },
  { "soundtags", "tags", handle_soundtags           },
  { "soundaudit", NULL, handle_soundaudit           },
  { "listprofiles", "profiles", handle_listprofiles  },
  { "rates",    NULL, handle_rates                  },
  { "policies", "multipliers", handle_policies       },
  { "revolution", "government", handle_revolution    },
  { "worklists", "wl", handle_worklists                },
  { "citizens", "pop", handle_citizens                  },
  { "happiness", "mood", handle_happiness               },
  { "cma",      NULL, handle_cma                        },
  { "rally",    NULL, handle_rally                      },
  { "players",  "pl",  handle_players                    },
  { "demographics", "demo", handle_demographics          },
  { "topcities", "top", handle_topcities                 },
  { "wonders",  NULL, handle_wonders                     },
  { "achievements", "achieve", handle_achievements       },
  { "infra",    NULL, handle_infra                      },
  { "msgopts",  "messageopts", handle_msgopts            },
  { "patrol",   NULL, handle_patrol                     },
  { "paradrop", NULL, handle_paradrop                   },
  { "transform", NULL, handle_transform                 },
  { "clean",    NULL, handle_clean                      },
  { "fortress", NULL, handle_fortress                   },
  { "airbase",  NULL, handle_airbase                    },
  { "unloadall", "unload", handle_unload_all            },
  { "unsentry", "wakeall", handle_unsentry_all          },
  { "action",   "do",  handle_action_do                 },
  { "findcity", "find", handle_findcity                 },
  { "gotocity", "gc",   handle_gotocity                 },
  { "voteyes",  "yes",  handle_vote_yes                 },
  { "voteno",   "no",   handle_vote_no                  },
  { "voteabstain", "abstain", handle_vote_abstain       },
  { "seltile",  "selecttile", handle_seltile            },
  { "selcont",  "selectcont", handle_selcont            },
  { "selall",   "selectall",  handle_selall             },
  { "bg1",      NULL, handle_bg1                        },
  { "bg2",      NULL, handle_bg2                        },
  { "bg3",      NULL, handle_bg3                        },
  { "bg4",      NULL, handle_bg4                        },
  { "bgsave1",  NULL, handle_bgsave1                    },
  { "bgsave2",  NULL, handle_bgsave2                    },
  { "bgsave3",  NULL, handle_bgsave3                    },
  { "bgsave4",  NULL, handle_bgsave4                    },
  { "save",     NULL, handle_save                   },
  { "goto",     "g",  handle_goto                   },
  { "goto-commit", NULL, handle_goto_commit         },
  { "editor",   "mapedit", sr_mapedit_toggle        },
  { "terrain",  "ter",     sr_mapedit_terrain       },
  { "extra",    NULL,      sr_mapedit_extra         },
  { "savescen", "saveas",  sr_mapedit_savescen      },
  { "newedit",  NULL,      sr_mapedit_newedit       },
  { "oceanize", NULL,      sr_mapedit_oceanize      },
};

/* Short letter commands that paint a specific terrain on the target
   tile, skipping the `terrain` picker. Rule names stay stable across
   rulesets (paint falls silent if the ruleset lacks the terrain). */
static const struct { const char *word; const char *rule_name; }
terrain_shortcuts[] = {
  { "tg",  "grassland" },
  { "tp",  "plains"    },
  { "to",  "ocean"     },
  { "tdo", "deep_ocean" },
  { "tl",  "lake"      },
  { "tm",  "mountains" },
  { "th",  "hills"     },
  { "tf",  "forest"    },
  { "tj",  "jungle"    },
  { "ts",  "swamp"     },
  { "td",  "desert"    },
  { "tt",  "tundra"    },
  { "tc",  "glacier"   },
};

static int try_terrain_shortcut(const char *head)
{
  size_t i;

  for (i = 0;
       i < sizeof(terrain_shortcuts) / sizeof(terrain_shortcuts[0]);
       i++) {
    if (str_eq(head, terrain_shortcuts[i].word)) {
      sr_mapedit_paint_by_rule(terrain_shortcuts[i].rule_name);
      return 1;
    }
  }
  return 0;
}

static int try_simple(const char *head)
{
  size_t i;

  for (i = 0; i < sizeof(simple_cmds) / sizeof(simple_cmds[0]); i++) {
    if (str_eq(head, simple_cmds[i].word)
        || (simple_cmds[i].alias != NULL && str_eq(head, simple_cmds[i].alias))) {
      simple_cmds[i].fn();
      return 1;
    }
  }
  return 0;
}

void sr_cmd_dispatch_line(const char *line)
{
  /* Skip leading whitespace. */
  while (*line != '\0' && isspace((unsigned char)*line)) {
    line++;
  }
  if (*line == '\0') {
    return;
  }

  /* Forward anything starting with '/' straight to the server. */
  if (line[0] == '/') {
    send_chat(line);
    return;
  }

  /* Split head command from rest. */
  const char *rest = line;
  char head[32];
  size_t i = 0;

  while (*rest != '\0' && !isspace((unsigned char)*rest) && i + 1 < sizeof(head)) {
    head[i++] = (char)tolower((unsigned char)*rest);
    rest++;
  }
  head[i] = '\0';
  while (*rest != '\0' && isspace((unsigned char)*rest)) {
    rest++;
  }

  if (str_eq(head, "help") || str_eq(head, "?")) {
    cmd_help();
  } else if (str_eq(head, "quit") || str_eq(head, "q") || str_eq(head, "exit")) {
    sr_say("Quitting.", 1);
    start_quitting();
  } else if (str_eq(head, "newgame")) {
    sr_newgame_open();
  } else if (str_eq(head, "load")) {
    sr_loadgame_open();
  } else if (str_eq(head, "start")) {
    sr_say("Starting local server.", 1);
    client_start_server_and_set_page(PAGE_START);
  } else if (str_eq(head, "begin") || str_eq(head, "go")) {
    sr_say("Requesting game start.", 1);
    /* CLI fallback: no dedicated packet exists for pregame /start;
       the server dispatches this through its stdin command handler. */
    send_chat("/start");
  } else if (str_eq(head, "end")) {
    sr_say("Ending turn.", 1);
    key_end_turn();
  } else if (str_eq(head, "unit") || str_eq(head, "u")) {
    say_current_unit();
  } else if (str_eq(head, "newedit") && *rest != '\0') {
    /* Direct form: `newedit W H` skips the width/height popups and
       spawns the server immediately. Useful when stdin input is the
       primary interaction model (popups require Alt-Tab). */
    int w = 0, h = 0;

    if (sscanf(rest, "%d %d", &w, &h) == 2) {
      sr_mapedit_newedit_direct(w, h);
    } else {
      sr_say("Usage: newedit W H  (width 40-200, height 20-200).", 1);
    }
  } else if (try_direction(head)) {
    /* handled */
  } else if (try_simple(head)) {
    /* handled */
  } else if (try_terrain_shortcut(head)) {
    /* handled */
  } else if (str_eq(head, "soundscaffold")) {
    handle_soundscaffold(rest);
  } else if (str_eq(head, "setopt")) {
    handle_setopt(rest);
  } else if (str_eq(head, "saveprofile")) {
    handle_saveprofile(rest);
  } else if (str_eq(head, "loadprofile")) {
    handle_loadprofile(rest);
  } else if (str_eq(head, "chat") || str_eq(head, "c")) {
    if (*rest != '\0') {
      send_chat(rest);
    }
  } else if (str_eq(head, "server") || str_eq(head, "s")) {
    /* Convenience: prepend a slash for server commands. */
    if (*rest != '\0') {
      char buf[SR_LINE_MAX];
      snprintf(buf, sizeof(buf), "/%s", rest);
      send_chat(buf);
    }
  } else if (str_eq(head, "say")) {
    if (*rest != '\0') {
      sr_say(rest, 1);
    }
  } else {
    sr_say("Unknown command. Type help.", 1);
  }
}

void sr_cmd_poll(void)
{
  for (;;) {
    char line[SR_LINE_MAX];

    mtx_lock(&q.mutex);
    if (q.head == q.tail) {
      mtx_unlock(&q.mutex);
      return;
    }
    strncpy(line, q.lines[q.head], SR_LINE_MAX);
    q.head = (q.head + 1) % SR_QUEUE_MAX;
    mtx_unlock(&q.mutex);

    sr_cmd_dispatch_line(line);
  }
}
