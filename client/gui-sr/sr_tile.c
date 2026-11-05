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
#include "game.h"
#include "map.h"
#include "nation.h"
#include "player.h"
#include "tile.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "climap.h"
#include "client_main.h"
#include "control.h"
#include "text.h"

/* gui-sr */
#include "sr_speech.h"

#include "sr_tile.h"

#define SR_TILE_BUF 2048

/* Flatten newlines and surrounding whitespace in src into ". " separators
   in dst. Collapses runs of \n, trims leading dot-space, NUL-terminates. */
static void flatten_lines(char *dst, size_t dst_size, const char *src)
{
  size_t di = 0;
  bool at_line_start = TRUE;
  bool pending_sep = FALSE;

  if (dst_size == 0) {
    return;
  }

  while (*src != '\0' && di + 1 < dst_size) {
    char c = *src++;

    if (c == '\n' || c == '\r') {
      if (di > 0) {
        pending_sep = TRUE;
      }
      at_line_start = TRUE;
      continue;
    }
    if (at_line_start && (c == ' ' || c == '\t')) {
      /* Skip leading whitespace on each logical line. */
      continue;
    }
    at_line_start = FALSE;
    if (pending_sep) {
      /* Avoid doubling punctuation: if previous char already ends a
         sentence, just use a space. */
      char prev = di > 0 ? dst[di - 1] : '\0';

      if (prev == '.' || prev == '!' || prev == '?' || prev == ':'
          || prev == ';' || prev == ',') {
        if (di + 1 < dst_size) {
          dst[di++] = ' ';
        }
      } else {
        if (di + 2 < dst_size) {
          dst[di++] = '.';
          dst[di++] = ' ';
        } else if (di + 1 < dst_size) {
          dst[di++] = ' ';
        }
      }
      pending_sep = FALSE;
    }
    dst[di++] = c;
  }
  dst[di] = '\0';
}

/* Build a terse single-line summary into buf. */
static void build_terse(char *buf, size_t size, const struct tile *ptile)
{
  enum known_type known = client_tile_get_known(ptile);
  struct city *pcity;
  struct unit *punit;
  const char *infratext;
  int tile_x, tile_y;

  if (known == TILE_UNKNOWN) {
    fc_strlcpy(buf, _("Unknown tile."), size);
    return;
  }

  index_to_map_pos(&tile_x, &tile_y, tile_index(ptile));
  fc_snprintf(buf, size, "%d,%d: ", tile_x, tile_y);

  /* Terrain / river / resource. */
  cat_snprintf(buf, size, "%s",
               tile_get_info_text(ptile, TRUE, 0));

  /* Territory owner (brief). */
  {
    struct player *towner = tile_owner(ptile);

    if (towner != NULL) {
      if (client_player() != NULL && towner == client_player()) {
        cat_snprintf(buf, size, ", %s", _("ours"));
      } else {
        cat_snprintf(buf, size, ", %s",
                     nation_adjective_for_player(towner));
      }
    }
  }

  /* City. */
  pcity = tile_city(ptile);
  if (pcity != NULL) {
    cat_snprintf(buf, size, ". %s %s (%d)",
                 _("City:"), city_name_get(pcity), city_size_get(pcity));
  }

  /* Units. */
  punit = find_visible_unit((struct tile *)ptile);
  if (punit != NULL) {
    int count = unit_list_size(ptile->units);
    struct player *owner = unit_owner(punit);
    const char *nation_adj = owner != NULL
      ? nation_adjective_for_player(owner) : "";

    if (count > 1) {
      cat_snprintf(buf, size, ". %s %d %s (%s)",
                   _("Units:"), count,
                   utype_name_translation(unit_type_get(punit)),
                   nation_adj);
    } else {
      cat_snprintf(buf, size, ". %s %s (%s)",
                   _("Unit:"),
                   utype_name_translation(unit_type_get(punit)),
                   nation_adj);
    }
  }

  /* Infrastructure. */
  infratext = get_infrastructure_text(ptile->extras);
  if (infratext != NULL && *infratext != '\0') {
    cat_snprintf(buf, size, ". %s", infratext);
  }

  /* Yield. */
  {
    const char *out = get_tile_output_text(ptile);

    if (out != NULL && *out != '\0') {
      cat_snprintf(buf, size, ". %s", out);
    }
  }

  /* Fog state marker. */
  if (known == TILE_KNOWN_UNSEEN) {
    cat_snprintf(buf, size, ". %s", _("fogged"));
  }

  cat_snprintf(buf, size, ".");
}

void sr_tile_say(const struct tile *ptile, bool terse)
{
  char out[SR_TILE_BUF];

  if (ptile == NULL) {
    sr_say(_("No tile."), 1);
    return;
  }

  if (terse) {
    build_terse(out, sizeof(out), ptile);
    sr_say(out, 1);
    return;
  }

  /* Thorough: delegate to popup_info_text for full civ-authentic content,
     then flatten its line structure into continuous speech.  Strip lines
     that are only useful for developers (native coordinates, latitude)
     to keep the spoken output focused on gameplay. */
  {
    const char *raw = popup_info_text((struct tile *)ptile);
    char filtered[SR_TILE_BUF];
    char *dst;
    const char *src;
    size_t remaining;

    if (raw == NULL) {
      sr_say(_("No tile."), 1);
      return;
    }

    /* Copy raw but skip lines starting with "Native coordinates"
       or "Latitude". */
    dst = filtered;
    remaining = sizeof(filtered) - 1;
    src = raw;
    while (*src != '\0' && remaining > 0) {
      const char *eol = strchr(src, '\n');
      size_t line_len = eol != NULL ? (size_t)(eol - src + 1)
                                    : strlen(src);

      if (strncmp(src, _("Native coordinates:"), 19) != 0
          && strncmp(src, _("Latitude:"), 9) != 0) {
        size_t copy = line_len < remaining ? line_len : remaining;

        memcpy(dst, src, copy);
        dst += copy;
        remaining -= copy;
      }
      src += line_len;
    }
    *dst = '\0';

    flatten_lines(out, sizeof(out), filtered);
    sr_say(out, 1);
  }
}

void sr_tile_say_where(const struct tile *ptile)
{
  char buf[128];
  int tile_x, tile_y;

  if (ptile == NULL) {
    sr_say(_("No tile."), 1);
    return;
  }
  index_to_map_pos(&tile_x, &tile_y, tile_index(ptile));
  fc_snprintf(buf, sizeof(buf), _("Position %d, %d, continent %d."),
              tile_x, tile_y, tile_continent(ptile));
  sr_say(buf, 1);
}
