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
#include "climisc.h"     /* client_diplomacy_clause_string */
#include "client_main.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_play.h"
#include "sr_window.h"
#include "sr_speech.h"
#include "sr_treaty_pick.h"

#include "sr_treaty.h"

#define SR_TREATY_CLASS_NAME L"FreecivSRTreatyDlg"
#define IDC_TREATY_STATUS  6001
#define IDC_TREATY_CLAUSES 6002
#define IDC_TREATY_ADD     6003
#define IDC_TREATY_ACCEPT  6004
#define IDC_TREATY_CANCEL  6005

static HWND g_dlg      = NULL;
static HWND g_status   = NULL;
static HWND g_clauses  = NULL;
static HWND g_add      = NULL;
static HWND g_accept   = NULL;
static HWND g_cancel   = NULL;
static int  g_counterpart = -1;  /* player_number of "them" */

static WNDPROC g_clauses_default_proc = NULL;
static WNDPROC g_add_default_proc     = NULL;

/* ====================================================================== */
/* Add-clause catalogue (v1: pacts + share clauses only)                  */
/* ====================================================================== */

/* picker kind: which sub-modal (if any) fires for this row. */
enum add_picker {
  PICKER_NONE = 0,
  PICKER_TECH,
  PICKER_GOLD,
  PICKER_CITY
};

struct add_entry {
  const char *label;
  enum clause_type clause;       /* used for PICKER_NONE                */
  int from_us;                   /* 1 = we give, 0 = they give          */
  enum add_picker picker;        /* PICKER_NONE sends directly          */
};

static const struct add_entry g_add_entries[] = {
  { "Ceasefire  (mutual pact)",   CLAUSE_CEASEFIRE, 1, PICKER_NONE },
  { "Peace      (mutual pact)",   CLAUSE_PEACE,     1, PICKER_NONE },
  { "Alliance   (mutual pact)",   CLAUSE_ALLIANCE,  1, PICKER_NONE },
  { "You share: world map",       CLAUSE_MAP,       1, PICKER_NONE },
  { "They share: world map",      CLAUSE_MAP,       0, PICKER_NONE },
  { "You share: sea map",         CLAUSE_SEAMAP,    1, PICKER_NONE },
  { "They share: sea map",        CLAUSE_SEAMAP,    0, PICKER_NONE },
  { "You share: embassy",         CLAUSE_EMBASSY,   1, PICKER_NONE },
  { "They share: embassy",        CLAUSE_EMBASSY,   0, PICKER_NONE },
  { "You share: vision",          CLAUSE_VISION,    1, PICKER_NONE },
  { "They share: vision",         CLAUSE_VISION,    0, PICKER_NONE },
  { "You share: tiles",           CLAUSE_SHARED_TILES, 1, PICKER_NONE },
  { "They share: tiles",          CLAUSE_SHARED_TILES, 0, PICKER_NONE },
  /* Rows that open a sub-picker. */
  { "Offer a tech \u2026",        CLAUSE_ADVANCE,   1, PICKER_TECH },
  { "Request a tech \u2026",      CLAUSE_ADVANCE,   0, PICKER_TECH },
  { "Offer gold \u2026",          CLAUSE_GOLD,      1, PICKER_GOLD },
  { "Request gold \u2026",        CLAUSE_GOLD,      0, PICKER_GOLD },
  { "Offer a city \u2026",        CLAUSE_CITY,      1, PICKER_CITY },
  { "Request a city \u2026",      CLAUSE_CITY,      0, PICKER_CITY },
};
#define N_ADD_ENTRIES (int)(sizeof(g_add_entries) / sizeof(g_add_entries[0]))

/* ====================================================================== */
/* Helpers                                                                */
/* ====================================================================== */

static struct player *they_player(void)
{
  return g_counterpart >= 0 ? player_by_number(g_counterpart) : NULL;
}

static struct Treaty *current_treaty(void)
{
  struct player *we = client_player();
  struct player *they = they_player();

  if (we == NULL || they == NULL) {
    return NULL;
  }
  return find_treaty(we, they);
}

static void set_static_utf8(HWND h, const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (h == NULL || utf8 == NULL) {
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
  SetWindowTextW(h, w);
  free(w);
}

static void list_add_utf8(HWND list, const char *utf8, LPARAM data)
{
  int wlen;
  wchar_t *w;
  int index;

  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) {
    return;
  }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) {
    return;
  }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  index = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
  if (index >= 0) {
    SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, data);
  }
}

static void refresh_status(struct Treaty *t)
{
  struct player *they = they_player();
  char buf[256];

  if (g_status == NULL || t == NULL || they == NULL) {
    return;
  }
  fc_snprintf(buf, sizeof(buf),
              "Negotiating with the %s.\r\nYou: %s.  They: %s.",
              nation_plural_for_player(they),
              t->accept0 ? "accepted" : "not yet accepted",
              t->accept1 ? "accepted" : "not yet accepted");
  set_static_utf8(g_status, buf);

  if (g_accept != NULL) {
    SetWindowTextW(g_accept,
                   t->accept0 ? L"&Retract acceptance" : L"&Accept");
  }
}

static void populate_clauses(struct Treaty *t)
{
  int idx = 0;

  if (g_clauses == NULL || t == NULL) {
    return;
  }
  SendMessageW(g_clauses, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_clauses, LB_RESETCONTENT, 0, 0);

  if (t->clauses != NULL) {
    clause_list_iterate(t->clauses, pclause) {
      char buf[256];

      client_diplomacy_clause_string(buf, sizeof(buf), pclause);
      list_add_utf8(g_clauses, buf, (LPARAM)idx);
      idx++;
    } clause_list_iterate_end;
  }

  if (idx == 0) {
    list_add_utf8(g_clauses, "(no clauses yet)", (LPARAM)(-1));
  }

  SendMessageW(g_clauses, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_clauses, NULL, TRUE);
}

static void populate_add(void)
{
  int i;

  if (g_add == NULL) {
    return;
  }
  if (SendMessageW(g_add, LB_GETCOUNT, 0, 0) == N_ADD_ENTRIES) {
    return;  /* already filled; static list */
  }
  SendMessageW(g_add, LB_RESETCONTENT, 0, 0);
  for (i = 0; i < N_ADD_ENTRIES; i++) {
    list_add_utf8(g_add, g_add_entries[i].label, (LPARAM)i);
  }
  SendMessageW(g_add, LB_SETCURSEL, 0, 0);
}

/* ====================================================================== */
/* Actions                                                                */
/* ====================================================================== */

static void action_add_clause(void)
{
  int sel;
  const struct add_entry *e;
  struct player *we = client_player();
  struct player *they = they_player();
  int giver_plrno;

  if (g_add == NULL || we == NULL || they == NULL) {
    return;
  }
  sel = (int)SendMessageW(g_add, LB_GETCURSEL, 0, 0);
  if (sel < 0 || sel >= N_ADD_ENTRIES) {
    return;
  }
  e = &g_add_entries[sel];
  giver_plrno = e->from_us ? player_number(we) : player_number(they);

  switch (e->picker) {
    case PICKER_NONE:
      dsend_packet_diplomacy_create_clause_req(&client.conn,
                                               g_counterpart,
                                               giver_plrno,
                                               e->clause,
                                               0);
      return;
    case PICKER_TECH:
      sr_treaty_pick_tech(g_counterpart, giver_plrno);
      return;
    case PICKER_GOLD:
      sr_treaty_pick_gold(g_counterpart, giver_plrno);
      return;
    case PICKER_CITY:
      sr_treaty_pick_city(g_counterpart, giver_plrno);
      return;
  }
}

static void action_remove_clause(void)
{
  int sel;
  int target_idx;
  int walk = 0;
  struct Treaty *t = current_treaty();

  if (g_clauses == NULL || t == NULL) {
    return;
  }
  sel = (int)SendMessageW(g_clauses, LB_GETCURSEL, 0, 0);
  if (sel < 0) {
    return;
  }
  target_idx = (int)SendMessageW(g_clauses, LB_GETITEMDATA, (WPARAM)sel, 0);
  if (target_idx < 0) {
    return;  /* the "(no clauses yet)" row */
  }
  if (t->clauses == NULL) {
    return;
  }
  clause_list_iterate(t->clauses, pclause) {
    if (walk == target_idx) {
      dsend_packet_diplomacy_remove_clause_req(&client.conn,
                                               g_counterpart,
                                               player_number(pclause->from),
                                               pclause->type,
                                               pclause->value);
      return;
    }
    walk++;
  } clause_list_iterate_end;
}

static void action_accept(void)
{
  if (g_counterpart < 0) {
    return;
  }
  dsend_packet_diplomacy_accept_treaty_req(&client.conn, g_counterpart);
}

static void action_cancel(void)
{
  if (g_counterpart < 0) {
    sr_treaty_close_all();
    return;
  }
  dsend_packet_diplomacy_cancel_meeting_req(&client.conn, g_counterpart);
  /* Wait for the server's gui_recv_cancel_meeting to actually close. */
}

/* ====================================================================== */
/* Subclass procs                                                         */
/* ====================================================================== */

static LRESULT CALLBACK clauses_proc(HWND hWnd, UINT msg,
                                     WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_DELETE || wParam == VK_BACK) {
      action_remove_clause();
      return 0;
    }
    if (wParam == VK_ESCAPE) {
      action_cancel();
      return 0;
    }
  }
  return CallWindowProcW(g_clauses_default_proc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK add_proc(HWND hWnd, UINT msg,
                                 WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (msg == WM_KEYDOWN) {
    if (wParam == VK_RETURN) {
      action_add_clause();
      return 0;
    }
    if (wParam == VK_ESCAPE) {
      action_cancel();
      return 0;
    }
  }
  return CallWindowProcW(g_add_default_proc, hWnd, msg, wParam, lParam);
}

/* ====================================================================== */
/* Window proc                                                            */
/* ====================================================================== */

static LRESULT CALLBACK sr_treaty_wnd_proc(HWND hWnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      g_status = CreateWindowExW(
          0, L"STATIC", L"(loading treaty...)",
          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_TREATY_STATUS, hi, NULL);

      g_clauses = CreateWindowExW(
          WS_EX_CLIENTEDGE,
          L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_TREATY_CLAUSES, hi, NULL);
      if (g_clauses != NULL) {
        g_clauses_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_clauses, GWLP_WNDPROC, (LONG_PTR)clauses_proc);
      }

      g_add = CreateWindowExW(
          WS_EX_CLIENTEDGE,
          L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_TREATY_ADD, hi, NULL);
      if (g_add != NULL) {
        g_add_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_add, GWLP_WNDPROC, (LONG_PTR)add_proc);
      }

      g_accept = CreateWindowExW(
          0, L"BUTTON", L"&Accept",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_TREATY_ACCEPT, hi, NULL);

      g_cancel = CreateWindowExW(
          0, L"BUTTON", L"&Cancel meeting",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_TREATY_CANCEL, hi, NULL);
      return 0;
    }

    case WM_SIZE: {
      RECT rc;
      int pad = 6;
      int status_h = 56;
      int button_h = 28;
      int third;

      GetClientRect(hWnd, &rc);
      if (g_status != NULL) {
        MoveWindow(g_status, pad, pad,
                   rc.right - 2 * pad, status_h, TRUE);
      }
      third = (rc.bottom - pad - status_h - button_h - 4 * pad) / 2;
      if (third < 50) {
        third = 50;
      }
      if (g_clauses != NULL) {
        MoveWindow(g_clauses, pad, pad + status_h + pad,
                   rc.right - 2 * pad, third, TRUE);
      }
      if (g_add != NULL) {
        MoveWindow(g_add, pad, pad + status_h + pad + third + pad,
                   rc.right - 2 * pad, third, TRUE);
      }
      {
        int y = pad + status_h + pad + third + pad + third + pad;

        if (g_accept != NULL) {
          MoveWindow(g_accept, pad, y, 160, button_h, TRUE);
        }
        if (g_cancel != NULL) {
          MoveWindow(g_cancel, pad + 160 + pad, y, 180, button_h, TRUE);
        }
      }
      return 0;
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam);

      if (id == IDC_TREATY_ACCEPT && HIWORD(wParam) == BN_CLICKED) {
        action_accept();
        return 0;
      }
      if (id == IDC_TREATY_CANCEL && HIWORD(wParam) == BN_CLICKED) {
        action_cancel();
        return 0;
      }
      if (HIWORD(wParam) == LBN_DBLCLK) {
        if ((HWND)lParam == g_add) {
          action_add_clause();
          return 0;
        }
        if ((HWND)lParam == g_clauses) {
          action_remove_clause();
          return 0;
        }
      }
      break;
    }

    case WM_CLOSE:
      action_cancel();
      return 0;

    case WM_DESTROY:
      sr_window_unregister_popup(g_dlg);
      g_dlg = NULL;
      g_status = g_clauses = g_add = g_accept = g_cancel = NULL;
      g_counterpart = -1;
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
  wc.lpfnWndProc   = sr_treaty_wnd_proc;
  wc.hInstance     = hi;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_TREATY_CLASS_NAME;
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

void sr_treaty_open(struct Treaty *ptreaty, struct player *they)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  wchar_t title[128];
  char title_utf8[128];

  if (they == NULL || ptreaty == NULL) {
    return;
  }

  /* Only one at a time. If a new meeting displaces an old window,
     close it first (the old meeting's server-side lifecycle will
     cancel independently). */
  if (g_dlg != NULL && g_counterpart != player_number(they)) {
    DestroyWindow(g_dlg);
  }

  g_counterpart = player_number(they);

  if (g_dlg != NULL) {
    sr_treaty_refresh(ptreaty);
    SetForegroundWindow(g_dlg);
    if (g_accept != NULL) {
      SetFocus(g_accept);
    }
    return;
  }

  if (!ensure_class_registered(hi)) {
    return;
  }

  fc_snprintf(title_utf8, sizeof(title_utf8),
              "Treaty with %s", nation_plural_for_player(they));
  MultiByteToWideChar(CP_UTF8, 0, title_utf8, -1, title,
                      sizeof(title) / sizeof(title[0]));

  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_TREATY_CLASS_NAME,
      title,
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME
      | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 620, 600,
      NULL, NULL, hi, NULL);
  if (g_dlg == NULL) {
    g_counterpart = -1;
    return;
  }

  sr_window_register_popup(g_dlg);

  populate_add();
  refresh_status(ptreaty);
  populate_clauses(ptreaty);

  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_accept != NULL) {
    SetFocus(g_accept);
  }

  {
    char msg[160];

    fc_snprintf(msg, sizeof(msg), "Negotiating with the %s.",
                nation_plural_for_player(they));
    sr_log_append(msg);
  }
}

void sr_treaty_refresh(struct Treaty *ptreaty)
{
  if (g_dlg == NULL || ptreaty == NULL) {
    return;
  }
  refresh_status(ptreaty);
  populate_clauses(ptreaty);
}

void sr_treaty_close_for(struct player *they)
{
  HWND play;

  if (g_dlg == NULL || they == NULL
      || g_counterpart != player_number(they)) {
    return;
  }

  {
    char msg[128];

    fc_snprintf(msg, sizeof(msg), "Meeting with the %s cancelled.",
                nation_plural_for_player(they));
    sr_log_append(msg);
  }
  DestroyWindow(g_dlg);

  play = sr_play_hwnd();
  if (play != NULL) {
    SetFocus(play);
  }
}

void sr_treaty_close_all(void)
{
  if (g_dlg != NULL) {
    DestroyWindow(g_dlg);
  }
}

int sr_treaty_is_open(void)
{
  return g_dlg != NULL;
}

#endif  /* _WIN32 */
