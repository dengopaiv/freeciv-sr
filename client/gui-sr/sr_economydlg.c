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
#include "improvement.h"

/* client */
#include "client_main.h"
#include "repodlgs_common.h"

/* gui-sr */
#include "sr_play.h"
#include "sr_window.h"

#include "sr_economydlg.h"

#define SR_ECON_CLASS_NAME L"FreecivSREconomyDlg"
#define IDC_ECON_LIST      7001

static HWND g_dlg  = NULL;
static HWND g_list = NULL;
static WNDPROC g_list_default_proc = NULL;

static void list_add_utf8(const char *utf8)
{
  int wlen;
  wchar_t *w;

  if (g_list == NULL) {
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

static void populate(void)
{
  struct improvement_entry bld[B_LAST];
  struct unit_entry units[U_LAST];
  int n_bld = 0, n_units = 0;
  int total_cost = 0, total_income = 0, total_unit_cost = 0;
  char line[256];
  int i;

  if (g_list == NULL) {
    return;
  }
  SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_list, LB_RESETCONTENT, 0, 0);

  get_economy_report_data(bld, &n_bld, &total_cost, &total_income);
  get_economy_report_units_data(units, &n_units, &total_unit_cost);

  fc_snprintf(line, sizeof(line),
              "Gold income: %+d  \u00B7  building upkeep: %d  \u00B7  unit upkeep: %d",
              total_income, total_cost, total_unit_cost);
  list_add_utf8(line);
  list_add_utf8("");
  list_add_utf8("— Improvements —");
  for (i = 0; i < n_bld; i++) {
    fc_snprintf(line, sizeof(line),
                "%s  \u00B7  count %d  \u00B7  redundant %d  \u00B7  upkeep %d  \u00B7  total %d",
                improvement_name_translation(bld[i].type),
                bld[i].count, bld[i].redundant, bld[i].cost, bld[i].total_cost);
    list_add_utf8(line);
  }
  list_add_utf8("");
  list_add_utf8("— Units (gold upkeep) —");
  for (i = 0; i < n_units; i++) {
    fc_snprintf(line, sizeof(line),
                "%s  \u00B7  count %d  \u00B7  each %d  \u00B7  total %d",
                utype_name_translation(units[i].type),
                units[i].count, units[i].cost, units[i].total_cost);
    list_add_utf8(line);
  }

  SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_list, NULL, TRUE);

  if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) > 0) {
    SendMessageW(g_list, LB_SETCURSEL, 0, 0);
  }
}

static LRESULT CALLBACK list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_KEYDOWN && w == VK_ESCAPE) {
    sr_economydlg_close();
    return 0;
  }
  return CallWindowProcW(g_list_default_proc, h, m, w, l);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      g_list = CreateWindowExW(
          WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0,
          h, (HMENU)(LONG_PTR)IDC_ECON_LIST, hi, NULL);
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
    case WM_CLOSE:   sr_economydlg_close(); return 0;
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
  wc.lpszClassName = SR_ECON_CLASS_NAME;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    done = 1; return 1;
  }
  return 0;
}

void sr_economydlg_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (g_dlg != NULL) {
    populate();
    SetForegroundWindow(g_dlg);
    if (g_list != NULL) { SetFocus(g_list); }
    return;
  }
  if (!ensure_class(hi)) { return; }
  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_ECON_CLASS_NAME, L"Economy",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 680, 520,
      NULL, NULL, hi, NULL);
  if (g_dlg == NULL) { return; }
  sr_window_register_popup(g_dlg);
  populate();
  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_list != NULL) { SetFocus(g_list); }
}

void sr_economydlg_close(void)
{
  HWND p;

  if (g_dlg != NULL) { DestroyWindow(g_dlg); }
  p = sr_play_hwnd();
  if (p != NULL) { SetFocus(p); }
}

void sr_economydlg_refresh(void)
{
  if (g_dlg != NULL) { populate(); }
}

int sr_economydlg_is_open(void) { return g_dlg != NULL; }

#endif
