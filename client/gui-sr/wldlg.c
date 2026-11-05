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
#include "mem.h"
#include "support.h"

/* common */
#include "city.h"
#include "requirements.h"
#include "worklist.h"

/* client */
#include "citydlg_common.h"        /* city_set_worklist */
#include "client_main.h"
#include "control.h"               /* head_of_units_in_focus */
#include "global_worklist.h"
#include "tile.h"

/* gui-sr */
#include "sr_citydlg.h"
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"

#include "wldlg.h"

/**********************************************************************//**
  If the worklist report is open, force its contents to be updated.
  The SR client has no persistent worklist-templates window — the per-city
  editor in sr_citydlg redraws on its own; the global-worklists report
  is a one-shot picker rebuilt each time it's opened.
**************************************************************************/
void update_worklist_report_dialog(void)
{
  sr_citydlg_refresh_worklist();
}

/* ---------------------------------------------------------------------
   Global worklists report — accessible-friendly text UI.

   "worklists" command:
     1. Logs every saved global worklist with its index, name, and item
        count, plus item names for each.
     2. Opens an action chooser: View, New (empty), Save current city as
        new, Apply to current city, Delete, Cancel.
     3. Cascades through sr_pick_enum picks; commits via the existing
        global_worklist_* / city_set_worklist API — same calls
        gui-gtk-3.22/wldlg.c uses.
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_WL_PICK 64

enum wl_action {
  WLA_VIEW = 0,
  WLA_NEW_EMPTY,
  WLA_SAVE_CITY,
  WLA_APPLY,
  WLA_DELETE,
  WLA_CANCEL,
  WLA_COUNT
};

static const char *const wl_action_labels[WLA_COUNT] = {
  "View items",
  "New empty worklist",
  "Save current city's worklist as new",
  "Apply to current city",
  "Delete",
  "Cancel"
};

struct wl_pick_state {
  int n;
  int wl_ids[MAX_WL_PICK];
  char *names[MAX_WL_PICK];
  const char *name_ptrs[MAX_WL_PICK];
};

static struct city *current_focus_city(void)
{
  struct unit *punit;

  if (get_num_units_in_focus() == 0) {
    return NULL;
  }
  punit = head_of_units_in_focus();
  if (punit == NULL) {
    return NULL;
  }
  return tile_city(unit_tile(punit));
}

static const char *wl_universal_name(const struct universal *u, char *buf,
                                     size_t bufsz)
{
  if (u == NULL) {
    return "?";
  }
  return universal_name_translation(u, buf, bufsz);
}

static void wl_log_items(const struct global_worklist *pgwl, int idx)
{
  const struct worklist *pwl = global_worklist_get(pgwl);
  int len = pwl != NULL ? worklist_length(pwl) : 0;
  char line[160];
  char ubuf[96];
  int i;
  struct universal u;

  fc_snprintf(line, sizeof(line), _("Worklist #%d \"%s\" (%d item(s)):"),
              idx, global_worklist_name(pgwl), len);
  sr_log_append(line);
  for (i = 0; i < len; i++) {
    if (worklist_peek_ith(pwl, &u, i)) {
      fc_snprintf(line, sizeof(line), _("  %d. %s"), i + 1,
                  wl_universal_name(&u, ubuf, sizeof(ubuf)));
      sr_log_append(line);
    }
  }
}

static struct wl_pick_state *wl_collect(void)
{
  struct wl_pick_state *st = fc_malloc(sizeof(*st));
  int i = 0;

  st->n = 0;
  global_worklists_iterate(pgwl) {
    if (i >= MAX_WL_PICK) {
      break;
    }
    st->wl_ids[i] = global_worklist_id(pgwl);
    st->names[i] = fc_strdup(global_worklist_name(pgwl));
    st->name_ptrs[i] = st->names[i];
    i++;
  } global_worklists_iterate_end;
  st->n = i;
  return st;
}

static void wl_free(struct wl_pick_state *st)
{
  int i;

  for (i = 0; i < st->n; i++) {
    free(st->names[i]);
  }
  free(st);
}

static void wl_on_pick_view(int idx, const char *value, void *ud)
{
  struct wl_pick_state *st = ud;
  struct global_worklist *pgwl;

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    wl_free(st);
    return;
  }
  pgwl = global_worklist_by_id(st->wl_ids[idx]);
  if (pgwl != NULL) {
    wl_log_items(pgwl, idx + 1);
    sr_say(_("Worklist contents written to log."), 1);
  } else {
    sr_say(_("Worklist no longer exists."), 1);
  }
  wl_free(st);
}

static void wl_on_pick_apply(int idx, const char *value, void *ud)
{
  struct wl_pick_state *st = ud;
  struct global_worklist *pgwl;
  struct city *pcity;
  const struct worklist *pwl;
  char buf[128];

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    wl_free(st);
    return;
  }
  pcity = current_focus_city();
  if (pcity == NULL) {
    sr_say(_("Need a unit in a city to apply a worklist."), 1);
    wl_free(st);
    return;
  }
  pgwl = global_worklist_by_id(st->wl_ids[idx]);
  if (pgwl == NULL || (pwl = global_worklist_get(pgwl)) == NULL) {
    sr_say(_("Worklist no longer exists."), 1);
    wl_free(st);
    return;
  }
  city_set_worklist(pcity, pwl);
  fc_snprintf(buf, sizeof(buf),
              _("Applied worklist \"%s\" to %s."),
              global_worklist_name(pgwl), city_name_get(pcity));
  sr_say(buf, 1);
  sr_log_append(buf);
  wl_free(st);
}

static void wl_on_pick_delete(int idx, const char *value, void *ud)
{
  struct wl_pick_state *st = ud;
  struct global_worklist *pgwl;
  char buf[128];

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    wl_free(st);
    return;
  }
  pgwl = global_worklist_by_id(st->wl_ids[idx]);
  if (pgwl == NULL) {
    sr_say(_("Worklist no longer exists."), 1);
    wl_free(st);
    return;
  }
  fc_snprintf(buf, sizeof(buf), _("Deleted worklist \"%s\"."),
              global_worklist_name(pgwl));
  global_worklist_destroy(pgwl);
  sr_say(buf, 1);
  sr_log_append(buf);
  wl_free(st);
}

static void wl_action_dispatch(int action, struct wl_pick_state *st)
{
  switch (action) {
  case WLA_VIEW:
    if (st->n == 0) {
      sr_say(_("No worklists to view."), 1);
      wl_free(st);
      return;
    }
    sr_pick_enum(_("View which worklist?"),
                 st->name_ptrs, st->n, 0, wl_on_pick_view, st);
    return;

  case WLA_APPLY:
    if (st->n == 0) {
      sr_say(_("No worklists to apply."), 1);
      wl_free(st);
      return;
    }
    if (current_focus_city() == NULL) {
      sr_say(_("Need a unit in a city first."), 1);
      wl_free(st);
      return;
    }
    sr_pick_enum(_("Apply which worklist?"),
                 st->name_ptrs, st->n, 0, wl_on_pick_apply, st);
    return;

  case WLA_DELETE:
    if (st->n == 0) {
      sr_say(_("No worklists to delete."), 1);
      wl_free(st);
      return;
    }
    sr_pick_enum(_("Delete which worklist?"),
                 st->name_ptrs, st->n, 0, wl_on_pick_delete, st);
    return;

  case WLA_NEW_EMPTY: {
    struct global_worklist *pgwl;
    char buf[64];

    fc_snprintf(buf, sizeof(buf), _("worklist %d"), st->n + 1);
    pgwl = global_worklist_new(buf);
    if (pgwl != NULL) {
      fc_snprintf(buf, sizeof(buf),
                  _("Created empty worklist \"%s\"."),
                  global_worklist_name(pgwl));
      sr_say(buf, 1);
      sr_log_append(buf);
    } else {
      sr_say(_("Could not create worklist."), 1);
    }
    wl_free(st);
    return;
  }

  case WLA_SAVE_CITY: {
    struct city *pcity = current_focus_city();
    struct global_worklist *pgwl;
    char buf[96];

    if (pcity == NULL) {
      sr_say(_("Need a unit in a city to save its worklist."), 1);
      wl_free(st);
      return;
    }
    fc_snprintf(buf, sizeof(buf), _("from %s"), city_name_get(pcity));
    pgwl = global_worklist_new(buf);
    if (pgwl == NULL) {
      sr_say(_("Could not create worklist."), 1);
      wl_free(st);
      return;
    }
    if (!global_worklist_set(pgwl, &pcity->worklist)) {
      sr_say(_("Could not store city worklist."), 1);
      global_worklist_destroy(pgwl);
      wl_free(st);
      return;
    }
    fc_snprintf(buf, sizeof(buf),
                _("Saved %s's worklist as global \"%s\" (%d items)."),
                city_name_get(pcity), global_worklist_name(pgwl),
                worklist_length(&pcity->worklist));
    sr_say(buf, 1);
    sr_log_append(buf);
    wl_free(st);
    return;
  }

  case WLA_CANCEL:
  default:
    sr_say(_("Canceled."), 1);
    wl_free(st);
    return;
  }
}

static void wl_on_action(int idx, const char *value, void *ud)
{
  (void) value;
  wl_action_dispatch(idx, (struct wl_pick_state *) ud);
}

#endif /* _WIN32 */

/**********************************************************************//**
  Open the global-worklists report. Logs all saved worklists, then
  prompts for an action (View / New empty / Save city / Apply / Delete).
**************************************************************************/
void popup_worklists_report(void)
{
#ifdef _WIN32
  struct wl_pick_state *st = wl_collect();
  char buf[96];

  fc_snprintf(buf, sizeof(buf), _("%d global worklist(s) saved."), st->n);
  sr_say(buf, 1);
  sr_log_append(buf);

  /* Dump every worklist's contents to the log so the user can review
     before picking an action. */
  if (st->n > 0) {
    int i = 0;

    global_worklists_iterate(pgwl) {
      wl_log_items(pgwl, i + 1);
      i++;
      if (i >= st->n) {
        break;
      }
    } global_worklists_iterate_end;
  }

  sr_pick_enum(_("Worklists action"),
               wl_action_labels, WLA_COUNT, WLA_VIEW, wl_on_action, st);
#endif /* _WIN32 */
}
