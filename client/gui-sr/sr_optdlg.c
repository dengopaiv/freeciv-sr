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
#include "string_vector.h"
#include "support.h"

/* client */
#include "options.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_play.h"
#include "sr_window.h"

#include "sr_optdlg.h"

#define SR_OPT_CLASS_NAME L"FreecivSROptionsDlg"
#define IDC_OPT_LIST      7301

static HWND g_dlg = NULL;
static HWND g_list = NULL;
static WNDPROC g_list_default_proc = NULL;

static void list_add_utf8(const char *utf8, LPARAM lp)
{
  int wlen;
  wchar_t *w;
  int index;

  if (g_list == NULL) { return; }
  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) { return; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return; }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  index = (int)SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)w);
  free(w);
  if (index >= 0) {
    SendMessageW(g_list, LB_SETITEMDATA, (WPARAM)index, lp);
  }
}

/* Build a "Option name [value] — description" row. */
static void build_row(struct option *opt)
{
  char line[512];
  enum option_type t = option_type(opt);
  char value[128];

  value[0] = '\0';
  switch (t) {
    case OT_BOOLEAN:
      fc_strlcpy(value, option_bool_get(opt) ? "on" : "off", sizeof(value));
      break;
    case OT_INTEGER:
      fc_snprintf(value, sizeof(value), "%d", option_int_get(opt));
      break;
    case OT_STRING: {
      const char *s = option_str_get(opt);
      fc_snprintf(value, sizeof(value), "\"%s\"", s != NULL ? s : "");
      break;
    }
    case OT_ENUM: {
      const char *s = option_enum_get_str(opt);
      fc_strlcpy(value, s != NULL ? s : "?", sizeof(value));
      break;
    }
    case OT_BITWISE:
      fc_snprintf(value, sizeof(value), "0x%x", option_bitwise_get(opt));
      break;
    case OT_FONT: {
      const char *s = option_font_get(opt);
      fc_snprintf(value, sizeof(value), "\"%s\"", s != NULL ? s : "");
      break;
    }
    case OT_COLOR:
    case OT_VIDEO_MODE:
      fc_strlcpy(value, "(complex)", sizeof(value));
      break;
  }
  fc_snprintf(line, sizeof(line), "%s  [%s]  —  %s",
              option_description(opt) != NULL
                ? option_description(opt) : option_name(opt),
              value,
              option_category_name(opt));
  /* lParam = option number for lookup on activate. */
  list_add_utf8(line, (LPARAM)option_number(opt));
}

static void populate(void)
{
  if (g_list == NULL) { return; }
  SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_list, LB_RESETCONTENT, 0, 0);

  options_iterate(client_optset, opt) {
    build_row(opt);
  } options_iterate_end;

  SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_list, NULL, TRUE);
  if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) > 0) {
    SendMessageW(g_list, LB_SETCURSEL, 0, 0);
  }
}

static void activate_current(void)
{
  int sel;
  int opt_no;
  struct option *opt;
  enum option_type t;
  char msg[256];

  if (g_list == NULL) { return; }
  sel = (int)SendMessageW(g_list, LB_GETCURSEL, 0, 0);
  if (sel < 0) { return; }
  opt_no = (int)SendMessageW(g_list, LB_GETITEMDATA, (WPARAM)sel, 0);
  opt = optset_option_by_number(client_optset, opt_no);
  if (opt == NULL || !option_is_changeable(opt)) {
    sr_log_append("Option not changeable here.");
    return;
  }
  t = option_type(opt);

  if (t == OT_BOOLEAN) {
    bool cur = option_bool_get(opt);
    option_bool_set(opt, !cur);
    fc_snprintf(msg, sizeof(msg), "Option '%s' → %s",
                option_name(opt), !cur ? "on" : "off");
    sr_log_append(msg);
  } else if (t == OT_ENUM) {
    const struct strvec *vals = option_enum_values(opt);
    int cur = option_enum_get_int(opt);
    int count = vals != NULL ? (int)strvec_size(vals) : 0;

    if (count > 0) {
      int next = (cur + 1) % count;
      option_enum_set_int(opt, next);
      fc_snprintf(msg, sizeof(msg), "Option '%s' → %s",
                  option_name(opt), option_enum_get_str(opt));
      sr_log_append(msg);
    }
  } else {
    fc_snprintf(msg, sizeof(msg),
                "Option '%s' (%s) not editable here — use the command line or settings file.",
                option_name(opt),
                option_type_name(t));
    sr_log_append(msg);
  }

  /* Rebuild to reflect the new value. */
  populate();
  if (sel >= 0) {
    SendMessageW(g_list, LB_SETCURSEL, (WPARAM)sel, 0);
  }
}

static LRESULT CALLBACK list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { activate_current(); return 0; }
    if (w == VK_ESCAPE) { sr_optdlg_close(); return 0; }
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
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_OPT_LIST, hi, NULL);
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
    case WM_COMMAND:
      if (HIWORD(w) == LBN_DBLCLK && (HWND)l == g_list) {
        activate_current(); return 0;
      }
      break;
    case WM_CLOSE:   sr_optdlg_close(); return 0;
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
  wc.lpszClassName = SR_OPT_CLASS_NAME;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    done = 1; return 1;
  }
  return 0;
}

void sr_optdlg_open(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  if (g_dlg != NULL) {
    populate(); SetForegroundWindow(g_dlg);
    if (g_list != NULL) { SetFocus(g_list); } return;
  }
  if (!ensure_class(hi)) { return; }
  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_OPT_CLASS_NAME, L"Options",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 760, 560, NULL, NULL, hi, NULL);
  if (g_dlg == NULL) { return; }
  sr_window_register_popup(g_dlg);
  populate();
  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_list != NULL) { SetFocus(g_list); }
}

void sr_optdlg_close(void)
{
  HWND p;
  if (g_dlg != NULL) { DestroyWindow(g_dlg); }
  p = sr_play_hwnd();
  if (p != NULL) { SetFocus(p); }
}

void sr_optdlg_refresh(void) { if (g_dlg != NULL) { populate(); } }
int sr_optdlg_is_open(void) { return g_dlg != NULL; }

#endif
