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

/* common/aicore */
#include "cm.h"

/* client */
#include "client_main.h"

/* client/agents */
#include "cma_core.h"
#include "cma_fec.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_speech.h"

#include "sr_cma.h"

#ifdef _WIN32

/* ---- top-level action menu ----------------------------------------- */

enum cma_action {
  CMA_VIEW = 0,
  CMA_APPLY,
  CMA_SAVE,
  CMA_DELETE,
  CMA_EDIT,
  CMA_RELEASE,
  CMA_CANCEL,
  CMA_ACTION_COUNT
};

static const char *const cma_action_labels[CMA_ACTION_COUNT] = {
  "View status",
  "Apply preset",
  "Save current parameters as preset",
  "Delete preset",
  "Edit factors and surpluses",
  "Release agent (manual control)",
  "Cancel"
};

/* ---- preset picker state ------------------------------------------- */

#define MAX_CMA_PRESETS 64

struct preset_pick {
  struct city *pcity;
  int n;
  char *names[MAX_CMA_PRESETS];
  const char *name_ptrs[MAX_CMA_PRESETS];
  int indices[MAX_CMA_PRESETS];
};

static struct preset_pick *preset_collect(struct city *pcity)
{
  struct preset_pick *st = fc_malloc(sizeof(*st));
  int i;
  int total = cmafec_preset_num();

  st->pcity = pcity;
  st->n = 0;
  for (i = 0; i < total && st->n < MAX_CMA_PRESETS; i++) {
    char *d = cmafec_preset_get_descr(i);

    st->names[st->n] = fc_strdup(d != NULL ? d : "(unnamed)");
    st->name_ptrs[st->n] = st->names[st->n];
    st->indices[st->n] = i;
    st->n++;
  }
  return st;
}

static void preset_free(struct preset_pick *st)
{
  int i;

  for (i = 0; i < st->n; i++) {
    free(st->names[i]);
  }
  free(st);
}

/* Apply a parameter to a city: release if currently managed, then
   put under agent with the new parameter. Same dance as
   gui-gtk-3.22/cma_fe.c does on preset selection. */
static void cma_apply_param(struct city *pcity,
                            const struct cm_parameter *param)
{
  cmafec_set_fe_parameter(pcity, param);
  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_release_city(pcity);
  }
  cma_put_city_under_agent(pcity, param);
}

/* ---- view status --------------------------------------------------- */

static void cma_view_status(struct city *pcity)
{
  struct cm_parameter cur;
  bool active;
  char buf[256];

  active = cma_is_city_under_agent(pcity, &cur);
  fc_snprintf(buf, sizeof(buf),
              _("CMA for %s: %s"), city_name_get(pcity),
              active ? _("ACTIVE") : _("inactive"));
  sr_log_append(buf);
  sr_say(buf, 1);

  if (!active) {
    cmafec_get_fe_parameter(pcity, &cur);
  }

  {
    const char *s = cmafec_get_short_descr(&cur);

    if (s != NULL) {
      fc_snprintf(buf, sizeof(buf), _("Parameter: %s"), s);
      sr_log_append(buf);
    }
  }
}

/* ---- apply preset -------------------------------------------------- */

static void cma_on_apply_pick(int idx, const char *value, void *ud)
{
  struct preset_pick *st = ud;
  const struct cm_parameter *p;
  char buf[160];

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    preset_free(st);
    return;
  }
  p = cmafec_preset_get_parameter(st->indices[idx]);
  if (p == NULL) {
    sr_say(_("Preset no longer exists."), 1);
    preset_free(st);
    return;
  }
  cma_apply_param(st->pcity, p);
  fc_snprintf(buf, sizeof(buf),
              _("Applied preset \"%s\" to %s; agent active."),
              st->names[idx], city_name_get(st->pcity));
  sr_say(buf, 1);
  sr_log_append(buf);
  preset_free(st);
}

/* ---- save current -------------------------------------------------- */

static void cma_save_current(struct city *pcity)
{
  struct cm_parameter p;
  char name[64];
  char buf[160];

  cmafec_get_fe_parameter(pcity, &p);
  fc_snprintf(name, sizeof(name), _("%s %d"),
              city_name_get(pcity), cmafec_preset_num() + 1);
  cmafec_preset_add(name, &p);
  fc_snprintf(buf, sizeof(buf),
              _("Saved current parameters as preset \"%s\"."), name);
  sr_say(buf, 1);
  sr_log_append(buf);
}

/* ---- delete preset ------------------------------------------------- */

static void cma_on_delete_pick(int idx, const char *value, void *ud)
{
  struct preset_pick *st = ud;
  char buf[128];

  (void) value;
  if (idx < 0 || idx >= st->n) {
    sr_say(_("Canceled."), 1);
    preset_free(st);
    return;
  }
  fc_snprintf(buf, sizeof(buf), _("Deleted preset \"%s\"."), st->names[idx]);
  cmafec_preset_remove(st->indices[idx]);
  sr_say(buf, 1);
  sr_log_append(buf);
  preset_free(st);
}

/* ---- edit factors/surpluses --------------------------------------- */

struct cma_edit {
  struct city *pcity;
  struct cm_parameter p;
  int o;            /* current output index */
  int phase;        /* 0 = surplus, 1 = factor */
};

static void edit_ask_next(struct cma_edit *e);

static void edit_finalize(struct cma_edit *e)
{
  cma_apply_param(e->pcity, &e->p);
  {
    char buf[128];

    fc_snprintf(buf, sizeof(buf),
                _("Updated CMA parameters for %s; agent active."),
                city_name_get(e->pcity));
    sr_say(buf, 1);
    sr_log_append(buf);
  }
  free(e);
}

static void edit_on_max_growth(int idx, const char *value, void *ud)
{
  struct cma_edit *e = ud;

  (void) value;
  e->p.max_growth = (idx == 0);
  edit_finalize(e);
}

static void edit_on_require_happy(int idx, const char *value, void *ud)
{
  struct cma_edit *e = ud;
  static const char *const yn[2] = { "Yes", "No" };

  (void) value;
  e->p.require_happy = (idx == 0);
  sr_pick_enum(_("Maximize growth?"), yn, 2,
               e->p.max_growth ? 0 : 1, edit_on_max_growth, e);
}

static void edit_on_happy_factor(int v, void *ud)
{
  struct cma_edit *e = ud;
  static const char *const yn[2] = { "Yes", "No" };

  if (v < 1) {
    v = 1;
  }
  e->p.happy_factor = v;
  sr_pick_enum(_("Require happy?"), yn, 2,
               e->p.require_happy ? 0 : 1, edit_on_require_happy, e);
}

static void edit_on_value(int v, void *ud)
{
  struct cma_edit *e = ud;

  if (e->phase == 0) {
    e->p.minimal_surplus[e->o] = v;
    e->phase = 1;
  } else {
    if (v < 0) {
      v = 0;
    }
    e->p.factor[e->o] = v;
    e->phase = 0;
    e->o++;
  }
  edit_ask_next(e);
}

static void edit_ask_next(struct cma_edit *e)
{
  char title[128];

  if (e->o >= O_LAST) {
    /* Done with outputs — ask happy_factor and the booleans. */
    fc_snprintf(title, sizeof(title), _("Happy factor (>=1)"));
    sr_pick_int(title, e->p.happy_factor, 1, 100,
                edit_on_happy_factor, e);
    return;
  }

  if (e->phase == 0) {
    fc_snprintf(title, sizeof(title),
                _("%s minimal surplus (-100..100)"),
                get_output_name(e->o));
    sr_pick_int(title, e->p.minimal_surplus[e->o], -100, 100,
                edit_on_value, e);
  } else {
    fc_snprintf(title, sizeof(title),
                _("%s factor (0..100)"),
                get_output_name(e->o));
    sr_pick_int(title, e->p.factor[e->o], 0, 100,
                edit_on_value, e);
  }
}

static void cma_edit_start(struct city *pcity)
{
  struct cma_edit *e = fc_malloc(sizeof(*e));

  e->pcity = pcity;
  cmafec_get_fe_parameter(pcity, &e->p);
  e->o = 0;
  e->phase = 0;

  sr_log_append(_("Editing CMA parameters — surplus then factor for each "
                  "output, then happy_factor, then booleans."));
  edit_ask_next(e);
}

/* ---- release ------------------------------------------------------- */

static void cma_release(struct city *pcity)
{
  char buf[128];

  if (cma_is_city_under_agent(pcity, NULL)) {
    cma_release_city(pcity);
    fc_snprintf(buf, sizeof(buf),
                _("CMA released for %s — manual control."),
                city_name_get(pcity));
  } else {
    fc_snprintf(buf, sizeof(buf),
                _("CMA was not active on %s."),
                city_name_get(pcity));
  }
  sr_say(buf, 1);
  sr_log_append(buf);
}

/* ---- top-level dispatcher ------------------------------------------ */

struct cma_top {
  struct city *pcity;
};

static void cma_on_action(int action, const char *value, void *ud)
{
  struct cma_top *t = ud;
  struct city *pcity = t->pcity;

  (void) value;
  free(t);

  switch (action) {
  case CMA_VIEW:
    cma_view_status(pcity);
    return;
  case CMA_APPLY: {
    struct preset_pick *st = preset_collect(pcity);

    if (st->n == 0) {
      sr_say(_("No CMA presets defined."), 1);
      preset_free(st);
      return;
    }
    sr_pick_enum(_("Apply which preset?"), st->name_ptrs, st->n, 0,
                 cma_on_apply_pick, st);
    return;
  }
  case CMA_SAVE:
    cma_save_current(pcity);
    return;
  case CMA_DELETE: {
    struct preset_pick *st = preset_collect(pcity);

    if (st->n == 0) {
      sr_say(_("No CMA presets to delete."), 1);
      preset_free(st);
      return;
    }
    sr_pick_enum(_("Delete which preset?"), st->name_ptrs, st->n, 0,
                 cma_on_delete_pick, st);
    return;
  }
  case CMA_EDIT:
    cma_edit_start(pcity);
    return;
  case CMA_RELEASE:
    cma_release(pcity);
    return;
  case CMA_CANCEL:
  default:
    sr_say(_("Canceled."), 1);
    return;
  }
}

#endif /* _WIN32 */

void sr_cma_open(struct city *pcity)
{
#ifdef _WIN32
  struct cma_top *t;
  char buf[128];

  if (pcity == NULL) {
    sr_say(_("No city for CMA."), 1);
    return;
  }
  if (city_owner(pcity) != client_player()) {
    sr_say(_("CMA only manages your own cities."), 1);
    return;
  }

  fc_snprintf(buf, sizeof(buf),
              _("CMA for %s: %s. %d preset(s) saved."),
              city_name_get(pcity),
              cma_is_city_under_agent(pcity, NULL)
                ? _("ACTIVE") : _("inactive"),
              cmafec_preset_num());
  sr_log_append(buf);
  sr_say(buf, 1);

  t = fc_malloc(sizeof(*t));
  t->pcity = pcity;
  sr_pick_enum(_("CMA action"),
               cma_action_labels, CMA_ACTION_COUNT, CMA_VIEW,
               cma_on_action, t);
#endif /* _WIN32 */
}
