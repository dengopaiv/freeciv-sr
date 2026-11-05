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
#include "government.h"
#include "improvement.h"
#include "nation.h"
#include "player.h"
#include "research.h"
#include "tech.h"

/* client */
#include "client_main.h"

/* gui-sr */
#include "sr_play.h"
#include "sr_window.h"

#include "sr_inteldlg.h"

#define SR_INTEL_CLASS_NAME L"FreecivSRIntelDlg"
#define IDC_INTEL_LIST     7201

static HWND g_dlg  = NULL;
static HWND g_list = NULL;
static int  g_pplayer_no = -1;
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
  struct player *them;
  struct player *me = client_player();
  char line[320];
  bool has_embassy;

  if (g_list == NULL) { return; }
  SendMessageW(g_list, WM_SETREDRAW, FALSE, 0);
  SendMessageW(g_list, LB_RESETCONTENT, 0, 0);

  them = g_pplayer_no >= 0 ? player_by_number(g_pplayer_no) : NULL;
  if (them == NULL) {
    list_add_utf8("(player not found)");
    SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
    return;
  }

  has_embassy = (me != NULL && player_has_embassy(me, them));

  fc_snprintf(line, sizeof(line), "Nation: %s",
              nation_adjective_for_player(them));
  list_add_utf8(line);
  fc_snprintf(line, sizeof(line), "Ruler: %s", player_name(them));
  list_add_utf8(line);

  if (them->government != NULL) {
    fc_snprintf(line, sizeof(line), "Government: %s",
                government_name_translation(them->government));
  } else {
    fc_snprintf(line, sizeof(line), "Government: unknown");
  }
  list_add_utf8(line);

  {
    struct city *capital = player_primary_capital(them);

    fc_snprintf(line, sizeof(line), "Capital: %s",
                capital != NULL ? city_name_get(capital) : "unknown");
    list_add_utf8(line);
  }

  fc_snprintf(line, sizeof(line), "Score: %d", them->score.game);
  list_add_utf8(line);

  if (has_embassy) {
    fc_snprintf(line, sizeof(line),
                "Gold: %d  \u00B7  Tax %d%% / Luxury %d%% / Science %d%%",
                them->economic.gold,
                them->economic.tax, them->economic.luxury,
                them->economic.science);
    list_add_utf8(line);

    {
      struct research *r = research_get(them);

      if (r != NULL && r->researching != A_UNSET && r->researching != A_UNKNOWN) {
        fc_snprintf(line, sizeof(line), "Researching: %s  (%d of %d bulbs)",
                    advance_name_translation(advance_by_number(r->researching)),
                    r->bulbs_researched,
                    research_total_bulbs_required(r, r->researching, FALSE));
        list_add_utf8(line);
      } else {
        list_add_utf8("Researching: (nothing)");
      }
    }

    /* Great wonders built by this player (always informative even
       without an embassy in real freeciv, but kept embassy-gated here
       for parity with the other intel data). */
    {
      int wonder_count = 0;
      char wbuf[4096];
      int wpos = 0;

      wbuf[0] = '\0';
      improvement_iterate(pimprove) {
        if (!is_great_wonder(pimprove)) {
          continue;
        }
        if (wonder_is_built(them, pimprove)) {
          struct city *wcity = city_from_wonder(them, pimprove);
          int n;

          n = fc_snprintf(wbuf + wpos, sizeof(wbuf) - wpos,
                          "%s%s%s%s",
                          wonder_count == 0 ? "" : ", ",
                          improvement_name_translation(pimprove),
                          wcity != NULL ? " (" : "",
                          wcity != NULL ? city_name_get(wcity) : "");
          if (n <= 0 || wpos + n >= (int) sizeof(wbuf)) {
            break;
          }
          wpos += n;
          if (wcity != NULL && wpos < (int) sizeof(wbuf) - 1) {
            wbuf[wpos++] = ')';
            wbuf[wpos] = '\0';
          }
          wonder_count++;
        }
      } improvement_iterate_end;
      list_add_utf8("");
      fc_snprintf(line, sizeof(line), "Great wonders built (%d):", wonder_count);
      list_add_utf8(line);
      if (wonder_count > 0) {
        list_add_utf8(wbuf);
      } else {
        list_add_utf8("  (none)");
      }
    }

    /* Known techs (compact list). */
    {
      struct research *r = research_get(them);
      char buf[4096];
      int pos = 0;
      int known = 0;
      Tech_type_id ac = advance_count();

      buf[0] = '\0';
      if (r != NULL) {
        advance_index_iterate_max(A_FIRST, i, ac) {
          if (r->inventions[i].state == TECH_KNOWN) {
            int n;

            n = fc_snprintf(buf + pos, sizeof(buf) - pos,
                            "%s%s",
                            known == 0 ? "" : ", ",
                            advance_name_translation(advance_by_number(i)));
            if (n <= 0 || pos + n >= (int)sizeof(buf)) {
              break;
            }
            pos += n;
            known++;
          }
        } advance_index_iterate_max_end;
      }
      list_add_utf8("");
      fc_snprintf(line, sizeof(line), "Known techs (%d):", known);
      list_add_utf8(line);
      if (pos > 0) {
        list_add_utf8(buf);
      } else {
        list_add_utf8("  (none known to them)");
      }
    }
  } else {
    list_add_utf8("(no embassy — limited information)");
  }

  SendMessageW(g_list, WM_SETREDRAW, TRUE, 0);
  InvalidateRect(g_list, NULL, TRUE);
  if (SendMessageW(g_list, LB_GETCOUNT, 0, 0) > 0) {
    SendMessageW(g_list, LB_SETCURSEL, 0, 0);
  }
}

static LRESULT CALLBACK list_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_KEYDOWN && w == VK_ESCAPE) { sr_inteldlg_close(); return 0; }
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
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_INTEL_LIST, hi, NULL);
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
    case WM_CLOSE:   sr_inteldlg_close(); return 0;
    case WM_DESTROY:
      sr_window_unregister_popup(g_dlg);
      g_dlg = NULL; g_list = NULL; g_pplayer_no = -1; return 0;
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
  wc.lpszClassName = SR_INTEL_CLASS_NAME;
  wc.hIcon = LoadIconW(NULL, IDI_APPLICATION);
  if (RegisterClassExW(&wc) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    done = 1; return 1;
  }
  return 0;
}

void sr_inteldlg_open(struct player *pplayer)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  wchar_t title[128];
  char title_utf8[128];

  if (pplayer == NULL) { return; }

  g_pplayer_no = player_number(pplayer);

  if (g_dlg != NULL) {
    populate();
    SetForegroundWindow(g_dlg);
    if (g_list != NULL) { SetFocus(g_list); }
    return;
  }
  if (!ensure_class(hi)) { return; }
  fc_snprintf(title_utf8, sizeof(title_utf8), "Intel: %s",
              nation_plural_for_player(pplayer));
  MultiByteToWideChar(CP_UTF8, 0, title_utf8, -1, title,
                      sizeof(title) / sizeof(title[0]));
  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_INTEL_CLASS_NAME, title,
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 620, 520, NULL, NULL, hi, NULL);
  if (g_dlg == NULL) { g_pplayer_no = -1; return; }
  sr_window_register_popup(g_dlg);
  populate();
  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_list != NULL) { SetFocus(g_list); }
}

void sr_inteldlg_close(void)
{
  HWND p;
  if (g_dlg != NULL) { DestroyWindow(g_dlg); }
  p = sr_play_hwnd();
  if (p != NULL) { SetFocus(p); }
}

int sr_inteldlg_is_open(void) { return g_dlg != NULL; }

#endif
