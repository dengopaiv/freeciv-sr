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

#include <string.h>

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "city.h"

/* client */
#include "text.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_speech.h"

#include "sr_happiness.h"

/* Append a multi-line string to the log, splitting on newline so each
   line is a separately-navigable EDIT entry for screen readers. */
static void log_block(const char *heading, const char *body)
{
  const char *line;
  const char *p;
  char buf[512];
  size_t len;

  if (heading != NULL) {
    sr_log_append(heading);
  }
  if (body == NULL || body[0] == '\0') {
    sr_log_append(_("  (no contribution)"));
    return;
  }

  line = body;
  for (p = body; ; p++) {
    if (*p == '\n' || *p == '\0') {
      len = (size_t) (p - line);
      if (len > 0) {
        if (len >= sizeof(buf) - 3) {
          len = sizeof(buf) - 3;
        }
        buf[0] = ' ';
        buf[1] = ' ';
        memcpy(buf + 2, line, len);
        buf[len + 2] = '\0';
        sr_log_append(buf);
      }
      if (*p == '\0') {
        break;
      }
      line = p + 1;
    }
  }
}

void sr_happiness_open(struct city *pcity)
{
  char header[160];
  char tally[160];

  if (pcity == NULL) {
    sr_say(_("No city to inspect."), 1);
    return;
  }

  fc_snprintf(header, sizeof(header),
              _("Happiness breakdown for %s (size %d):"),
              city_name_get(pcity), city_size_get(pcity));
  sr_log_append(header);

  log_block(_("Cities (empire size penalty):"), text_happiness_cities(pcity));
  log_block(_("Luxuries:"),                     text_happiness_luxuries(pcity));
  log_block(_("Buildings:"),                    text_happiness_buildings(pcity));
  log_block(_("Nationality:"),                  text_happiness_nationality(pcity));
  log_block(_("Units (martial / unhappy):"),    text_happiness_units(pcity));
  log_block(_("Wonders:"),                      text_happiness_wonders(pcity));

  fc_snprintf(tally, sizeof(tally),
              _("%s mood: %d happy, %d content, %d unhappy, %d angry."),
              city_name_get(pcity),
              pcity->feel[CITIZEN_HAPPY][FEELING_FINAL],
              pcity->feel[CITIZEN_CONTENT][FEELING_FINAL],
              pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL],
              pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]);
  sr_log_append(tally);
  sr_say(tally, 1);
}
