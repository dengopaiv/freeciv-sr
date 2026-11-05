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

#ifdef _WIN32

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>

/* utility */
#include "support.h"

/* common */
#include "city.h"
#include "game.h"
#include "player.h"
#include "research.h"
#include "unit.h"

/* client */
#include "client_main.h"
#include "text.h"

#include "sr_status.h"

static HWND g_status = NULL;

void sr_status_attach(HWND h)
{
  g_status = h;
}

void sr_status_update(void)
{
  char utf8[256];
  wchar_t wbuf[512];
  struct player *pl = client_player();
  int n_units = 0, n_cities = 0;
  int gold = 0;
  const char *research = "—";

  if (g_status == NULL) {
    return;
  }

  if (pl != NULL) {
    n_units  = unit_list_size(pl->units);
    n_cities = city_list_size(pl->cities);
    gold     = pl->economic.gold;
    {
      double pct = 0.0;
      const char *t = get_science_target_text(&pct);

      if (t != NULL && *t != '\0') {
        research = t;
      }
    }
  }

  fc_snprintf(utf8, sizeof(utf8),
              "Turn %d  \u00B7  %d unit%s  \u00B7  %d cit%s  \u00B7  Gold %d  \u00B7  %s",
              game.info.turn,
              n_units,  n_units == 1 ? "" : "s",
              n_cities, n_cities == 1 ? "y" : "ies",
              gold, research);

  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wbuf,
                      sizeof(wbuf) / sizeof(wbuf[0]));
  SendMessageW(g_status, SB_SETTEXTW, (WPARAM)0, (LPARAM)wbuf);
}

#endif  /* _WIN32 */
