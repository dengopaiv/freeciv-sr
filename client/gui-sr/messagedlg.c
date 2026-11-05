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
#include "events.h"

/* client */
#include "options.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_pick.h"
#include "sr_pick_common.h"
#include "sr_speech.h"

#include "messagedlg.h"

/* ---------------------------------------------------------------------
   Message routing options.

   For each event the client picks zero or more destinations
   (Out output / Mes messages window / Pop popup), tracked in
   messages_where[ev]. The SR picker walks: pick event -> pick combo.
   On commit, updates messages_where directly. Same model as
   gui-gtk-3.22/messagedlg.c, no packets to send.
   ------------------------------------------------------------------ */

#ifdef _WIN32

#define MAX_MSG_EVENTS 256

struct msg_pick {
  struct sr_pick_list events;      /* id = enum event_type */
  enum event_type chosen;
};

static void describe_dest(int where, char *out, size_t outsz)
{
  bool first = TRUE;

  out[0] = '\0';
  if (where & MW_OUTPUT) {
    fc_strlcat(out, "Out", outsz);
    first = FALSE;
  }
  if (where & MW_MESSAGES) {
    if (!first) {
      fc_strlcat(out, "+", outsz);
    }
    fc_strlcat(out, "Mes", outsz);
    first = FALSE;
  }
  if (where & MW_POPUP) {
    if (!first) {
      fc_strlcat(out, "+", outsz);
    }
    fc_strlcat(out, "Pop", outsz);
    first = FALSE;
  }
  if (first) {
    fc_strlcat(out, "(suppressed)", outsz);
  }
}

/* All 8 destination combinations, in a stable order so the picker
   index encodes the bitfield directly. */
static const struct { int bits; const char *name; } combo_table[8] = {
  { 0,                                  "Suppressed" },
  { MW_OUTPUT,                          "Out only" },
  { MW_MESSAGES,                        "Mes only" },
  { MW_POPUP,                           "Pop only" },
  { MW_OUTPUT | MW_MESSAGES,            "Out + Mes" },
  { MW_OUTPUT | MW_POPUP,               "Out + Pop" },
  { MW_MESSAGES | MW_POPUP,             "Mes + Pop" },
  { MW_OUTPUT | MW_MESSAGES | MW_POPUP, "Out + Mes + Pop" }
};

static int combo_to_index(int bits)
{
  int i;

  for (i = 0; i < 8; i++) {
    if (combo_table[i].bits == bits) {
      return i;
    }
  }
  return 0;
}

static void msg_free(struct msg_pick *st)
{
  sr_pick_list_free(&st->events);
  free(st);
}

static void msg_on_combo(int idx, const char *value, void *ud)
{
  struct msg_pick *st = ud;
  char buf[160];
  char dest[64];

  (void) value;
  if (idx < 0 || idx >= 8) {
    sr_say(_("Canceled."), 1);
    msg_free(st);
    return;
  }
  messages_where[st->chosen] = combo_table[idx].bits;
  describe_dest(combo_table[idx].bits, dest, sizeof(dest));
  fc_snprintf(buf, sizeof(buf),
              _("\"%s\" -> %s."),
              get_event_message_text(st->chosen), dest);
  sr_say(buf, 1);
  sr_log_append(buf);
  msg_free(st);
}

static void msg_on_event(int idx, const char *value, void *ud)
{
  struct msg_pick *st = ud;
  static const char *combo_labels[8];
  int cur_idx;
  int i;

  (void) value;
  if (idx < 0 || idx >= st->events.n) {
    sr_say(_("Canceled."), 1);
    msg_free(st);
    return;
  }
  st->chosen = (enum event_type)st->events.ids[idx];

  for (i = 0; i < 8; i++) {
    combo_labels[i] = combo_table[i].name;
  }
  cur_idx = combo_to_index(messages_where[st->chosen]);

  sr_pick_enum(_("New routing for this event"),
               combo_labels, 8, cur_idx, msg_on_combo, st);
}

#endif /* _WIN32 */

void popup_messageopt_dialog(void)
{
#ifdef _WIN32
  struct msg_pick *st = fc_malloc(sizeof(*st));
  char buf[256];

  sr_pick_list_init(&st->events, MAX_MSG_EVENTS);

  /* Walk events sorted by translated text so the picker reads naturally. */
  sorted_event_iterate(ev) {
    char dest[64];

    describe_dest(messages_where[ev], dest, sizeof(dest));
    fc_snprintf(buf, sizeof(buf), "%s [%s]",
                get_event_message_text(ev), dest);
    if (!sr_pick_list_append(&st->events, (int)ev, buf)) {
      break;
    }
  } sorted_event_iterate_end;

  if (st->events.n == 0) {
    sr_say(_("No message events to configure."), 1);
    msg_free(st);
    return;
  }

  fc_snprintf(buf, sizeof(buf),
              _("Message routing — %d event(s). Pick one to change."),
              st->events.n);
  sr_say(buf, 1);
  sr_log_append(buf);

  sr_pick_enum(_("Pick event"),
               st->events.label_ptrs, st->events.n, 0, msg_on_event, st);
#endif /* _WIN32 */
}
