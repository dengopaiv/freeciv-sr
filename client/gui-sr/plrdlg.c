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
#include "diptreaty.h"
#include "fc_types.h"
#include "nation.h"
#include "packets.h"
#include "player.h"

/* client */
#include "chatline_common.h"       /* send_chat_printf */
#include "client_main.h"

/* gui-sr */
#include "sr_diplodlg.h"
#include "sr_inteldlg.h"
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"
#include "sr_tree.h"

#include "plrdlg.h"

/* ---------------------------------------------------------------------
   Full players dialog: list of every other alive player, then a per-
   player action menu (Meet, Cancel pact / declare war, Withdraw vision,
   Intel, AI toggle, Change AI level, Close).

   Same packets gtk-3.22/plrdlg.c sends:
   - Meet:       dsend_packet_diplomacy_init_meeting_req
   - Cancel/war: dsend_packet_diplomacy_cancel_pact (CLAUSE_CEASEFIRE)
   - Vision:     dsend_packet_diplomacy_cancel_pact (CLAUSE_VISION)
   - AI toggle:  /aitoggle <name> via send_chat_printf
   - AI level:   /<level> <name> via send_chat_printf
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_PLRDLG_PLAYERS 64

enum plr_action {
  PLR_MEET = 0,
  PLR_CANCEL_PACT,
  PLR_WITHDRAW_VISION,
  PLR_INTEL,
  PLR_AI_TOGGLE,
  PLR_AI_LEVEL,
  PLR_CANCEL,
  PLR_ACTION_COUNT
};

static const char *const plr_action_labels[PLR_ACTION_COUNT] = {
  "Meet (open diplomacy)",
  "Cancel pact / declare war",
  "Withdraw shared vision",
  "Intelligence report",
  "AI toggle (/aitoggle)",
  "Change AI skill level",
  "Cancel"
};

struct plr_pick {
  int n;
  int plr_ids[MAX_PLRDLG_PLAYERS];
  char *labels[MAX_PLRDLG_PLAYERS];
  const char *label_ptrs[MAX_PLRDLG_PLAYERS];
  int chosen_id;
};

static void plr_free(struct plr_pick *st)
{
  int i;

  for (i = 0; i < st->n; i++) {
    free(st->labels[i]);
  }
  free(st);
}

static struct plr_pick *plr_collect(void)
{
  struct plr_pick *st = fc_malloc(sizeof(*st));
  struct player *me = client_player();
  char buf[160];

  st->n = 0;
  st->chosen_id = -1;

  players_iterate_alive(pplayer) {
    if (pplayer == me || is_barbarian(pplayer)) {
      continue;
    }
    if (st->n >= MAX_PLRDLG_PLAYERS) {
      break;
    }
    {
      enum diplstate_type ds = me != NULL
        ? player_diplstate_get(me, pplayer)->type : DS_NO_CONTACT;

      fc_snprintf(buf, sizeof(buf), "%s (%s) — %s",
                  player_name(pplayer),
                  nation_adjective_for_player(pplayer),
                  diplstate_type_translated_name(ds));
    }
    st->plr_ids[st->n] = player_number(pplayer);
    st->labels[st->n] = fc_strdup(buf);
    st->label_ptrs[st->n] = st->labels[st->n];
    st->n++;
  } players_iterate_alive_end;

  return st;
}

/* ---- AI level picker --------------------------------------------- */

struct ai_level_state {
  int plrno;
  int n;
  int levels[AI_LEVEL_COUNT];
  char *labels[AI_LEVEL_COUNT];
  const char *label_ptrs[AI_LEVEL_COUNT];
};

static void ai_level_free(struct ai_level_state *als)
{
  int i;

  for (i = 0; i < als->n; i++) {
    free(als->labels[i]);
  }
  free(als);
}

static void ai_level_on_pick(int idx, const char *value, void *ud)
{
  struct ai_level_state *als = ud;
  struct player *pplayer;
  char buf[128];

  (void) value;
  if (idx < 0 || idx >= als->n) {
    sr_say(_("Canceled."), 1);
    ai_level_free(als);
    return;
  }
  pplayer = player_by_number(als->plrno);
  if (pplayer == NULL) {
    sr_say(_("Player no longer exists."), 1);
    ai_level_free(als);
    return;
  }
  send_chat_printf("/%s \"%s\"",
                   ai_level_cmd((enum ai_level) als->levels[idx]),
                   player_name(pplayer));
  fc_snprintf(buf, sizeof(buf), _("Requested AI level %s for %s."),
              als->labels[idx], player_name(pplayer));
  sr_say(buf, 1);
  sr_log_append(buf);
  ai_level_free(als);
}

static void ai_level_start(int plrno)
{
  struct ai_level_state *als = fc_malloc(sizeof(*als));
  enum ai_level lvl;

  als->plrno = plrno;
  als->n = 0;
  for (lvl = 0; lvl < AI_LEVEL_COUNT; lvl++) {
    if (!is_settable_ai_level(lvl)) {
      continue;
    }
    als->levels[als->n] = (int) lvl;
    als->labels[als->n] = fc_strdup(ai_level_name(lvl));
    als->label_ptrs[als->n] = als->labels[als->n];
    als->n++;
  }
  if (als->n == 0) {
    sr_say(_("No AI levels are settable."), 1);
    free(als);
    return;
  }
  sr_pick_enum(_("New AI skill level"),
               als->label_ptrs, als->n, 0, ai_level_on_pick, als);
}

/* ---- per-player action dispatch ---------------------------------- */

static void plr_do_cancel_pact(struct player *pplayer, enum clause_type clause)
{
  char buf[160];
  enum diplstate_type oldstate;

  if (client_player() == NULL) {
    return;
  }
  oldstate = player_diplstate_get(client_player(), pplayer)->type;
  dsend_packet_diplomacy_cancel_pact(&client.conn,
                                     player_number(pplayer), clause);
  if (clause == CLAUSE_VISION) {
    fc_snprintf(buf, sizeof(buf),
                _("Withdrew shared vision from %s."), player_name(pplayer));
  } else {
    fc_snprintf(buf, sizeof(buf),
                _("Requested cancellation of %s with %s (new state: %s)."),
                diplstate_type_translated_name(oldstate),
                player_name(pplayer),
                diplstate_type_translated_name(cancel_pact_result(oldstate)));
  }
  sr_say(buf, 1);
  sr_log_append(buf);
}

static void plr_on_action(int action, const char *value, void *ud)
{
  struct plr_pick *st = ud;
  struct player *pplayer = player_by_number(st->chosen_id);
  char buf[128];

  (void) value;
  if (pplayer == NULL) {
    sr_say(_("Player no longer exists."), 1);
    plr_free(st);
    return;
  }

  switch (action) {
  case PLR_MEET:
    if (could_meet_with_player(client_player(), pplayer)) {
      dsend_packet_diplomacy_init_meeting_req(&client.conn,
                                              player_number(pplayer));
      fc_snprintf(buf, sizeof(buf), _("Meeting requested with %s."),
                  player_name(pplayer));
      sr_say(buf, 1);
      sr_log_append(buf);
    } else {
      sr_say(_("Cannot meet with this player."), 1);
    }
    break;
  case PLR_CANCEL_PACT:
    plr_do_cancel_pact(pplayer, CLAUSE_CEASEFIRE);
    break;
  case PLR_WITHDRAW_VISION:
    plr_do_cancel_pact(pplayer, CLAUSE_VISION);
    break;
  case PLR_INTEL:
    sr_inteldlg_open(pplayer);
    break;
  case PLR_AI_TOGGLE:
    send_chat_printf("/aitoggle \"%s\"", player_name(pplayer));
    fc_snprintf(buf, sizeof(buf), _("Toggled AI control for %s."),
                player_name(pplayer));
    sr_say(buf, 1);
    sr_log_append(buf);
    break;
  case PLR_AI_LEVEL: {
    int plrno = st->chosen_id;

    plr_free(st);
    ai_level_start(plrno);
    return;   /* don't free twice */
  }
  case PLR_CANCEL:
  default:
    sr_say(_("Canceled."), 1);
    break;
  }
  plr_free(st);
}

static void plr_on_player(int idx, const char *value, void *ud)
{
  struct plr_pick *st = ud;

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    plr_free(st);
    return;
  }
  st->chosen_id = st->plr_ids[idx];
  sr_pick_enum(_("Action"),
               plr_action_labels, PLR_ACTION_COUNT, PLR_MEET,
               plr_on_action, st);
}

#endif /* _WIN32 */

/**********************************************************************//**
  Display the player list dialog.  Optionally raise it.
**************************************************************************/
void popup_players_dialog(bool raise)
{
#ifdef _WIN32
  struct plr_pick *st;
  char buf[96];

  (void) raise;

  if (client_player() == NULL) {
    sr_say(_("Not in a game."), 1);
    return;
  }

  st = plr_collect();
  if (st->n == 0) {
    sr_say(_("No other players to show."), 1);
    plr_free(st);
    return;
  }

  fc_snprintf(buf, sizeof(buf), _("%d other player(s):"), st->n);
  sr_log_append(buf);
  {
    int i;

    for (i = 0; i < st->n; i++) {
      char line[192];

      fc_snprintf(line, sizeof(line), _("  %d. %s"), i + 1, st->labels[i]);
      sr_log_append(line);
    }
  }

  sr_pick_enum(_("Pick a player"),
               st->label_ptrs, st->n, 0, plr_on_player, st);
#else
  (void) raise;
#endif /* _WIN32 */
}

/**********************************************************************//**
  Update all information in the player list dialog.
**************************************************************************/
void real_players_dialog_update(void *unused)
{
  (void) unused;
  sr_diplodlg_refresh();
}
