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
#include <string.h>
#include <stdlib.h>

#include <windows.h>

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "calendar.h"
#include "game.h"
#include "government.h"
#include "nation.h"
#include "player.h"
#include "research.h"
#include "tech.h"
#include "unit.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "text.h"

/* gui-sr */
#include "sr_sidebar.h"

static HWND g_list = NULL;

/* ====================================================================== */
/* Helpers                                                                */
/* ====================================================================== */

static void push_line_utf8(const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (g_list == NULL || utf8 == NULL) {
    return;
  }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) {
    return;
  }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) {
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
}

static void push_line(const char *fmt, ...)
{
  char buf[256];
  va_list ap;

  va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  push_line_utf8(buf);
}

/* ====================================================================== */
/* Public                                                                 */
/* ====================================================================== */

int sr_sidebar_create(HWND parent)
{
  g_list = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
      | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
      0, 0, 0, 0,
      parent, NULL, GetModuleHandleW(NULL), NULL);
  return g_list != NULL;
}

HWND sr_sidebar_hwnd(void)
{
  return g_list;
}

static void rebuild_all(void)
{
  struct player *pl;
  struct research *r;
  int waiting = 0;

  if (g_list == NULL) {
    return;
  }
  SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_list, LB_RESETCONTENT, 0, 0);

  push_line("Turn: %d  (%s)", game.info.turn, calendar_text());

  pl = client_player();
  if (pl != NULL) {
    push_line("Gold: %d  (%+d per turn)",
              pl->economic.gold, player_get_expected_income(pl));
    push_line("Rates: Tax %d%% / Luxury %d%% / Science %d%%",
              pl->economic.tax, pl->economic.luxury, pl->economic.science);
    if (government_of_player(pl) != NULL) {
      push_line("Government: %s",
                government_name_translation(government_of_player(pl)));
    }
    push_line("Nation: %s",
              nation_adjective_for_player(pl));
    push_line("Score: %d", pl->score.game);

    r = research_get(pl);
    if (r != NULL) {
      double pct = 0.0;
      const char *target = get_science_target_text(&pct);

      push_line("Research: %s",
                (target != NULL && *target != '\0') ? target : "—");
    }

    /* Selected unit summary. */
    if (get_num_units_in_focus() > 0) {
      struct unit *punit = head_of_units_in_focus();

      if (punit != NULL) {
        const struct unit_type *ut = unit_type_get(punit);

        push_line("Selected: %s — %d/%d moves — HP %d/%d",
                  utype_name_translation(ut),
                  punit->moves_left,
                  ut != NULL ? ut->move_rate : 0,
                  punit->hp,
                  ut != NULL ? ut->hp : 0);
      }
    } else {
      push_line_utf8("Selected: —");
    }

    /* Waiting-units hint. */
    unit_list_iterate(pl->units, punit) {
      if (punit->moves_left > 0 && punit->activity == ACTIVITY_IDLE) {
        waiting++;
      }
    } unit_list_iterate_end;
    if (waiting > 0) {
      push_line("Waiting: %d unit%s",
                waiting, waiting == 1 ? "" : "s");
    }
  } else {
    push_line_utf8("(not in a game)");
  }

  SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_list, NULL, TRUE);
}

void sr_sidebar_update_game(void) { rebuild_all(); }
void sr_sidebar_update_unit(void) { rebuild_all(); }

#endif  /* _WIN32 */
