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
#include <string.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "support.h"

/* common */
#include "actions.h"
#include "city.h"
#include "extras.h"
#include "game.h"
#include "government.h"
#include "nation.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "style.h"
#include "tile.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "audio.h"
#include "chatline_common.h"    /* send_chat */
#include "client_main.h"
#include "climisc.h"
#include "control.h"
#include "mapview_common.h"      /* center_tile_mapcanvas */
#include "music.h"               /* sound_set_name / music_set_name globals */
#include "options.h"
#include "packhand.h"            /* set_government_choice, start_revolution */
#include "text.h"

/* gui main header */
#include "gui_sr.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"

#include "dialogs.h"

/* ====================================================================== */
/* Notification dialogs                                                   */
/* ====================================================================== */

/**********************************************************************//**
  Popup a dialog to display information about an event that has a
  specific location.  The user should be given the option to goto that
  location.
**************************************************************************/
void popup_notify_goto_dialog(const char *headline, const char *lines,
                              const struct text_tag_list *tags,
                              struct tile *ptile)
{
  char buf[2048];

  if (headline != NULL && headline[0] != '\0') {
    fc_snprintf(buf, sizeof(buf), "%s: %s", headline,
                lines != NULL ? lines : "");
  } else if (lines != NULL) {
    fc_strlcpy(buf, lines, sizeof(buf));
  } else {
    return;
  }
  sr_say(buf, 0);
  sr_log_append(buf);

  /* If a tile was given, center the map on it. */
  if (ptile != NULL) {
    center_tile_mapcanvas(ptile);
  }
}

/**********************************************************************//**
  Popup a dialog to display connection message from server.
**************************************************************************/
void popup_connect_msg(const char *headline, const char *message)
{
  char buf[1024];

  fc_snprintf(buf, sizeof(buf), "%s %s",
              headline != NULL ? headline : "",
              message != NULL ? message : "");
  sr_say(buf, 0);
  sr_log_append(buf);
}

/**********************************************************************//**
  Popup a generic dialog to display some generic information.
**************************************************************************/
void popup_notify_dialog(const char *caption, const char *headline,
                         const char *lines)
{
  char buf[2048];

  if (headline != NULL && headline[0] != '\0') {
    fc_snprintf(buf, sizeof(buf), "%s: %s", headline,
                lines != NULL ? lines : "");
  } else if (lines != NULL) {
    fc_strlcpy(buf, lines, sizeof(buf));
  } else if (caption != NULL) {
    fc_strlcpy(buf, caption, sizeof(buf));
  } else {
    return;
  }
  sr_say(buf, 0);
  sr_log_append(buf);
}

/* ====================================================================== */
/* Tech gained                                                            */
/* ====================================================================== */

/**********************************************************************//**
  Player has gained a new tech.
**************************************************************************/
void show_tech_gained_dialog(Tech_type_id tech)
{
  char buf[256];
  const char *name = research_advance_name_translation(
      research_get(client_player()), tech);

  fc_snprintf(buf, sizeof(buf), _("Discovered %s."),
              name != NULL ? name : "unknown technology");
  sr_say(buf, 1);
  sr_log_append(buf);
}

/* ====================================================================== */
/* Pillage selection                                                      */
/* ====================================================================== */

/**********************************************************************//**
  Popup a dialog asking the unit which improvement they would like to
  pillage.
**************************************************************************/
void popup_pillage_dialog(struct unit *punit, bv_extras may_pillage)
{
  /* If only one thing to pillage, just do it. Otherwise list the options. */
  int count = 0;
  struct extra_type *only = NULL;

  extra_type_iterate(pextra) {
    if (BV_ISSET(may_pillage, extra_index(pextra))) {
      count++;
      only = pextra;
    }
  } extra_type_iterate_end;

  if (count == 0) {
    sr_say(_("Nothing to pillage."), 1);
    return;
  }

  if (count == 1 && only != NULL) {
    /* Only one option — just do it. */
    request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, only);
    return;
  }

  /* Multiple options — announce them and pick the first by default.
     A full picker dialog would be better, but for now auto-pick and
     tell the user what was chosen. */
  {
    struct extra_type *first = NULL;
    char buf[512];

    fc_snprintf(buf, sizeof(buf), _("Pillaging: "));
    extra_type_iterate(pextra) {
      if (BV_ISSET(may_pillage, extra_index(pextra))) {
        if (first == NULL) {
          first = pextra;
        }
        cat_snprintf(buf, sizeof(buf), "%s, ",
                     extra_name_translation(pextra));
      }
    } extra_type_iterate_end;
    /* Trim trailing ", " */
    {
      size_t len = strlen(buf);

      if (len >= 2 && buf[len - 2] == ',') {
        buf[len - 2] = '.';
        buf[len - 1] = '\0';
      }
    }
    sr_say(buf, 1);
    sr_log_append(buf);

    if (first != NULL) {
      request_new_unit_activity_targeted(punit, ACTIVITY_PILLAGE, first);
    }
  }
}

/* ====================================================================== */
/* Upgrade / Disband confirmations                                        */
/* ====================================================================== */

/**********************************************************************//**
  Popup dialog for upgrade units.
**************************************************************************/
void popup_upgrade_dialog(struct unit_list *punits)
{
  char buf[512];
  int num = unit_list_size(punits);

  if (num <= 0) {
    return;
  }
  if (num == 1) {
    struct unit *punit = unit_list_get(punits, 0);
    const struct unit_type *from_type = unit_type_get(punit);
    const struct unit_type *to_type = can_upgrade_unittype(client_player(),
                                                           from_type);

    if (to_type == NULL) {
      fc_snprintf(buf, sizeof(buf), _("%s cannot be upgraded."),
                  utype_name_translation(from_type));
      sr_say(buf, 1);
      return;
    }
    fc_snprintf(buf, sizeof(buf), _("Upgrading %s to %s."),
                utype_name_translation(from_type),
                utype_name_translation(to_type));
  } else {
    fc_snprintf(buf, sizeof(buf), _("Upgrading %d units."), num);
  }
  sr_say(buf, 1);
  sr_log_append(buf);

  /* Auto-confirm — the server will check gold and reject if needed. */
  unit_list_iterate(punits, punit) {
    request_unit_upgrade(punit);
  } unit_list_iterate_end;
}

/**********************************************************************//**
  Pops up a dialog to confirm disband of the unit(s).
**************************************************************************/
void popup_disband_dialog(struct unit_list *punits)
{
  char buf[256];
  int num = unit_list_size(punits);

  if (num <= 0) {
    return;
  }
  if (num == 1) {
    struct unit *punit = unit_list_get(punits, 0);

    fc_snprintf(buf, sizeof(buf), _("Disbanding %s."),
                unit_name_translation(punit));
  } else {
    fc_snprintf(buf, sizeof(buf), _("Disbanding %d units."), num);
  }
  sr_say(buf, 1);
  sr_log_append(buf);

  /* Auto-confirm. */
  unit_list_iterate(punits, punit) {
    request_unit_disband(punit);
  } unit_list_iterate_end;
}

/* ====================================================================== */
/* Action selection dialog                                                */
/* ====================================================================== */

/* Track the current action selection state so query functions work. */
static int act_sel_actor_id  = IDENTITY_NUMBER_ZERO;
static int act_sel_target_city_id = IDENTITY_NUMBER_ZERO;
static int act_sel_target_unit_id = IDENTITY_NUMBER_ZERO;
static int act_sel_target_tile_idx = TILE_INDEX_NONE;
static int act_sel_target_extra_id = EXTRA_NONE;
static bool act_sel_open = FALSE;

/* ---------------------------------------------------------------------
   Action selection dialog.

   Builds the list of possible actions for (actor, target) and opens an
   sr_pick_enum picker. On commit, dispatches to request_do_action with
   the right target id derived from action_id_get_target_kind().

   The module-level act_sel_* statics persist while the picker is open
   so the action_selection_{actor,target_*} query functions still work.
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_ACTIONS 64

struct act_pick {
  int n;
  action_id ids[MAX_ACTIONS];
  char *labels[MAX_ACTIONS];
  const char *label_ptrs[MAX_ACTIONS];
};

static int target_id_for_action(action_id aid)
{
  switch (action_id_get_target_kind(aid)) {
  case ATK_CITY:
    return act_sel_target_city_id;
  case ATK_UNIT:
    return act_sel_target_unit_id;
  case ATK_TILE:
  case ATK_UNITS:
  case ATK_EXTRAS:
    return act_sel_target_tile_idx;
  case ATK_SELF:
    return act_sel_actor_id;
  default:
    return IDENTITY_NUMBER_ZERO;
  }
}

static void act_pick_free(struct act_pick *ap)
{
  int i;

  for (i = 0; i < ap->n; i++) {
    free(ap->labels[i]);
  }
  free(ap);
}

static void act_pick_do(action_id aid)
{
  request_do_action(aid, act_sel_actor_id, target_id_for_action(aid), 0, "");
  action_selection_no_longer_in_progress(act_sel_actor_id);
  act_sel_open = FALSE;
}

static void act_on_pick(int idx, const char *value, void *ud)
{
  struct act_pick *ap = ud;
  action_id aid;
  char msg[160];

  (void) value;
  if (idx < 0 || idx >= ap->n) {
    /* Canceled — release the action slot on the server. */
    action_selection_no_longer_in_progress(act_sel_actor_id);
    act_sel_open = FALSE;
    sr_say(_("Action canceled."), 1);
    act_pick_free(ap);
    return;
  }
  aid = ap->ids[idx];
  fc_snprintf(msg, sizeof(msg), _("Performing: %s"), ap->labels[idx]);
  sr_say(msg, 1);
  sr_log_append(msg);
  act_pick_do(aid);
  act_pick_free(ap);
}

#endif /* _WIN32 */

/**********************************************************************//**
  Popup a dialog that allows the player to select what action a unit
  should take.
**************************************************************************/
void popup_action_selection(struct unit *actor_unit,
                            struct city *target_city,
                            struct unit *target_unit,
                            struct tile *target_tile,
                            struct extra_type *target_extra,
                            const struct act_prob *act_probs)
{
  if (actor_unit == NULL) {
    return;
  }

  /* Store state for query functions. */
  act_sel_actor_id = actor_unit->id;
  act_sel_target_city_id = target_city ? target_city->id
                                       : IDENTITY_NUMBER_ZERO;
  act_sel_target_unit_id = target_unit ? target_unit->id
                                       : IDENTITY_NUMBER_ZERO;
  act_sel_target_tile_idx = target_tile ? tile_index(target_tile)
                                        : TILE_INDEX_NONE;
  act_sel_target_extra_id = target_extra ? extra_number(target_extra)
                                         : EXTRA_NONE;
  act_sel_open = TRUE;

#ifdef _WIN32
  {
    struct act_pick *ap = fc_malloc(sizeof(*ap));
    char summary[2048];

    ap->n = 0;
    fc_snprintf(summary, sizeof(summary), _("Actions: "));

    action_iterate(act_id) {
      if (action_prob_possible(act_probs[act_id])
          && ap->n < MAX_ACTIONS) {
        const char *name = action_prepare_ui_name(act_id, "",
                                                  act_probs[act_id], NULL);

        ap->ids[ap->n] = act_id;
        ap->labels[ap->n] = fc_strdup(name);
        ap->label_ptrs[ap->n] = ap->labels[ap->n];
        cat_snprintf(summary, sizeof(summary), "%s, ", name);
        ap->n++;
      }
    } action_iterate_end;

    if (ap->n == 0) {
      sr_say(_("No actions available."), 1);
      action_selection_no_longer_in_progress(act_sel_actor_id);
      act_sel_open = FALSE;
      act_pick_free(ap);
      return;
    }

    /* Trim trailing ", " for the summary line. */
    {
      size_t len = strlen(summary);

      if (len >= 2 && summary[len - 2] == ',') {
        summary[len - 2] = '.';
        summary[len - 1] = '\0';
      }
    }
    sr_say(summary, 1);
    sr_log_append(summary);

    /* Auto-execute when there is only one possible action — nothing to
       pick, no need to interrupt the player. */
    if (ap->n == 1) {
      char msg[160];

      fc_snprintf(msg, sizeof(msg), _("Performing: %s"), ap->labels[0]);
      sr_say(msg, 0);
      act_pick_do(ap->ids[0]);
      act_pick_free(ap);
      return;
    }

    sr_pick_enum(_("Pick action"),
                 ap->label_ptrs, ap->n, 0, act_on_pick, ap);
  }
#else
  /* Non-Windows stub — just close. */
  action_selection_no_longer_in_progress(act_sel_actor_id);
  act_sel_open = FALSE;
#endif /* _WIN32 */
}

/**********************************************************************//**
  Returns the id of the actor unit currently handled in action selection
  dialog when the action selection dialog is open.
  Returns IDENTITY_NUMBER_ZERO if no action selection dialog is open.
**************************************************************************/
int action_selection_actor_unit(void)
{
  return act_sel_open ? act_sel_actor_id : IDENTITY_NUMBER_ZERO;
}

int action_selection_target_city(void)
{
  return act_sel_open ? act_sel_target_city_id : IDENTITY_NUMBER_ZERO;
}

int action_selection_target_unit(void)
{
  return act_sel_open ? act_sel_target_unit_id : IDENTITY_NUMBER_ZERO;
}

int action_selection_target_tile(void)
{
  return act_sel_open ? act_sel_target_tile_idx : TILE_INDEX_NONE;
}

int action_selection_target_extra(void)
{
  return act_sel_open ? act_sel_target_extra_id : EXTRA_NONE;
}

/**********************************************************************//**
  Updates the action selection dialog with new information.
**************************************************************************/
void action_selection_refresh(struct unit *actor_unit,
                              struct city *target_city,
                              struct unit *target_unit,
                              struct tile *target_tile,
                              struct extra_type *target_extra,
                              const struct act_prob *act_probs)
{
  /* Re-popup with updated probabilities if still open. */
  if (act_sel_open) {
    popup_action_selection(actor_unit, target_city, target_unit,
                           target_tile, target_extra, act_probs);
  }
}

/**********************************************************************//**
  Closes the action selection dialog.
**************************************************************************/
void action_selection_close(void)
{
  if (act_sel_open) {
    action_selection_no_longer_in_progress(act_sel_actor_id);
    act_sel_open = FALSE;
  }
}

/**********************************************************************//**
  Let the non shared client code know that the action selection process
  no longer is in progress for the specified unit.
**************************************************************************/
void action_selection_no_longer_in_progress_gui_specific(int actor_id)
{
  if (act_sel_actor_id == actor_id) {
    act_sel_open = FALSE;
  }
}

/* ====================================================================== */
/* Spy / diplomat actions                                                 */
/* ====================================================================== */

/**********************************************************************//**
  Popup a window asking a diplomatic unit if it wishes to incite the
  given enemy city.
**************************************************************************/
void popup_incite_dialog(struct unit *actor, struct city *pcity, int cost,
                         const struct action *paction)
{
  char buf[256];

  if (cost == INCITE_IMPOSSIBLE_COST) {
    sr_say(_("Inciting revolt is impossible here."), 1);
    return;
  }
  fc_snprintf(buf, sizeof(buf), _("Inciting %s costs %d gold. Proceeding."),
              city_name_get(pcity), cost);
  sr_say(buf, 1);
  sr_log_append(buf);

  /* Auto-confirm. */
  request_do_action(paction->id, actor->id, pcity->id, 0, "");
}

/**********************************************************************//**
  Popup a dialog asking a diplomatic unit if it wishes to bribe the
  given enemy unit.
**************************************************************************/
void popup_bribe_dialog(struct unit *actor, struct unit *punit, int cost,
                        const struct action *paction)
{
  char buf[256];

  if (cost >= client_player()->economic.gold) {
    fc_snprintf(buf, sizeof(buf),
                _("Bribing costs %d gold; you only have %d."),
                cost, client_player()->economic.gold);
    sr_say(buf, 1);
    return;
  }
  fc_snprintf(buf, sizeof(buf), _("Bribing %s costs %d gold. Proceeding."),
              unit_name_translation(punit), cost);
  sr_say(buf, 1);
  sr_log_append(buf);

  /* Auto-confirm. */
  request_do_action(paction->id, actor->id, punit->id, 0, "");
}

/**********************************************************************//**
  Popup a dialog asking a diplomatic unit if it wishes to sabotage the
  given enemy city.
**************************************************************************/
void popup_sabotage_dialog(struct unit *actor, struct city *pcity,
                           const struct action *paction)
{
  char buf[256];

  /* Auto-select random target (production = 0). A full dialog would let
     the player pick a specific building. */
  fc_snprintf(buf, sizeof(buf), _("Sabotaging %s (random target)."),
              city_name_get(pcity));
  sr_say(buf, 1);
  sr_log_append(buf);

  request_do_action(paction->id, actor->id, pcity->id, B_LAST + 1, "");
}

/* ====================================================================== */
/* Nation selection                                                        */
/* ====================================================================== */

/* ---------------------------------------------------------------------
   Nation picker — a persistent modal modelled on sr_newgame.c.

   A listbox shows three rows (Nation / Leader / Gender); Enter or
   double-click on a row opens a sub-picker (sr_pick_enum) to change
   that field. The dialog stays open while sub-pickers come and go,
   so the user can review all three choices before committing with
   OK. Random sends nation = -1 (server picks everything). Cancel
   closes without sending — the server re-requests.

   Final commit sends PACKET_NATION_SELECT_REQ via
   dsend_packet_nation_select_req.
   ------------------------------------------------------------------ */

#ifdef _WIN32
#include <windows.h>

#include "sr_mapedit.h"        /* sr_mapedit_is_newedit_pending */
#include "sr_window.h"         /* sr_window_register_popup */

#define SR_RACES_CLASS   L"FreecivSRRaces"
#define IDC_RACES_LIST   9501
#define IDC_RACES_OK     9502
#define IDC_RACES_RANDOM 9503
#define IDC_RACES_CANCEL 9504

enum races_row {
  RACES_ROW_NATION,
  RACES_ROW_LEADER,
  RACES_ROW_GENDER,
  RACES_ROW_COUNT
};

struct races_dlg {
  HWND dlg;
  HWND list;
  HWND btn_ok;
  HWND btn_random;
  HWND btn_cancel;
  HWND prev_focus;

  int player_no;

  /* Nation catalog. Slot 0 is "Random"; slots 1..n_nations are real. */
  int n_nations;
  const struct nation_type **nations;       /* size n_nations + 1 */
  char **nation_labels;                     /* size n_nations + 1 */
  const char **nation_label_ptrs;           /* size n_nations + 1 */

  /* Leader catalog for the currently picked nation. Rebuilt on
     nation change. Slot 0 is "Random leader"; slots 1..n_leaders
     are real names. */
  int n_leaders;
  char **leader_names;                      /* size n_leaders + 1,
                                               owned UTF-8 */
  bool *leader_is_male;                     /* size n_leaders */
  const char **leader_label_ptrs;           /* size n_leaders + 1 */

  /* Current selections. */
  int nation_idx;   /* 0 = Random; else 1..n_nations */
  int leader_idx;   /* 0 = Random; else 1..n_leaders */
  int gender_idx;   /* 0 = As suggested, 1 = Male, 2 = Female */
};

static struct races_dlg *g_races = NULL;
static WNDPROC g_races_list_default = NULL;

/* ---- helpers ----------------------------------------------------- */

static wchar_t *races_to_wide(const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (utf8 == NULL) { return NULL; }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) { return NULL; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return NULL; }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  return w;
}

static void races_free_leaders(struct races_dlg *s)
{
  int i;

  if (s->leader_names != NULL) {
    for (i = 0; i < s->n_leaders; i++) { free(s->leader_names[i]); }
    free(s->leader_names);
    s->leader_names = NULL;
  }
  free(s->leader_is_male);
  s->leader_is_male = NULL;
  free(s->leader_label_ptrs);
  s->leader_label_ptrs = NULL;
  s->n_leaders = 0;
  s->leader_idx = 0;
}

static void races_rebuild_leaders(struct races_dlg *s)
{
  const struct nation_leader_list *leaders;
  const struct nation_type *pnation;
  int i;

  races_free_leaders(s);
  if (s->nation_idx <= 0 || s->nation_idx > s->n_nations) { return; }
  pnation = s->nations[s->nation_idx];
  if (pnation == NULL) { return; }
  leaders = nation_leaders(pnation);
  if (leaders == NULL) { return; }
  s->n_leaders = nation_leader_list_size(leaders);
  if (s->n_leaders <= 0) { return; }

  s->leader_names = (char **)calloc((size_t)s->n_leaders, sizeof(char *));
  s->leader_is_male = (bool *)calloc((size_t)s->n_leaders, sizeof(bool));
  s->leader_label_ptrs = (const char **)
      calloc((size_t)(s->n_leaders + 1), sizeof(const char *));
  if (s->leader_names == NULL || s->leader_is_male == NULL
      || s->leader_label_ptrs == NULL) {
    races_free_leaders(s);
    return;
  }
  i = 0;
  nation_leader_list_iterate(leaders, pleader) {
    if (i >= s->n_leaders) { break; }
    s->leader_names[i] = fc_strdup(nation_leader_name(pleader));
    s->leader_is_male[i] = nation_leader_is_male(pleader);
    i++;
  } nation_leader_list_iterate_end;
  s->leader_label_ptrs[0] = _("Random leader");
  for (i = 0; i < s->n_leaders; i++) {
    s->leader_label_ptrs[i + 1] = s->leader_names[i];
  }
}

static void races_row_label(struct races_dlg *s, int row,
                            char *out, size_t outsz)
{
  switch (row) {
  case RACES_ROW_NATION:
    if (s->nation_idx > 0 && s->nation_idx <= s->n_nations
        && s->nation_labels[s->nation_idx] != NULL) {
      snprintf(out, outsz, "Nation:  %s", s->nation_labels[s->nation_idx]);
    } else {
      snprintf(out, outsz, "Nation:  Random");
    }
    break;
  case RACES_ROW_LEADER:
    if (s->nation_idx == 0) {
      snprintf(out, outsz, "Leader:  (pick nation first)");
    } else if (s->leader_idx <= 0 || s->n_leaders == 0) {
      snprintf(out, outsz, "Leader:  Random");
    } else {
      int li = s->leader_idx - 1;

      snprintf(out, outsz, "Leader:  %s (%s)",
               s->leader_names[li],
               s->leader_is_male[li] ? "male" : "female");
    }
    break;
  case RACES_ROW_GENDER:
    switch (s->gender_idx) {
    case 1:  snprintf(out, outsz, "Gender:  Male");          break;
    case 2:  snprintf(out, outsz, "Gender:  Female");        break;
    default: snprintf(out, outsz, "Gender:  As suggested");  break;
    }
    break;
  default:
    snprintf(out, outsz, "?");
    break;
  }
}

static void races_populate_list(void)
{
  struct races_dlg *s = g_races;
  int sel;
  int i;

  if (s == NULL || s->list == NULL) { return; }
  sel = (int)SendMessageW(s->list, LB_GETCURSEL, 0, 0);
  SendMessageW(s->list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(s->list, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < RACES_ROW_COUNT; i++) {
    char buf[200];
    wchar_t *w;

    races_row_label(s, i, buf, sizeof(buf));
    w = races_to_wide(buf);
    if (w != NULL) {
      SendMessageW(s->list, LB_ADDSTRING, 0, (LPARAM)w);
      free(w);
    }
  }
  if (sel < 0 || sel >= RACES_ROW_COUNT) { sel = 0; }
  SendMessageW(s->list, LB_SETCURSEL, (WPARAM)sel, 0);
  SendMessageW(s->list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(s->list, NULL, TRUE);
}

/* ---- sub-picker callbacks --------------------------------------- */

static void races_on_pick_nation(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_races == NULL || idx < 0 || idx > g_races->n_nations) { return; }
  g_races->nation_idx = idx;
  races_rebuild_leaders(g_races);
  races_populate_list();
}

static void races_on_pick_leader(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_races == NULL) { return; }
  if (idx < 0 || idx > g_races->n_leaders) { return; }
  g_races->leader_idx = idx;
  races_populate_list();
}

static void races_on_pick_gender(int idx, const char *v, void *ud)
{
  (void)v; (void)ud;
  if (g_races == NULL) { return; }
  if (idx < 0 || idx > 2) { return; }
  g_races->gender_idx = idx;
  races_populate_list();
}

static void races_open_picker(int row)
{
  struct races_dlg *s = g_races;
  static const char *const GENDER_OPTS[3] = {
    N_("As suggested"), N_("Male"), N_("Female")
  };
  const char *translated_gender[3];
  int i;

  if (s == NULL) { return; }
  switch (row) {
  case RACES_ROW_NATION:
    sr_pick_enum(_("Nation?"), s->nation_label_ptrs,
                 s->n_nations + 1, s->nation_idx,
                 races_on_pick_nation, NULL);
    break;
  case RACES_ROW_LEADER:
    if (s->nation_idx <= 0 || s->n_leaders <= 0) {
      sr_say(_("Pick a nation first."), 1);
      return;
    }
    sr_pick_enum(_("Leader name?"), s->leader_label_ptrs,
                 s->n_leaders + 1, s->leader_idx,
                 races_on_pick_leader, NULL);
    break;
  case RACES_ROW_GENDER:
    for (i = 0; i < 3; i++) { translated_gender[i] = _(GENDER_OPTS[i]); }
    sr_pick_enum(_("Gender?"), translated_gender, 3, s->gender_idx,
                 races_on_pick_gender, NULL);
    break;
  default:
    break;
  }
}

/* ---- commit / cancel / close ----------------------------------- */

static void races_send_selection(bool random_all)
{
  struct races_dlg *s = g_races;
  const struct nation_type *pnation;
  int nation_no = -1;
  int style_no = 0;
  bool is_male = FALSE;
  const char *name = "";

  if (s == NULL) { return; }
  if (random_all || s->nation_idx <= 0) {
    dsend_packet_nation_select_req(&client.conn, s->player_no,
                                   -1, FALSE, "", 0);
    return;
  }
  pnation = s->nations[s->nation_idx];
  if (pnation == NULL) {
    dsend_packet_nation_select_req(&client.conn, s->player_no,
                                   -1, FALSE, "", 0);
    return;
  }
  nation_no = nation_number(pnation);
  if (style_of_nation(pnation) != NULL) {
    style_no = style_number(style_of_nation(pnation));
  }
  if (s->leader_idx > 0 && s->leader_idx <= s->n_leaders) {
    int li = s->leader_idx - 1;

    name = s->leader_names[li];
    is_male = s->leader_is_male[li];
  }
  if (s->gender_idx == 1) { is_male = TRUE; }
  else if (s->gender_idx == 2) { is_male = FALSE; }

  dsend_packet_nation_select_req(&client.conn, s->player_no,
                                 nation_no, is_male, name, style_no);
}

static void races_close_window(void)
{
  struct races_dlg *s = g_races;
  int i;

  if (s == NULL) { return; }
  if (s->dlg != NULL) {
    sr_window_unregister_popup(s->dlg);
    DestroyWindow(s->dlg);
    s->dlg = NULL;
  }
  races_free_leaders(s);
  if (s->nation_labels != NULL) {
    for (i = 0; i <= s->n_nations; i++) { free(s->nation_labels[i]); }
    free(s->nation_labels);
  }
  free(s->nation_label_ptrs);
  free(s->nations);
  if (s->prev_focus != NULL && IsWindow(s->prev_focus)) {
    SetFocus(s->prev_focus);
  }
  free(s);
  g_races = NULL;
}

/* ---- window proc ----------------------------------------------- */

static LRESULT CALLBACK races_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
  case WM_CREATE: {
    HINSTANCE hi = GetModuleHandleW(NULL);

    if (g_races == NULL) { return -1; }
    g_races->list = CreateWindowExW(
        WS_EX_CLIENTEDGE, L"LISTBOX", L"",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
        | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
        0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_RACES_LIST, hi, NULL);
    g_races->btn_ok = CreateWindowExW(
        0, L"BUTTON", L"OK",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
        0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_RACES_OK, hi, NULL);
    g_races->btn_random = CreateWindowExW(
        0, L"BUTTON", L"Random",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_RACES_RANDOM, hi, NULL);
    g_races->btn_cancel = CreateWindowExW(
        0, L"BUTTON", L"Cancel",
        WS_CHILD | WS_VISIBLE | WS_TABSTOP,
        0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_RACES_CANCEL, hi, NULL);
    races_populate_list();
    return 0;
  }
  case WM_SIZE: {
    RECT rc;
    int pad = 8;
    int btn_h = 28;

    GetClientRect(h, &rc);
    if (g_races == NULL) { return 0; }
    if (g_races->list != NULL) {
      MoveWindow(g_races->list, pad, pad,
                 rc.right - 2 * pad,
                 rc.bottom - 3 * pad - btn_h, TRUE);
    }
    if (g_races->btn_ok != NULL) {
      MoveWindow(g_races->btn_ok, pad, rc.bottom - pad - btn_h,
                 100, btn_h, TRUE);
    }
    if (g_races->btn_random != NULL) {
      MoveWindow(g_races->btn_random, pad + 100 + pad,
                 rc.bottom - pad - btn_h, 100, btn_h, TRUE);
    }
    if (g_races->btn_cancel != NULL) {
      MoveWindow(g_races->btn_cancel, pad + 2 * (100 + pad),
                 rc.bottom - pad - btn_h, 100, btn_h, TRUE);
    }
    return 0;
  }
  case WM_COMMAND: {
    int id = LOWORD(w);

    if (g_races == NULL) { break; }
    if (id == IDC_RACES_LIST && HIWORD(w) == LBN_DBLCLK) {
      int sel = (int)SendMessageW(g_races->list, LB_GETCURSEL, 0, 0);

      races_open_picker(sel);
      return 0;
    }
    if (id == IDC_RACES_OK && HIWORD(w) == BN_CLICKED) {
      races_send_selection(FALSE);
      races_close_window();
      return 0;
    }
    if (id == IDC_RACES_RANDOM && HIWORD(w) == BN_CLICKED) {
      races_send_selection(TRUE);
      races_close_window();
      return 0;
    }
    if (id == IDC_RACES_CANCEL && HIWORD(w) == BN_CLICKED) {
      /* Close without sending — server will retry. */
      races_close_window();
      return 0;
    }
    if (id == IDCANCEL) {
      races_close_window();
      return 0;
    }
    break;
  }
  case WM_KEYDOWN:
    if (w == VK_ESCAPE) { races_close_window(); return 0; }
    break;
  case WM_CLOSE:
    races_close_window();
    return 0;
  case WM_DESTROY:
    if (g_races != NULL) {
      g_races->dlg = NULL;
      g_races->list = NULL;
      g_races->btn_ok = NULL;
      g_races->btn_random = NULL;
      g_races->btn_cancel = NULL;
    }
    return 0;
  default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

static LRESULT CALLBACK races_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    LRESULT base = g_races_list_default != NULL
                   ? CallWindowProcW(g_races_list_default, h, m, w, l)
                   : DefWindowProcW(h, m, w, l);
    MSG *pmsg = (MSG *)l;

    if (pmsg != NULL
        && (pmsg->wParam == VK_RETURN || pmsg->wParam == VK_ESCAPE)) {
      return base | DLGC_WANTMESSAGE;
    }
    return base;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) {
      int sel = (int)SendMessageW(h, LB_GETCURSEL, 0, 0);

      races_open_picker(sel);
      return 0;
    }
    if (w == VK_ESCAPE) {
      races_close_window();
      return 0;
    }
  }
  if (g_races_list_default != NULL) {
    return CallWindowProcW(g_races_list_default, h, m, w, l);
  }
  return DefWindowProcW(h, m, w, l);
}

static int races_class_registered = 0;
static int races_ensure_class(HINSTANCE hi)
{
  WNDCLASSEXW wc;

  if (races_class_registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = races_wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_RACES_CLASS;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    races_class_registered = 1;
    return 1;
  }
  return 0;
}

#endif /* _WIN32 */

/**********************************************************************//**
  Popup the nation selection dialog.
**************************************************************************/
void popup_races_dialog(struct player *pplayer)
{
#ifdef _WIN32
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct races_dlg *s;
  int count = 0;
  int i;

  if (pplayer == NULL) {
    return;
  }
  /* Scenario-editor bootstrap: user just wants to paint terrain and
     doesn't care which civ they nominally "are". Auto-random the pick
     and skip the dialog so the editor flow reaches PAGE_GAME without
     an irrelevant interrupt. */
  if (sr_mapedit_is_newedit_pending()) {
    dsend_packet_nation_select_req(&client.conn, player_number(pplayer),
                                   -1, FALSE, "", 0);
    sr_log_append("newedit: auto-randomed nation (edit mode, civ "
                  "selection skipped).");
    return;
  }
  if (!races_ensure_class(hi)) {
    /* Class registration failed — fall back to random so the server
       doesn't block waiting on us. */
    dsend_packet_nation_select_req(&client.conn, player_number(pplayer),
                                   -1, FALSE, "", 0);
    return;
  }
  if (g_races != NULL) {
    /* A previous dialog is still up. Kill it; a new one supersedes. */
    races_close_window();
  }

  s = (struct races_dlg *)calloc(1, sizeof(*s));
  if (s == NULL) {
    dsend_packet_nation_select_req(&client.conn, player_number(pplayer),
                                   -1, FALSE, "", 0);
    return;
  }
  s->prev_focus = GetFocus();
  s->player_no = player_number(pplayer);
  s->nation_idx = 0;       /* Random by default */
  s->leader_idx = 0;       /* Random by default */
  s->gender_idx = 0;       /* As suggested by default */
  g_races = s;

  /* Collect pickable nations; slot 0 is "Random nation". */
  nations_iterate(pnation) {
    if (is_nation_pickable(pnation) && pnation->is_playable) {
      count++;
    }
  } nations_iterate_end;

  s->n_nations = count;
  s->nations = (const struct nation_type **)
      calloc((size_t)(count + 1), sizeof(*s->nations));
  s->nation_labels = (char **)calloc((size_t)(count + 1), sizeof(char *));
  s->nation_label_ptrs = (const char **)
      calloc((size_t)(count + 1), sizeof(const char *));
  if (s->nations == NULL || s->nation_labels == NULL
      || s->nation_label_ptrs == NULL) {
    races_close_window();
    dsend_packet_nation_select_req(&client.conn, player_number(pplayer),
                                   -1, FALSE, "", 0);
    return;
  }
  s->nations[0] = NULL;
  s->nation_labels[0] = fc_strdup(_("Random nation"));
  s->nation_label_ptrs[0] = s->nation_labels[0];
  i = 1;
  nations_iterate(pnation) {
    if (is_nation_pickable(pnation) && pnation->is_playable
        && i <= count) {
      s->nations[i] = pnation;
      s->nation_labels[i] = fc_strdup(nation_adjective_translation(pnation));
      s->nation_label_ptrs[i] = s->nation_labels[i];
      i++;
    }
  } nations_iterate_end;

  s->dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_RACES_CLASS, L"Nation selection",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 460, 280,
      NULL, NULL, hi, NULL);
  if (s->dlg == NULL) {
    races_close_window();
    dsend_packet_nation_select_req(&client.conn, player_number(pplayer),
                                   -1, FALSE, "", 0);
    return;
  }
  sr_window_register_popup(s->dlg);

  /* Subclass the listbox so Enter opens a sub-picker and Escape
     cancels instead of triggering the default button. */
  if (s->list != NULL) {
    g_races_list_default = (WNDPROC)SetWindowLongPtrW(
        s->list, GWLP_WNDPROC, (LONG_PTR)races_list_proc);
  }

  sr_say(_("Nation selection. Enter on a row to change it; OK to commit."), 1);
  sr_log_append("Races dialog opened.");

  ShowWindow(s->dlg, SW_SHOWNORMAL);
  UpdateWindow(s->dlg);
  if (s->list != NULL) { SetFocus(s->list); }
#else
  (void)pplayer;
#endif
}

/**********************************************************************//**
  Close the nation selection dialog.
**************************************************************************/
void popdown_races_dialog(void)
{
#ifdef _WIN32
  if (g_races != NULL) {
    races_close_window();
  }
#endif
}

/**********************************************************************//**
  Popup a dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_popup(struct tile *ptile)
{
  /* List the units on this tile and select the first. */
  if (ptile == NULL) { return; }

  {
    int count = unit_list_size(ptile->units);
    char buf[1024];

    if (count == 0) {
      sr_say(_("No units on this tile."), 1);
      return;
    }
    fc_snprintf(buf, sizeof(buf), _("%d unit(s) on tile: "), count);
    unit_list_iterate(ptile->units, punit) {
      cat_snprintf(buf, sizeof(buf), "%s, ",
                   unit_name_translation(punit));
    } unit_list_iterate_end;
    /* Trim trailing ", " */
    {
      size_t len = strlen(buf);

      if (len >= 2 && buf[len - 2] == ',') {
        buf[len - 2] = '.';
        buf[len - 1] = '\0';
      }
    }
    sr_say(buf, 1);

    /* Focus the first unit. */
    {
      struct unit *punit = unit_list_get(ptile->units, 0);

      if (punit != NULL) {
        unit_focus_set(punit);
      }
    }
  }
}

/**********************************************************************//**
  Update the dialog window to select units on a particular tile.
**************************************************************************/
void unit_select_dialog_update_real(void *unused)
{
  /* Nothing to update — we don't maintain a persistent dialog. */
}

/**********************************************************************//**
  The server has changed the set of selectable nations.
**************************************************************************/
void races_update_pickable(bool nationset_change)
{
  /* Will be used when nation picker dialog is implemented. */
}

/**********************************************************************//**
  In the nation selection dialog, make already-taken nations unavailable.
**************************************************************************/
void races_toggles_set_sensitive(void)
{
  /* Will be used when nation picker dialog is implemented. */
}

/* ====================================================================== */
/* Miscellaneous dialogs                                                  */
/* ====================================================================== */

/* ---------------------------------------------------------------------
   Government chooser. Lists every government the player can switch to
   (skipping the current one and the revolution-anarchy government), and
   commits via set_government_choice() — which sends the same packet
   gtk-3.22 sends. If no eligible target exists, falls back to plain
   start_revolution() (anarchy).
   ------------------------------------------------------------------ */

#ifdef _WIN32
struct gov_pick_state {
  int n;
  int gov_ids[64];      /* indexed alongside name_ptrs */
  char *names[64];      /* heap-owned UTF-8 names for sr_pick_enum */
  const char *name_ptrs[64];
};

static void gov_pick_free(struct gov_pick_state *st)
{
  int i;

  for (i = 0; i < st->n; i++) {
    free(st->names[i]);
  }
  free(st);
}

static void gov_pick_commit(int index, const char *value_utf8, void *ud)
{
  struct gov_pick_state *st = ud;
  struct government *gov;

  (void) value_utf8;
  if (index < 0 || index >= st->n) {
    sr_say(_("Government choice canceled."), 1);
    gov_pick_free(st);
    return;
  }
  gov = government_by_number(st->gov_ids[index]);
  if (gov == NULL) {
    sr_say(_("Unknown government."), 1);
    gov_pick_free(st);
    return;
  }

  {
    char buf[128];

    fc_snprintf(buf, sizeof(buf), _("Revolution: switching to %s."),
                government_name_translation(gov));
    sr_say(buf, 1);
    sr_log_append(buf);
  }
  set_government_choice(gov);
  gov_pick_free(st);
}
#endif /* _WIN32 */

/**********************************************************************//**
  Popup the government chooser. Lists every government the player can
  legally adopt and commits the chosen one (which starts the revolution
  if needed). If no choice is made or none is available, falls back to
  plain anarchy via start_revolution().
**************************************************************************/
void popup_revolution_dialog(void)
{
#ifdef _WIN32
  struct player *me = client_player();
  struct government *current;
  struct gov_pick_state *st;
  int cur_idx = 0;

  if (me == NULL || !can_client_issue_orders()) {
    sr_say(_("Can't change government right now."), 1);
    return;
  }

  current = government_of_player(me);

  st = fc_malloc(sizeof(*st));
  st->n = 0;

  governments_iterate(gov) {
    if (gov == current) {
      continue;
    }
    if (game.info.government_during_revolution_id == government_number(gov)) {
      /* Skip the placeholder anarchy government — handled separately. */
      continue;
    }
    if (!can_change_to_government(me, gov)) {
      continue;
    }
    if (st->n >= (int) (sizeof(st->gov_ids) / sizeof(st->gov_ids[0]))) {
      break;
    }
    st->gov_ids[st->n] = government_number(gov);
    st->names[st->n] = fc_strdup(government_name_translation(gov));
    st->name_ptrs[st->n] = st->names[st->n];
    st->n++;
  } governments_iterate_end;

  if (st->n == 0) {
    /* No legal target — just start anarchy. */
    sr_say(_("No alternative government available — starting anarchy."), 1);
    sr_log_append(_("Revolution: entering anarchy (no eligible target)."));
    start_revolution();
    free(st);
    return;
  }

  {
    char buf[128];

    fc_snprintf(buf, sizeof(buf),
                _("Current government: %s. %d alternative(s) available."),
                government_name_translation(current), st->n);
    sr_say(buf, 1);
    sr_log_append(buf);
  }

  sr_pick_enum(_("Choose new government"),
               st->name_ptrs, st->n, cur_idx,
               gov_pick_commit, st);
#endif /* _WIN32 */
}

/**********************************************************************//**
  Ruleset (modpack) has suggested loading certain tileset.
**************************************************************************/
void popup_tileset_suggestion_dialog(void)
{
  /* SR client doesn't use tilesets — ignore. */
}

/* Accept/keep prompt for soundset and musicset suggestions. The user
   picks "Load suggested" (index 0) or "Keep current" (index 1). The
   suggestion callbacks fire the real audio_restart() on accept. When
   autoaccept_* is set, we bypass the picker entirely and apply the
   change right away so the modpack's preferred audio kicks in
   without pestering the player. */

static const char *soundset_choices[2];
static const char *musicset_choices[2];

static void soundset_suggestion_on_pick(int idx, const char *v, void *ud)
{
  char msg[256];

  (void) v; (void) ud;
  if (idx == 0) {
    audio_restart(game.control.preferred_soundset, music_set_name);
    fc_snprintf(msg, sizeof(msg), _("Loaded suggested soundset %s."),
                game.control.preferred_soundset);
    sr_say(msg, 1);
    sr_log_append(msg);
  } else {
    sr_say(_("Kept current soundset."), 1);
  }
}

static void musicset_suggestion_on_pick(int idx, const char *v, void *ud)
{
  char msg[256];

  (void) v; (void) ud;
  if (idx == 0) {
    audio_restart(sound_set_name, game.control.preferred_musicset);
    fc_snprintf(msg, sizeof(msg), _("Loaded suggested musicset %s."),
                game.control.preferred_musicset);
    sr_say(msg, 1);
    sr_log_append(msg);
  } else {
    sr_say(_("Kept current musicset."), 1);
  }
}

/**********************************************************************//**
  Ruleset (modpack) has suggested loading certain soundset.
**************************************************************************/
void popup_soundset_suggestion_dialog(void)
{
  char title[256];

  if (game.control.preferred_soundset[0] == '\0') {
    return;
  }
  if (gui_options.autoaccept_soundset_suggestion) {
    audio_restart(game.control.preferred_soundset, music_set_name);
    return;
  }
  fc_snprintf(title, sizeof(title),
              _("Modpack suggests soundset %s (current: %s)."),
              game.control.preferred_soundset,
              sound_set_name != NULL ? sound_set_name : "");
  sr_say(title, 1);
  soundset_choices[0] = _("Load suggested");
  soundset_choices[1] = _("Keep current");
  sr_pick_enum(title, soundset_choices, 2, 0,
               soundset_suggestion_on_pick, NULL);
}

/**********************************************************************//**
  Ruleset (modpack) has suggested loading certain musicset.
**************************************************************************/
void popup_musicset_suggestion_dialog(void)
{
  char title[256];

  if (game.control.preferred_musicset[0] == '\0') {
    return;
  }
  if (gui_options.autoaccept_musicset_suggestion) {
    audio_restart(sound_set_name, game.control.preferred_musicset);
    return;
  }
  fc_snprintf(title, sizeof(title),
              _("Modpack suggests musicset %s (current: %s)."),
              game.control.preferred_musicset,
              music_set_name != NULL ? music_set_name : "");
  sr_say(title, 1);
  musicset_choices[0] = _("Load suggested");
  musicset_choices[1] = _("Keep current");
  sr_pick_enum(title, musicset_choices, 2, 0,
               musicset_suggestion_on_pick, NULL);
}

/**********************************************************************//**
  Tileset (modpack) has suggested loading certain theme.
**************************************************************************/
bool popup_theme_suggestion_dialog(const char *theme_name)
{
  /* SR client doesn't use themes. */
  return FALSE;
}

/**********************************************************************//**
  This function is called when the client disconnects or the game is
  over.  It should close all dialog windows for that game.
**************************************************************************/
void popdown_all_game_dialogs(void)
{
  /* Close action selection state if open. */
  if (act_sel_open) {
    act_sel_open = FALSE;
  }
}

/**********************************************************************//**
  Show tileset error dialog.
**************************************************************************/
void show_tileset_error(bool fatal, const char *tset_name, const char *msg)
{
  char buf[512];

  fc_snprintf(buf, sizeof(buf), "Tileset error%s: %s",
              fatal ? " (fatal)" : "", msg != NULL ? msg : "");
  sr_say(buf, 1);
  sr_log_append(buf);
}

/**********************************************************************//**
  Give a warning when user is about to edit scenario with manually
  set properties.
**************************************************************************/
bool gui_handmade_scenario_warning(void)
{
  /* Just tell the client common code to handle this. */
  return FALSE;
}

/**********************************************************************//**
  Unit wants to get into some transport on given tile.
**************************************************************************/
bool gui_request_transport(struct unit *pcargo, struct tile *ptile)
{
  /* Auto-load into the first available transport. */
  return FALSE; /* Let the common code handle it. */
}

/**********************************************************************//**
  Popup detailed information about battle or save information for
  some kind of statistics.
**************************************************************************/
void gui_popup_combat_info(int attacker_unit_id, int defender_unit_id,
                           int attacker_hp, int defender_hp,
                           bool make_att_veteran, bool make_def_veteran)
{
  char buf[256];
  struct unit *att = game_unit_by_number(attacker_unit_id);
  struct unit *def = game_unit_by_number(defender_unit_id);

  if (att == NULL && def == NULL) { return; }

  fc_snprintf(buf, sizeof(buf), _("Combat: %s (%d HP) vs %s (%d HP).%s%s"),
              att != NULL ? unit_name_translation(att) : "?",
              attacker_hp,
              def != NULL ? unit_name_translation(def) : "?",
              defender_hp,
              make_att_veteran ? _(" Attacker promoted!") : "",
              make_def_veteran ? _(" Defender promoted!") : "");
  sr_say(buf, 0);
  sr_log_append(buf);
}

/**********************************************************************//**
  Common code wants confirmation for an action.
**************************************************************************/
void gui_request_action_confirmation(const char *expl,
                                     struct act_confirmation_data *data)
{
  /* Auto-confirm with announcement. */
  if (expl != NULL && expl[0] != '\0') {
    sr_say(expl, 0);
  }
  action_confirmation(data, TRUE);
}

/**********************************************************************//**
  Popup image window.
**************************************************************************/
void gui_popup_image(const char *tag)
{
  /* SR client has no image display. */
}
