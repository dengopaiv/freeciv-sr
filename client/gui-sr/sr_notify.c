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

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "map.h"
#include "player.h"
#include "tile.h"
#include "unit.h"
#include "unitlist.h"

/* client */
#include "client_main.h"
#include "control.h"
#include "mapview_common.h"

/* gui-sr */
#include "sr_cmd.h"
#include "sr_play.h"
#include "sr_speech.h"

#include "sr_notify.h"

static HWND g_list = NULL;
static WNDPROC g_list_default_proc = NULL;

static LRESULT CALLBACK notify_subclass_proc(HWND hWnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    sr_notify_activate_current();
    return 0;
  }
  return CallWindowProcW(g_list_default_proc, hWnd, msg, wParam, lParam);
}

/* We encode kind + ref into the LISTBOX's per-item lParam: upper 8 bits
   = kind, lower 24 = ref. Fine for unit/player ids in any reasonable
   game. */
static LPARAM pack_item(enum sr_notify_kind kind, int ref)
{
  return ((LPARAM)(kind & 0xFF) << 24) | (LPARAM)(ref & 0x00FFFFFF);
}

static enum sr_notify_kind unpack_kind(LPARAM p)
{
  return (enum sr_notify_kind)((p >> 24) & 0xFF);
}

static int unpack_ref(LPARAM p)
{
  return (int)(p & 0x00FFFFFF);
}

static void add_item(enum sr_notify_kind kind, int ref, const char *utf8)
{
  int wlen;
  wchar_t *w;
  int index;

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
  index = (int)SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
  if (index >= 0) {
    SendMessageW(g_list, LB_SETITEMDATA, (WPARAM)index, pack_item(kind, ref));
  }
}

/* ====================================================================== */
/* Public                                                                 */
/* ====================================================================== */

int sr_notify_create(HWND parent)
{
  g_list = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      L"LISTBOX", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
      | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
      0, 0, 0, 0,
      parent, NULL, GetModuleHandleW(NULL), NULL);
  if (g_list != NULL) {
    g_list_default_proc = (WNDPROC)SetWindowLongPtrW(
        g_list, GWLP_WNDPROC, (LONG_PTR)notify_subclass_proc);
  }
  return g_list != NULL;
}

HWND sr_notify_hwnd(void)
{
  return g_list;
}

void sr_notify_push(enum sr_notify_kind kind, int ref, const char *utf8)
{
  add_item(kind, ref, utf8);
}

void sr_notify_turn_digest(const char *utf8)
{
  add_item(SR_NOTIFY_TURN_DIGEST, 0, utf8);
}

void sr_notify_recompute_waiting(void)
{
  struct player *pl;
  int i, count;

  if (g_list == NULL) {
    return;
  }

  /* Remove existing waiting-unit entries (kind == WAITING_UNIT). */
  count = (int)SendMessageW(g_list, LB_GETCOUNT, 0, 0);
  for (i = count - 1; i >= 0; i--) {
    LPARAM p = SendMessageW(g_list, LB_GETITEMDATA, (WPARAM)i, 0);

    if (unpack_kind(p) == SR_NOTIFY_WAITING_UNIT) {
      SendMessageW(g_list, LB_DELETESTRING, (WPARAM)i, 0);
    }
  }

  pl = client_player();
  if (pl == NULL) {
    return;
  }
  unit_list_iterate(pl->units, punit) {
    if (punit->moves_left > 0 && punit->activity == ACTIVITY_IDLE) {
      char buf[128];

      fc_snprintf(buf, sizeof(buf),
                  "Waiting: %s at %d,%d",
                  utype_name_translation(unit_type_get(punit)),
                  index_to_map_pos_x(tile_index(unit_tile(punit))),
                  index_to_map_pos_y(tile_index(unit_tile(punit))));
      add_item(SR_NOTIFY_WAITING_UNIT, punit->id, buf);
    }
  } unit_list_iterate_end;
}

void sr_notify_activate_current(void)
{
  int sel;
  LPARAM p;
  enum sr_notify_kind kind;
  int ref;

  if (g_list == NULL) {
    return;
  }
  sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
  if (sel < 0) {
    return;
  }
  p = SendMessageW(g_list, LB_GETITEMDATA, (WPARAM)sel, 0);
  kind = unpack_kind(p);
  ref  = unpack_ref(p);

  switch (kind) {
    case SR_NOTIFY_WAITING_UNIT: {
      struct player *pl = client_player();
      struct unit *punit = NULL;

      if (pl != NULL) {
        punit = player_unit_by_number(pl, ref);
      }
      if (punit != NULL) {
        unit_focus_set(punit);
        center_tile_mapcanvas(unit_tile(punit));
      }
      if (sr_play_hwnd() != NULL) {
        SetFocus(sr_play_hwnd());
      }
      return;
    }

    case SR_NOTIFY_TURN_DIGEST:
      /* Informational; no action. Announce it again on request. */
      {
        wchar_t buf[512];

        SendMessageW(g_list, LB_GETTEXT, (WPARAM)sel, (LPARAM)buf);
        /* We could re-speak via sr_say; fine to just leave it. */
      }
      return;

    case SR_NOTIFY_RESEARCH_DONE:
      sr_cmd_dispatch_line("research");   /* dispatch falls through to
                                            sr_sciencedlg once it exists;
                                            until then, sr_cmd logs an
                                            "unknown command" blurb. */
      return;

    case SR_NOTIFY_NEW_CONTACT:
      sr_cmd_dispatch_line("diplomacy");
      return;

    default:
      return;
  }
}

#endif  /* _WIN32 */
