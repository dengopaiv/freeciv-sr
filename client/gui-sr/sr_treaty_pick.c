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
#include "support.h"

/* common */
#include "city.h"
#include "diptreaty.h"
#include "game.h"
#include "packets.h"
#include "player.h"
#include "research.h"
#include "tech.h"

/* client */
#include "client_main.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_play.h"
#include "sr_window.h"

#include "sr_treaty_pick.h"

/* ====================================================================== */
/* Tech picker                                                            */
/* ====================================================================== */

#define SR_TECHPICK_CLASS_NAME L"FreecivSRTreatyTechPick"
#define IDC_TECHPICK_LIST 8001

static HWND g_tech_dlg  = NULL;
static HWND g_tech_list = NULL;
static WNDPROC g_tech_default_proc = NULL;
static int  g_tech_counterpart = -1;
static int  g_tech_giver       = -1;

static void list_add_utf8(HWND list, const char *utf8, LPARAM lp)
{
  int wlen;
  wchar_t *w;
  int index;

  if (list == NULL) { return; }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) { return; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return; }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  index = (int)SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
  if (index >= 0) {
    SendMessageW(list, LB_SETITEMDATA, (WPARAM)index, lp);
  }
}

static void tech_populate(void)
{
  struct research *giver_r = research_get(player_by_number(g_tech_giver));
  struct research *rec_r;
  int giver_plr;
  Tech_type_id ac = advance_count();

  if (g_tech_list == NULL || giver_r == NULL) { return; }
  giver_plr = g_tech_giver;
  /* The recipient is whichever of us/them is not the giver. */
  rec_r = research_get(
    player_by_number(giver_plr == g_tech_counterpart
                     ? player_number(client_player())
                     : g_tech_counterpart));

  SendMessageW(g_tech_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_tech_list, LB_RESETCONTENT, 0, 0);

  advance_index_iterate_max(A_FIRST, i, ac) {
    if (giver_r->inventions[i].state == TECH_KNOWN
        && (rec_r == NULL
            || rec_r->inventions[i].state != TECH_KNOWN)) {
      list_add_utf8(g_tech_list,
                    advance_name_translation(advance_by_number(i)),
                    (LPARAM)i);
    }
  } advance_index_iterate_max_end;

  if (SendMessageW(g_tech_list, LB_GETCOUNT, 0, 0) == 0) {
    list_add_utf8(g_tech_list, "(no techs available to give)", (LPARAM)(-1));
  }
  SendMessageW(g_tech_list, WM_SETREDRAW, TRUE, 0);
  SendMessageW(g_tech_list, LB_SETCURSEL, 0, 0);
}

static void tech_confirm(void)
{
  int sel;
  Tech_type_id tech;

  if (g_tech_list == NULL) { return; }
  sel = (int)SendMessageW(g_tech_list, LB_GETCURSEL, 0, 0);
  if (sel < 0) { return; }
  tech = (Tech_type_id)SendMessageW(g_tech_list, LB_GETITEMDATA,
                                    (WPARAM)sel, 0);
  if (tech < 0) { return; }
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           g_tech_counterpart,
                                           g_tech_giver,
                                           CLAUSE_ADVANCE,
                                           (int)tech);
}

static void tech_close(void)
{
  HWND play = sr_play_hwnd();

  if (g_tech_dlg != NULL) { DestroyWindow(g_tech_dlg); }
  g_tech_dlg = NULL; g_tech_list = NULL;
  g_tech_counterpart = g_tech_giver = -1;
  if (play != NULL) { SetFocus(play); }
}

static LRESULT CALLBACK tech_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN && w == VK_RETURN) {
    tech_confirm(); tech_close(); return 0;
  }
  if (m == WM_KEYDOWN && w == VK_ESCAPE) { tech_close(); return 0; }
  return CallWindowProcW(g_tech_default_proc, h, m, w, l);
}

static LRESULT CALLBACK tech_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);
      g_tech_list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_TECHPICK_LIST, hi, NULL);
      if (g_tech_list != NULL) {
        g_tech_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_tech_list, GWLP_WNDPROC, (LONG_PTR)tech_list_proc);
      }
      return 0;
    }
    case WM_SIZE: {
      RECT rc; int pad = 6;
      GetClientRect(h, &rc);
      if (g_tech_list != NULL) {
        MoveWindow(g_tech_list, pad, pad,
                   rc.right - 2 * pad, rc.bottom - 2 * pad, TRUE);
      }
      return 0;
    }
    case WM_CLOSE:   tech_close(); return 0;
    case WM_DESTROY:
      sr_window_unregister_popup(g_tech_dlg);
      g_tech_dlg = NULL; g_tech_list = NULL; return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int ensure_class_generic(HINSTANCE hi, LPCWSTR name, WNDPROC proc)
{
  WNDCLASSEXW wc;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = name;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  return (RegisterClassExW(&wc) != 0
          || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
}

void sr_treaty_pick_tech(int counterpart_plrno, int giver_plrno)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (!ensure_class_generic(hi, SR_TECHPICK_CLASS_NAME, tech_wnd_proc)) {
    return;
  }
  g_tech_counterpart = counterpart_plrno;
  g_tech_giver       = giver_plrno;

  g_tech_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_TECHPICK_CLASS_NAME, L"Pick a tech",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 420, 480, NULL, NULL, hi, NULL);
  if (g_tech_dlg == NULL) {
    g_tech_counterpart = g_tech_giver = -1;
    return;
  }
  sr_window_register_popup(g_tech_dlg);
  tech_populate();
  ShowWindow(g_tech_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_tech_dlg);
  if (g_tech_list != NULL) { SetFocus(g_tech_list); }
}

/* ====================================================================== */
/* Gold picker — single-line EDIT accepting an integer                    */
/* ====================================================================== */

#define SR_GOLDPICK_CLASS_NAME L"FreecivSRTreatyGoldPick"
#define IDC_GOLDPICK_EDIT 8101
#define IDC_GOLDPICK_OK   8102

static HWND g_gold_dlg  = NULL;
static HWND g_gold_edit = NULL;
static HWND g_gold_ok   = NULL;
static WNDPROC g_gold_edit_default_proc = NULL;
static int  g_gold_counterpart = -1;
static int  g_gold_giver       = -1;

static void gold_confirm(void)
{
  wchar_t wbuf[32];
  int n;
  int amount;

  if (g_gold_edit == NULL) { return; }
  n = GetWindowTextW(g_gold_edit, wbuf, 31);
  if (n <= 0) { return; }
  wbuf[n] = L'\0';
  amount = _wtoi(wbuf);
  if (amount < 0) {
    amount = 0;
  }
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           g_gold_counterpart,
                                           g_gold_giver,
                                           CLAUSE_GOLD,
                                           amount);
}

static void gold_close(void)
{
  HWND play = sr_play_hwnd();

  if (g_gold_dlg != NULL) { DestroyWindow(g_gold_dlg); }
  g_gold_dlg = NULL; g_gold_edit = NULL; g_gold_ok = NULL;
  g_gold_counterpart = g_gold_giver = -1;
  if (play != NULL) { SetFocus(play); }
}

static LRESULT CALLBACK gold_edit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_HASSETSEL
           | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN && w == VK_RETURN) {
    gold_confirm(); gold_close(); return 0;
  }
  if (m == WM_KEYDOWN && w == VK_ESCAPE) { gold_close(); return 0; }
  return CallWindowProcW(g_gold_edit_default_proc, h, m, w, l);
}

static LRESULT CALLBACK gold_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      CreateWindowExW(
          0, L"STATIC", L"Gold amount (whole number):",
          WS_CHILD | WS_VISIBLE | SS_LEFT,
          0, 0, 0, 0, h, NULL, hi, NULL);

      g_gold_edit = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"EDIT", L"0",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL | ES_NUMBER,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_GOLDPICK_EDIT, hi, NULL);
      if (g_gold_edit != NULL) {
        g_gold_edit_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_gold_edit, GWLP_WNDPROC, (LONG_PTR)gold_edit_proc);
      }

      g_gold_ok = CreateWindowExW(
          0, L"BUTTON", L"&OK",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_GOLDPICK_OK, hi, NULL);
      return 0;
    }
    case WM_SIZE: {
      RECT rc; int pad = 8;
      HWND child;
      GetClientRect(h, &rc);
      child = GetWindow(h, GW_CHILD);   /* first child == the STATIC label */
      if (child != NULL) {
        MoveWindow(child, pad, pad, rc.right - 2 * pad, 20, TRUE);
      }
      if (g_gold_edit != NULL) {
        MoveWindow(g_gold_edit, pad, pad + 24,
                   rc.right - 2 * pad, 24, TRUE);
      }
      if (g_gold_ok != NULL) {
        MoveWindow(g_gold_ok, pad, pad + 56, 80, 28, TRUE);
      }
      return 0;
    }
    case WM_COMMAND:
      if (LOWORD(w) == IDC_GOLDPICK_OK && HIWORD(w) == BN_CLICKED) {
        gold_confirm(); gold_close(); return 0;
      }
      break;
    case WM_CLOSE:   gold_close(); return 0;
    case WM_DESTROY:
      sr_window_unregister_popup(g_gold_dlg);
      g_gold_dlg = NULL; g_gold_edit = NULL; g_gold_ok = NULL; return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

void sr_treaty_pick_gold(int counterpart_plrno, int giver_plrno)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (!ensure_class_generic(hi, SR_GOLDPICK_CLASS_NAME, gold_wnd_proc)) {
    return;
  }
  g_gold_counterpart = counterpart_plrno;
  g_gold_giver       = giver_plrno;

  g_gold_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_GOLDPICK_CLASS_NAME, L"Gold amount",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, 320, 160, NULL, NULL, hi, NULL);
  if (g_gold_dlg == NULL) {
    g_gold_counterpart = g_gold_giver = -1;
    return;
  }
  sr_window_register_popup(g_gold_dlg);
  ShowWindow(g_gold_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_gold_dlg);
  if (g_gold_edit != NULL) {
    SendMessageW(g_gold_edit, EM_SETSEL, 0, -1);
    SetFocus(g_gold_edit);
  }
}

/* ====================================================================== */
/* City picker                                                            */
/* ====================================================================== */

#define SR_CITYPICK_CLASS_NAME L"FreecivSRTreatyCityPick"
#define IDC_CITYPICK_LIST 8201

static HWND g_cp_dlg  = NULL;
static HWND g_cp_list = NULL;
static WNDPROC g_cp_default_proc = NULL;
static int  g_cp_counterpart = -1;
static int  g_cp_giver       = -1;

static void city_populate(void)
{
  struct player *giver = player_by_number(g_cp_giver);

  if (g_cp_list == NULL || giver == NULL) { return; }
  SendMessageW(g_cp_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_cp_list, LB_RESETCONTENT, 0, 0);

  city_list_iterate(giver->cities, pcity) {
    list_add_utf8(g_cp_list, city_name_get(pcity), (LPARAM)pcity->id);
  } city_list_iterate_end;

  if (SendMessageW(g_cp_list, LB_GETCOUNT, 0, 0) == 0) {
    list_add_utf8(g_cp_list, "(no cities available)", (LPARAM)(-1));
  }
  SendMessageW(g_cp_list, WM_SETREDRAW, TRUE, 0);
  SendMessageW(g_cp_list, LB_SETCURSEL, 0, 0);
}

static void city_confirm(void)
{
  int sel;
  int cid;

  if (g_cp_list == NULL) { return; }
  sel = (int)SendMessageW(g_cp_list, LB_GETCURSEL, 0, 0);
  if (sel < 0) { return; }
  cid = (int)SendMessageW(g_cp_list, LB_GETITEMDATA, (WPARAM)sel, 0);
  if (cid < 0) { return; }
  dsend_packet_diplomacy_create_clause_req(&client.conn,
                                           g_cp_counterpart,
                                           g_cp_giver,
                                           CLAUSE_CITY,
                                           cid);
}

static void city_close(void)
{
  HWND play = sr_play_hwnd();

  if (g_cp_dlg != NULL) { DestroyWindow(g_cp_dlg); }
  g_cp_dlg = NULL; g_cp_list = NULL;
  g_cp_counterpart = g_cp_giver = -1;
  if (play != NULL) { SetFocus(play); }
}

static LRESULT CALLBACK city_list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN && w == VK_RETURN) {
    city_confirm(); city_close(); return 0;
  }
  if (m == WM_KEYDOWN && w == VK_ESCAPE) { city_close(); return 0; }
  return CallWindowProcW(g_cp_default_proc, h, m, w, l);
}

static LRESULT CALLBACK city_wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);
      g_cp_list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_CITYPICK_LIST, hi, NULL);
      if (g_cp_list != NULL) {
        g_cp_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_cp_list, GWLP_WNDPROC, (LONG_PTR)city_list_proc);
      }
      return 0;
    }
    case WM_SIZE: {
      RECT rc; int pad = 6;
      GetClientRect(h, &rc);
      if (g_cp_list != NULL) {
        MoveWindow(g_cp_list, pad, pad,
                   rc.right - 2 * pad, rc.bottom - 2 * pad, TRUE);
      }
      return 0;
    }
    case WM_CLOSE:   city_close(); return 0;
    case WM_DESTROY:
      sr_window_unregister_popup(g_cp_dlg);
      g_cp_dlg = NULL; g_cp_list = NULL; return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

void sr_treaty_pick_city(int counterpart_plrno, int giver_plrno)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (!ensure_class_generic(hi, SR_CITYPICK_CLASS_NAME, city_wnd_proc)) {
    return;
  }
  g_cp_counterpart = counterpart_plrno;
  g_cp_giver       = giver_plrno;

  g_cp_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_CITYPICK_CLASS_NAME, L"Pick a city",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 420, 480, NULL, NULL, hi, NULL);
  if (g_cp_dlg == NULL) {
    g_cp_counterpart = g_cp_giver = -1;
    return;
  }
  sr_window_register_popup(g_cp_dlg);
  city_populate();
  ShowWindow(g_cp_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_cp_dlg);
  if (g_cp_list != NULL) { SetFocus(g_cp_list); }
}

#endif  /* _WIN32 */
