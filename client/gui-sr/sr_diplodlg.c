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
#include "diptreaty.h"
#include "game.h"
#include "nation.h"
#include "packets.h"
#include "player.h"

/* client */
#include "client_main.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_play.h"
#include "sr_window.h"
#include "sr_speech.h"

#include "sr_diplodlg.h"

#define SR_DIPLODLG_CLASS_NAME L"FreecivSRNationsDlg"
#define IDC_DIPLO_LIST         5001

static HWND g_dlg  = NULL;
static HWND g_list = NULL;
static WNDPROC g_list_default_proc = NULL;

/* ====================================================================== */
/* Helpers                                                                */
/* ====================================================================== */

static const char *diplstate_name(enum diplstate_type t)
{
  switch (t) {
    case DS_ARMISTICE:  return "armistice";
    case DS_WAR:        return "at war";
    case DS_CEASEFIRE:  return "ceasefire";
    case DS_PEACE:      return "peace";
    case DS_ALLIANCE:   return "alliance";
    case DS_NO_CONTACT: return "no contact";
    case DS_TEAM:       return "team";
    default:            return "unknown";
  }
}

static void append_row(struct player *them)
{
  char utf8[256];
  struct player *me = client_player();
  const char *rel = "unknown";
  int turns_left = 0;
  int wlen;
  wchar_t *w;
  int index;

  if (me != NULL) {
    struct player_diplstate *ds = player_diplstate_get(me, them);

    if (ds != NULL) {
      rel = diplstate_name(ds->type);
      turns_left = ds->turns_left;
    }
  }
  if (turns_left > 0 &&
      (strcmp(rel, "armistice") == 0 || strcmp(rel, "ceasefire") == 0)) {
    fc_snprintf(utf8, sizeof(utf8),
                "%s  \u00B7  %s (%d turns)  \u00B7  score %d%s",
                nation_adjective_for_player(them),
                rel, turns_left,
                them->score.game,
                is_ai(them) ? "  \u00B7  AI" : "");
  } else {
    fc_snprintf(utf8, sizeof(utf8),
                "%s  \u00B7  %s  \u00B7  score %d%s",
                nation_adjective_for_player(them),
                rel,
                them->score.game,
                is_ai(them) ? "  \u00B7  AI" : "");
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
    SendMessageW(g_list, LB_SETITEMDATA, (WPARAM)index,
                 (LPARAM)player_number(them));
  }
}

static void populate(void)
{
  struct player *me = client_player();

  if (g_list == NULL) {
    return;
  }
  SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_list, LB_RESETCONTENT, 0, 0);

  players_iterate_alive(pplayer) {
    if (pplayer == me) {
      continue;
    }
    if (is_barbarian(pplayer)) {
      continue;
    }
    append_row(pplayer);
  } players_iterate_alive_end;

  SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_list, NULL, TRUE);

  if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) > 0) {
    if (SendMessageW(g_list, LB_GETCURSEL, 0, 0) < 0) {
      SendMessageW(g_list, LB_SETCURSEL, 0, 0);
    }
  }
}

static void activate_current(void)
{
  int sel;
  int plrno;
  struct player *me = client_player();
  struct player *them;

  if (g_list == NULL || me == NULL) {
    return;
  }
  sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
  if (sel < 0) {
    return;
  }
  plrno = (int)SendMessageW(g_list, LB_GETITEMDATA, (WPARAM)sel, 0);
  them = player_by_number(plrno);
  if (them == NULL) {
    return;
  }

  if (could_meet_with_player(me, them)) {
    char msg[128];

    dsend_packet_diplomacy_init_meeting_req(&client.conn, plrno);
    fc_snprintf(msg, sizeof(msg), "Requesting meeting with the %s.",
                nation_plural_for_player(them));
    sr_log_append(msg);
  } else {
    struct player_diplstate *ds = player_diplstate_get(me, them);
    char msg[192];

    fc_snprintf(msg, sizeof(msg),
                "%s — %s. No meetings possible in this state.",
                nation_plural_for_player(them),
                ds != NULL ? diplstate_name(ds->type) : "unknown");
    sr_log_append(msg);
    sr_say(msg, 0);
  }
}

/* ====================================================================== */
/* Subclass + WndProc                                                     */
/* ====================================================================== */

static LRESULT CALLBACK list_subclass_proc(HWND hWnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    activate_current();
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    sr_diplodlg_close();
    return 0;
  }
  return CallWindowProcW(g_list_default_proc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK sr_diplodlg_wnd_proc(HWND hWnd, UINT msg,
                                             WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      g_list = CreateWindowExW(
          WS_EX_CLIENTEDGE,
          L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_DIPLO_LIST, hi, NULL);
      if (g_list != NULL) {
        g_list_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_list, GWLP_WNDPROC, (LONG_PTR)list_subclass_proc);
      }
      return 0;
    }

    case WM_SIZE: {
      RECT rc;
      int pad = 6;

      GetClientRect(hWnd, &rc);
      if (g_list != NULL) {
        MoveWindow(g_list, pad, pad,
                   rc.right - 2 * pad, rc.bottom - 2 * pad, TRUE);
      }
      return 0;
    }

    case WM_COMMAND:
      if (HIWORD(wParam) == LBN_DBLCLK && (HWND)lParam == g_list) {
        activate_current();
        return 0;
      }
      break;

    case WM_CLOSE:
      sr_diplodlg_close();
      return 0;

    case WM_DESTROY:
      sr_window_unregister_popup(g_dlg);
      g_dlg = NULL;
      g_list = NULL;
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

static int ensure_class_registered(HINSTANCE hi)
{
  static int registered = 0;
  WNDCLASSEXW wc;

  if (registered) {
    return 1;
  }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = sr_diplodlg_wnd_proc;
  wc.hInstance     = hi;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_DIPLODLG_CLASS_NAME;
  wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
  wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);

  if (RegisterClassExW(&wc) != 0 || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    registered = 1;
    return 1;
  }
  return 0;
}

/* ====================================================================== */
/* Public                                                                 */
/* ====================================================================== */

void sr_diplodlg_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (g_dlg != NULL) {
    populate();
    SetForegroundWindow(g_dlg);
    if (g_list != NULL) {
      SetFocus(g_list);
    }
    return;
  }
  if (!ensure_class_registered(hi)) {
    return;
  }
  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_DIPLODLG_CLASS_NAME,
      L"Nations",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME
      | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 580, 440,
      NULL, NULL, hi, NULL);
  if (g_dlg == NULL) {
    return;
  }
  sr_window_register_popup(g_dlg);
  populate();
  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_list != NULL) {
    SetFocus(g_list);
  }
}

void sr_diplodlg_close(void)
{
  HWND play;

  if (g_dlg != NULL) {
    DestroyWindow(g_dlg);
  }
  play = sr_play_hwnd();
  if (play != NULL) {
    SetFocus(play);
  }
}

void sr_diplodlg_refresh(void)
{
  if (g_dlg != NULL) {
    populate();
  }
}

int sr_diplodlg_is_open(void)
{
  return g_dlg != NULL;
}

#endif  /* _WIN32 */
