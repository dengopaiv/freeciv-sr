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

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "citizens.h"
#include "city.h"
#include "game.h"
#include "nation.h"
#include "player.h"
#include "specialist.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_speech.h"

#include "sr_citizens.h"

void sr_citizens_open(struct city *pcity)
{
  char buf[160];
  int total;

  if (pcity == NULL) {
    sr_say(_("No city to inspect."), 1);
    return;
  }

  total = city_size_get(pcity);
  fc_snprintf(buf, sizeof(buf),
              _("Citizens of %s (population %d):"),
              city_name_get(pcity), total);
  sr_log_append(buf);

  /* Mood tally — final feeling, what the city actually is right now. */
  fc_snprintf(buf, sizeof(buf),
              _("  Mood: %d happy, %d content, %d unhappy, %d angry."),
              pcity->feel[CITIZEN_HAPPY][FEELING_FINAL],
              pcity->feel[CITIZEN_CONTENT][FEELING_FINAL],
              pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL],
              pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]);
  sr_log_append(buf);

  /* Specialist counts — one line per specialist type. */
  sr_log_append(_("  Specialists:"));
  specialist_type_iterate(sp) {
    int n = pcity->specialists[sp];
    const struct specialist *psp = specialist_by_number(sp);

    if (n > 0 && psp != NULL) {
      fc_snprintf(buf, sizeof(buf), _("    %s: %d"),
                  specialist_plural_translation(psp), n);
      sr_log_append(buf);
    }
  } specialist_type_iterate_end;

  /* Nationality breakdown — only meaningful if the ruleset tracks it. */
  if (game.info.citizen_nationality) {
    bool any = FALSE;

    sr_log_append(_("  Nationalities:"));
    citizens_iterate(pcity, pslot, nationality) {
      struct player *pplayer = player_slot_get_player(pslot);
      const char *name;

      if (pplayer != NULL) {
        name = nation_plural_for_player(pplayer);
      } else {
        name = _("(unknown)");
      }
      fc_snprintf(buf, sizeof(buf), _("    %s: %d"), name, (int) nationality);
      sr_log_append(buf);
      any = TRUE;
    } citizens_iterate_end;
    if (!any) {
      sr_log_append(_("    (none recorded)"));
    }
  }

  /* One-line spoken summary. */
  fc_snprintf(buf, sizeof(buf),
              _("%s: pop %d, %d happy / %d content / %d unhappy / %d angry."),
              city_name_get(pcity), total,
              pcity->feel[CITIZEN_HAPPY][FEELING_FINAL],
              pcity->feel[CITIZEN_CONTENT][FEELING_FINAL],
              pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL],
              pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]);
  sr_say(buf, 1);
}
