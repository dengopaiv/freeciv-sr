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
#include "game.h"
#include "player.h"
#include "unit.h"
#include "unitlist.h"
#include "unittype.h"

/* client */
#include "client_main.h"

/* gui-sr */
#include "sr_play.h"
#include "sr_window.h"

#include "sr_unitsdlg.h"

#define SR_UNITS_CLASS_NAME L"FreecivSRUnitsDlg"
#define IDC_UNITS_LIST     7101

static HWND g_dlg  = NULL;
static HWND g_list = NULL;
static WNDPROC g_list_default_proc = NULL;

static void list_add_utf8(const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (g_list == NULL) { return; }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) { return; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return; }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
}

static void populate(void)
{
  struct player *pl = client_player();
  int active[U_LAST] = { 0 };
  int building[U_LAST] = { 0 };
  int total = 0;
  char line[256];

  if (g_list == NULL) { return; }
  SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_list, LB_RESETCONTENT, 0, 0);

  if (pl == NULL) {
    list_add_utf8("(not in a game)");
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    return;
  }

  unit_list_iterate(pl->units, punit) {
    active[utype_index(unit_type_get(punit))]++;
    total++;
  } unit_list_iterate_end;

  city_list_iterate(pl->cities, pcity) {
    if (VUT_UTYPE == pcity->production.kind) {
      int idx = utype_index(pcity->production.value.utype);

      if (idx >= 0 && idx < U_LAST) {
        building[idx]++;
      }
    }
  } city_list_iterate_end;

  fc_snprintf(line, sizeof(line), "Total units: %d", total);
  list_add_utf8(line);
  list_add_utf8("");

  unit_type_iterate(ut) {
    int idx = utype_index(ut);

    if (active[idx] > 0 || building[idx] > 0) {
      fc_snprintf(line, sizeof(line),
                  "%s  \u00B7  active %d  \u00B7  building %d",
                  utype_name_translation(ut),
                  active[idx], building[idx]);
      list_add_utf8(line);
    }
  } unit_type_iterate_end;

  SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_list, NULL, TRUE);
  if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) > 0) {
    SendMessageW(g_list, LB_SETCURSEL, 0, 0);
  }
}

static LRESULT CALLBACK list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_KEYDOWN && w == VK_ESCAPE) { sr_unitsdlg_close(); return 0; }
  return CallWindowProcW(g_list_default_proc, h, m, w, l);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);
      g_list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_UNITS_LIST, hi, NULL);
      if (g_list != NULL) {
        g_list_default_proc = (WNDPROC)SetWindowLongPtrW(
            g_list, GWLP_WNDPROC, (LONG_PTR)list_proc);
      }
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 6;
      GetClientRect(h, &rc);
      if (g_list != NULL) {
        MoveWindow(g_list, pad, pad,
                   rc.right - 2 * pad, rc.bottom - 2 * pad, TRUE);
      }
      return 0;
    }
    case WM_CLOSE:   sr_unitsdlg_close(); return 0;
    case WM_DESTROY:
      sr_window_unregister_popup(g_dlg);
      g_dlg = NULL; g_list = NULL; return 0;
    default: break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int ensure_class(HINSTANCE hi)
{
  static int done = 0;
  WNDCLASSEXW wc;
  if (done) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.style = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc = wnd_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_UNITS_CLASS_NAME;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    done = 1; return 1;
  }
  return 0;
}

void sr_unitsdlg_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  if (g_dlg != NULL) {
    populate(); SetForegroundWindow(g_dlg);
    if (g_list != NULL) { SetFocus(g_list); } return;
  }
  if (!ensure_class(hi)) { return; }
  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_UNITS_CLASS_NAME, L"Units",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 620, 520, NULL, NULL, hi, NULL);
  if (g_dlg == NULL) { return; }
  sr_window_register_popup(g_dlg);
  populate();
  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_list != NULL) { SetFocus(g_list); }
}

void sr_unitsdlg_close(void)
{
  HWND p;
  if (g_dlg != NULL) { DestroyWindow(g_dlg); }
  p = sr_play_hwnd();
  if (p != NULL) { SetFocus(p); }
}

void sr_unitsdlg_refresh(void) { if (g_dlg != NULL) { populate(); } }
int sr_unitsdlg_is_open(void) { return g_dlg != NULL; }

#endif
