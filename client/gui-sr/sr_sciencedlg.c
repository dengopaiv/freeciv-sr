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
#include "game.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "tech.h"

/* client */
#include "client_main.h"
#include "text.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_play.h"
#include "sr_window.h"
#include "sr_speech.h"

#include "sr_sciencedlg.h"

#define SR_SCIENCE_CLASS_NAME L"FreecivSRScienceDlg"
#define IDC_SCI_STATUS   4001
#define IDC_SCI_CURRENT  4002
#define IDC_SCI_GOAL     4003

static HWND g_dlg     = NULL;
static HWND g_status  = NULL;
static HWND g_current = NULL;
static HWND g_goal    = NULL;
static WNDPROC g_current_default_proc = NULL;
static WNDPROC g_goal_default_proc    = NULL;

/* ====================================================================== */
/* Helpers                                                                */
/* ====================================================================== */

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

static void list_append_utf8(HWND list, const char *utf8, LPARAM tech_id)
{
  int wlen;
  wchar_t *w;
  int index;

  if (list == NULL || utf8 == NULL) {
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
  index = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
  if (index >= 0) {
    SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, tech_id);
  }
}

static void refresh_status(struct research *r)
{
  char buf[512];

  if (g_status == NULL || r == NULL) {
    return;
  }
  {
    double pct = 0.0;
    const char *tgt = get_science_target_text(&pct);
    const char *goal_text = (r->tech_goal != A_UNSET && r->tech_goal != A_UNKNOWN)
                          ? get_science_goal_text(r->tech_goal) : "(no goal)";

    fc_snprintf(buf, sizeof(buf),
                "%s\r\n%s\r\nGoal: %s",
                science_dialog_text(),
                (tgt != NULL && *tgt != '\0') ? tgt : "(no active research)",
                goal_text);
  }
  set_static_utf8(g_status, buf);
}

/* Add one tech row, formatted as "Pottery  —  2 turns (120 bulbs)" or
   similar. For the goal list the same helper is fine. */
static void add_tech_row(HWND list, struct research *r, Tech_type_id tech)
{
  char line[256];
  int total, remaining;

  if (tech == A_UNSET) {
    list_append_utf8(list, "(unset)", (LPARAM)A_UNSET);
    return;
  }
  if (tech == A_FUTURE) {
    list_append_utf8(list, "Future Tech", (LPARAM)A_FUTURE);
    return;
  }
  total = research_total_bulbs_required(r, tech, FALSE);
  remaining = total - r->bulbs_researched;
  if (remaining < 0) {
    remaining = 0;
  }
  fc_snprintf(line, sizeof(line), "%s  —  %d of %d bulbs",
              advance_name_translation(advance_by_number(tech)),
              r->bulbs_researched, total);
  list_append_utf8(list, line, (LPARAM)tech);
}

static void populate_current(struct research *r)
{
  Tech_type_id ac = advance_count();

  if (g_current == NULL || r == NULL) {
    return;
  }
  SendMessageW(g_current, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_current, LB_RESETCONTENT, 0, 0);

  advance_index_iterate_max(A_FIRST, i, ac) {
    if (TECH_PREREQS_KNOWN == r->inventions[i].state) {
      add_tech_row(g_current, r, i);
    }
  } advance_index_iterate_max_end;

  /* If nothing's reachable, at least show Future Tech when we're past
     end-of-tree, else a placeholder. */
  if (SendMessageW(g_current, LB_GETCOUNT, 0, 0) == 0) {
    add_tech_row(g_current, r, A_FUTURE);
  }

  /* Preselect the current research, if any. */
  if (r->researching != A_UNSET && r->researching != A_UNKNOWN) {
    int count = (int)SendMessageW(g_current, LB_GETCOUNT, 0, 0);
    int i;

    for (i = 0; i < count; i++) {
      LPARAM p = SendMessageW(g_current, LB_GETITEMDATA, (WPARAM)i, 0);

      if ((Tech_type_id)p == r->researching) {
        SendMessageW(g_current, LB_SETCURSEL, (WPARAM)i, 0);
        break;
      }
    }
  } else if (SendMessageW(g_current, LB_GETCOUNT, 0, 0) > 0) {
    SendMessageW(g_current, LB_SETCURSEL, 0, 0);
  }

  SendMessageW(g_current, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_current, NULL, TRUE);
}

static void populate_goal(struct research *r)
{
  Tech_type_id ac = advance_count();

  if (g_goal == NULL || r == NULL) {
    return;
  }
  SendMessageW(g_goal, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_goal, LB_RESETCONTENT, 0, 0);

  add_tech_row(g_goal, r, A_UNSET);

  advance_index_iterate_max(A_FIRST, i, ac) {
    if (research_invention_reachable(r, i)
        && TECH_KNOWN != r->inventions[i].state
        && (i == r->tech_goal
            || 10 >= r->inventions[i].num_required_techs)) {
      add_tech_row(g_goal, r, i);
    }
  } advance_index_iterate_max_end;

  if (r->tech_goal != A_UNSET && r->tech_goal != A_UNKNOWN) {
    int count = (int)SendMessageW(g_goal, LB_GETCOUNT, 0, 0);
    int i;

    for (i = 0; i < count; i++) {
      LPARAM p = SendMessageW(g_goal, LB_GETITEMDATA, (WPARAM)i, 0);

      if ((Tech_type_id)p == r->tech_goal) {
        SendMessageW(g_goal, LB_SETCURSEL, (WPARAM)i, 0);
        break;
      }
    }
  } else {
    SendMessageW(g_goal, LB_SETCURSEL, 0, 0);
  }

  SendMessageW(g_goal, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_goal, NULL, TRUE);
}

static void refresh_all(void)
{
  struct research *r;

  if (g_dlg == NULL) {
    return;
  }
  r = research_get(client_player());
  if (r == NULL) {
    set_static_utf8(g_status, "(not in a game)");
    return;
  }
  refresh_status(r);
  populate_current(r);
  populate_goal(r);
}

/* Set the current research target. */
static void activate_current(void)
{
  int sel;
  Tech_type_id tech;
  char msg[128];

  if (g_current == NULL) {
    return;
  }
  sel = (int)SendMessageW(g_current, LB_GETCURSEL, 0, 0);
  if (sel < 0) {
    return;
  }
  tech = (Tech_type_id)SendMessageW(g_current, LB_GETITEMDATA, (WPARAM)sel, 0);
  dsend_packet_player_research(&client.conn, tech);
  if (tech == A_UNSET) {
    sr_log_append("Research target cleared.");
  } else if (tech == A_FUTURE) {
    sr_log_append("Researching Future Tech.");
  } else {
    fc_snprintf(msg, sizeof(msg), "Researching: %s.",
                advance_name_translation(advance_by_number(tech)));
    sr_log_append(msg);
  }
}

/* Set the long-term research goal. */
static void activate_goal(void)
{
  int sel;
  Tech_type_id tech;
  char msg[128];

  if (g_goal == NULL) {
    return;
  }
  sel = (int)SendMessageW(g_goal, LB_GETCURSEL, 0, 0);
  if (sel < 0) {
    return;
  }
  tech = (Tech_type_id)SendMessageW(g_goal, LB_GETITEMDATA, (WPARAM)sel, 0);
  dsend_packet_player_tech_goal(&client.conn, tech);
  if (tech == A_UNSET) {
    sr_log_append("Research goal cleared.");
  } else {
    fc_snprintf(msg, sizeof(msg), "Research goal: %s.",
                advance_name_translation(advance_by_number(tech)));
    sr_log_append(msg);
  }
}

/* ====================================================================== */
/* Subclass procs                                                         */
/* ====================================================================== */

static LRESULT CALLBACK current_subclass_proc(HWND hWnd, UINT msg,
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
    sr_sciencedlg_close();
    return 0;
  }
  return CallWindowProcW(g_current_default_proc, hWnd, msg, wParam, lParam);
}

static LRESULT CALLBACK goal_subclass_proc(HWND hWnd, UINT msg,
                                           WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    activate_goal();
    return 0;
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    sr_sciencedlg_close();
    return 0;
  }
  return CallWindowProcW(g_goal_default_proc, hWnd, msg, wParam, lParam);
}

/* ====================================================================== */
/* Dialog WndProc                                                         */
/* ====================================================================== */

static LRESULT CALLBACK sr_sciencedlg_wnd_proc(HWND hWnd, UINT msg,
                                               WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      g_status = CreateWindowExW(
          0,
          L"STATIC", L"(loading research state...)",
          WS_CHILD | WS_VISIBLE | SS_LEFT | SS_NOPREFIX,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_SCI_STATUS, hi, NULL);

      g_current = CreateWindowExW(
          WS_EX_CLIENTEDGE,
          L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_SCI_CURRENT, hi, NULL);

      g_goal = CreateWindowExW(
          WS_EX_CLIENTEDGE,
          L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_SCI_GOAL, hi, NULL);

      if (g_current != NULL) {
        g_current_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_current, GWLP_WNDPROC, (LONG_PTR)current_subclass_proc);
      }
      if (g_goal != NULL) {
        g_goal_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_goal, GWLP_WNDPROC, (LONG_PTR)goal_subclass_proc);
      }
      return 0;
    }

    case WM_SIZE: {
      RECT rc;
      int pad = 6;
      int status_h = 72;
      int half;

      GetClientRect(hWnd, &rc);
      if (g_status != NULL) {
        MoveWindow(g_status, pad, pad,
                   rc.right - 2 * pad, status_h, TRUE);
      }
      half = (rc.bottom - pad - status_h - 3 * pad) / 2;
      if (half < 60) {
        half = 60;
      }
      if (g_current != NULL) {
        MoveWindow(g_current, pad, pad + status_h + pad,
                   rc.right - 2 * pad, half, TRUE);
      }
      if (g_goal != NULL) {
        MoveWindow(g_goal, pad, pad + status_h + pad + half + pad,
                   rc.right - 2 * pad, half, TRUE);
      }
      return 0;
    }

    case WM_COMMAND:
      if (HIWORD(wParam) == LBN_DBLCLK) {
        if ((HWND)lParam == g_current) {
          activate_current();
          return 0;
        }
        if ((HWND)lParam == g_goal) {
          activate_goal();
          return 0;
        }
      }
      break;

    case WM_CLOSE:
      sr_sciencedlg_close();
      return 0;

    case WM_DESTROY:
      sr_window_unregister_popup(g_dlg);
      g_dlg = NULL;
      g_status = g_current = g_goal = NULL;
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
  wc.lpfnWndProc   = sr_sciencedlg_wnd_proc;
  wc.hInstance     = hi;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_SCIENCE_CLASS_NAME;
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

void sr_sciencedlg_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (g_dlg != NULL) {
    refresh_all();
    SetForegroundWindow(g_dlg);
    if (g_current != NULL) {
      SetFocus(g_current);
    }
    return;
  }
  if (!ensure_class_registered(hi)) {
    return;
  }
  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_SCIENCE_CLASS_NAME,
      L"Research",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME
      | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 560, 560,
      NULL, NULL, hi, NULL);
  if (g_dlg == NULL) {
    return;
  }
  sr_window_register_popup(g_dlg);
  refresh_all();
  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_current != NULL) {
    SetFocus(g_current);
  }
}

void sr_sciencedlg_close(void)
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

void sr_sciencedlg_refresh(void)
{
  if (g_dlg != NULL) {
    refresh_all();
  }
}

int sr_sciencedlg_is_open(void)
{
  return g_dlg != NULL;
}

#endif  /* _WIN32 */
