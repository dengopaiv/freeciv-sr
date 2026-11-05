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
#include <string.h>

/* utility */
#include "fcintl.h"
#include "log.h"
#include "mem.h"
#include "shared.h"
#include "support.h"

/* common */
#include "effects.h"
#include "game.h"
#include "government.h"
#include "multipliers.h"
#include "packets.h"
#include "player.h"

/* client */
#include "client_main.h"

/* gui-sr */
#include "gui_sr.h"
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"

#include "ratesdlg.h"

/* ---------------------------------------------------------------------
   Rates (tax / luxury / science) — interactive, SR-friendly flow.

   Rates are integers in [0, maxrate] summing to 100 and stepping in
   units of 10. We walk the user through three sr_pick_int prompts,
   clamp and snap between steps, and commit with the same packet the
   gtk-3.22 dialog sends.
   ------------------------------------------------------------------ */

struct rates_state {
  int tax;
  int lux;
  int sci;
  int maxrate;
};

#ifdef _WIN32
static int snap_to_10(int v)
{
  if (v < 0) {
    v = 0;
  }
  return (v / 10) * 10;
}

static void rates_commit(struct rates_state *s)
{
  char buf[128];

  dsend_packet_player_rates(&client.conn, s->tax, s->lux, s->sci);
  fc_snprintf(buf, sizeof(buf),
              _("Rates sent: tax %d%%, luxury %d%%, science %d%%."),
              s->tax, s->lux, s->sci);
  sr_say(buf, 1);
  sr_log_append(buf);
  free(s);
}

static void rates_on_lux(int value, void *ud)
{
  struct rates_state *s = ud;
  int sci;
  int lux_cap;

  value = snap_to_10(value);
  lux_cap = MIN(100 - s->tax, s->maxrate);
  if (value > lux_cap) {
    value = lux_cap;
  }
  s->lux = value;

  sci = 100 - s->tax - s->lux;
  if (sci < 0 || sci > s->maxrate) {
    char msg[160];

    fc_snprintf(msg, sizeof(msg),
                _("Can't balance: tax %d + luxury %d leaves science %d "
                  "(max %d). Canceled."),
                s->tax, s->lux, sci, s->maxrate);
    sr_say(msg, 1);
    sr_log_append(msg);
    free(s);
    return;
  }
  s->sci = sci;
  rates_commit(s);
}

static void rates_on_tax(int value, void *ud)
{
  struct rates_state *s = ud;
  char title[96];
  int lux_cap;
  int lux_preset;

  value = snap_to_10(value);
  if (value > s->maxrate) {
    value = s->maxrate;
  }
  s->tax = value;

  lux_cap = MIN(100 - s->tax, s->maxrate);
  lux_preset = MIN(s->lux, lux_cap);

  fc_snprintf(title, sizeof(title),
              _("Luxury %% (0 to %d, step 10; science fills rest)"),
              lux_cap);
  sr_pick_int(title, lux_preset, 0, lux_cap, rates_on_lux, s);
}
#endif /* _WIN32 */

/**********************************************************************//**
  Popup (or raise) the (tax/science/luxury) rates selection dialog.
**************************************************************************/
void popup_rates_dialog(void)
{
#ifdef _WIN32
  struct rates_state *s;
  struct player *me = client_player();
  char buf[160];
  char title[96];

  if (me == NULL || !can_client_issue_orders()) {
    sr_say(_("Can't change rates right now."), 1);
    return;
  }

  s = fc_malloc(sizeof(*s));
  s->tax = me->economic.tax;
  s->lux = me->economic.luxury;
  s->sci = me->economic.science;
  s->maxrate = get_player_bonus(me, EFT_MAX_RATES);
  if (s->maxrate <= 0 || s->maxrate > 100) {
    s->maxrate = 100;
  }

  fc_snprintf(buf, sizeof(buf),
              _("Rates under %s: tax %d%%, luxury %d%%, science %d%% "
                "(max %d%%)."),
              government_name_for_player(me),
              s->tax, s->lux, s->sci, s->maxrate);
  sr_say(buf, 1);
  sr_log_append(buf);

  fc_snprintf(title, sizeof(title),
              _("Tax %% (0 to %d, step 10)"), s->maxrate);
  sr_pick_int(title, s->tax, 0, s->maxrate, rates_on_tax, s);
#endif /* _WIN32 */
}

/* ---------------------------------------------------------------------
   Multipliers (policies) — walk each one via sr_pick_int, then send.
   ------------------------------------------------------------------ */

#ifdef _WIN32
struct mul_state {
  int targets[MAX_NUM_MULTIPLIERS];
  int idx;                       /* next multiplier to ask about */
  int active_count;              /* how many we actually queue */
  int active_ids[MAX_NUM_MULTIPLIERS];  /* multiplier index for each slot */
};

static void mul_ask_next(struct mul_state *ms);

static void mul_commit(struct mul_state *ms)
{
  struct packet_player_multiplier pkt;
  int i;
  char buf[96];

  memset(&pkt, 0, sizeof(pkt));
  pkt.count = multiplier_count();

  /* Start from current values; overwrite the ones we asked about. */
  multipliers_iterate(pmul) {
    Multiplier_type_id mi = multiplier_index(pmul);
    pkt.multipliers[mi] = player_multiplier_target_value(client_player(), pmul);
  } multipliers_iterate_end;

  for (i = 0; i < ms->active_count; i++) {
    pkt.multipliers[ms->active_ids[i]] = ms->targets[i];
  }

  send_packet_player_multiplier(&client.conn, &pkt);
  fc_snprintf(buf, sizeof(buf),
              _("Policy changes sent (%d adjustable)."), ms->active_count);
  sr_say(buf, 1);
  sr_log_append(buf);
  free(ms);
}

static void mul_on_value(int value, void *ud)
{
  struct mul_state *ms = ud;
  int slot = ms->idx;
  struct multiplier *pmul = multiplier_by_number(ms->active_ids[slot]);

  if (pmul != NULL) {
    /* Snap to step, clamp to [start, stop]. */
    if (pmul->step > 0) {
      int steps = (value - pmul->start + pmul->step / 2) / pmul->step;

      value = pmul->start + steps * pmul->step;
    }
    if (value < pmul->start) {
      value = pmul->start;
    }
    if (value > pmul->stop) {
      value = pmul->stop;
    }
  }
  ms->targets[slot] = value;
  ms->idx++;
  mul_ask_next(ms);
}

static void mul_ask_next(struct mul_state *ms)
{
  struct multiplier *pmul;
  char title[128];
  int cur;

  if (ms->idx >= ms->active_count) {
    mul_commit(ms);
    return;
  }
  pmul = multiplier_by_number(ms->active_ids[ms->idx]);
  if (pmul == NULL) {
    ms->idx++;
    mul_ask_next(ms);
    return;
  }
  cur = player_multiplier_target_value(client_player(), pmul);
  fc_snprintf(title, sizeof(title),
              _("%s (range %d..%d, step %d)"),
              multiplier_name_translation(pmul),
              pmul->start, pmul->stop, pmul->step);
  sr_pick_int(title, cur, pmul->start, pmul->stop, mul_on_value, ms);
}
#endif /* _WIN32 */

/**********************************************************************//**
  Popup the multipliers (policies) dialog.
**************************************************************************/
void popup_multiplier_dialog(void)
{
#ifdef _WIN32
  struct mul_state *ms;
  struct player *me = client_player();
  int count;

  if (me == NULL) {
    sr_say(_("Not in a game."), 1);
    return;
  }
  if (multiplier_count() == 0) {
    sr_say(_("This ruleset has no adjustable policies."), 1);
    return;
  }

  /* Announce the current state first, then walk through. */
  {
    char line[160];

    sr_log_append(_("Policies (current / target):"));
    multipliers_iterate(pmul) {
      int val = player_multiplier_value(me, pmul);
      int tgt = player_multiplier_target_value(me, pmul);

      fc_snprintf(line, sizeof(line), _("  %s: %d / %d"),
                  multiplier_name_translation(pmul), val, tgt);
      sr_log_append(line);
    } multipliers_iterate_end;
  }

  if (!can_client_issue_orders()) {
    sr_say(_("View-only: policies can't be changed now."), 1);
    return;
  }

  ms = fc_malloc(sizeof(*ms));
  memset(ms, 0, sizeof(*ms));
  count = 0;
  multipliers_iterate(pmul) {
    if (multiplier_can_be_changed(pmul, me)) {
      ms->active_ids[count++] = multiplier_index(pmul);
    }
  } multipliers_iterate_end;
  ms->active_count = count;

  if (count == 0) {
    sr_say(_("No policies are currently adjustable."), 1);
    free(ms);
    return;
  }
  mul_ask_next(ms);
#endif /* _WIN32 */
}

/**********************************************************************//**
  Update multipliers (policies) dialog. No persistent window to refresh
  in the SR client — target values are read fresh each time the dialog
  is opened, so this callback is a no-op.
**************************************************************************/
void real_multipliers_dialog_update(void *unused)
{
  (void) unused;
}
