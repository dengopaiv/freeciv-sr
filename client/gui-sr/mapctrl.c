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

#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#endif

/* utility */
#include "fcintl.h"

/* common */
#include "map.h"
#include "tile.h"
#include "unit.h"

/* gui main header */
#include "gui_sr.h"

/* client */
#include "control.h"

/* gui-sr */
#include "sr_speech.h"
#include "sr_window.h"

#include "mapctrl.h"

#ifdef _WIN32

/* ---- City-name input dialog ------------------------------------------ */

#define IDC_CITYNAME_EDIT   2001
#define IDC_CITYNAME_OK     2002
#define IDC_CITYNAME_CANCEL 2003

static int  g_city_tile_index = -1;
static HWND g_city_dlg = NULL;

static LRESULT CALLBACK city_name_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      CreateWindowExW(WS_EX_CLIENTEDGE, L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL,
          8, 8, 340, 24, h, (HMENU)(LONG_PTR)IDC_CITYNAME_EDIT, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"OK",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          8, 40, 100, 28, h, (HMENU)(LONG_PTR)IDC_CITYNAME_OK, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          116, 40, 100, 28, h, (HMENU)(LONG_PTR)IDC_CITYNAME_CANCEL, hi, NULL);
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);

      if (id == IDC_CITYNAME_OK && HIWORD(w) == BN_CLICKED) {
        char name[MAX_LEN_CITYNAME];
        HWND edit = GetDlgItem(h, IDC_CITYNAME_EDIT);

        GetWindowTextA(edit, name, sizeof(name));
        if (name[0] != '\0' && g_city_tile_index >= 0) {
          struct tile *ptile = index_to_tile(&(wld.map), g_city_tile_index);

          if (ptile != NULL) {
            finish_city(ptile, name);
          }
        }
        sr_window_unregister_popup(h);
        DestroyWindow(h);
        g_city_dlg = NULL;
        return 0;
      }
      if (id == IDC_CITYNAME_CANCEL && HIWORD(w) == BN_CLICKED) {
        if (g_city_tile_index >= 0) {
          struct tile *ptile = index_to_tile(&(wld.map), g_city_tile_index);

          if (ptile != NULL) {
            cancel_city(ptile);
          }
        }
        sr_window_unregister_popup(h);
        DestroyWindow(h);
        g_city_dlg = NULL;
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      /* Treat closing the dialog as cancel. */
      SendMessageW(h, WM_COMMAND,
                   MAKEWPARAM(IDC_CITYNAME_CANCEL, BN_CLICKED), 0);
      return 0;
    case WM_DESTROY:
      g_city_dlg = NULL;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int ensure_cityname_class(HINSTANCE hi)
{
  static int registered = 0;
  WNDCLASSEXW wc;

  if (registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.lpfnWndProc   = city_name_proc;
  wc.hInstance      = hi;
  wc.hCursor        = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground  = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName  = L"FreecivSR_CityName";
  registered = RegisterClassExW(&wc) != 0
               || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return registered;
}

#endif /* _WIN32 */

/**********************************************************************//**
  Popup a dialog to ask for the name of a new city.  The given string
  should be used as a suggestion.
**************************************************************************/
void popup_newcity_dialog(struct unit *punit, const char *suggestname)
{
#ifdef _WIN32
  HINSTANCE hi = GetModuleHandleW(NULL);
  HWND edit;
  char buf[256];

  if (g_city_dlg != NULL) {
    /* Already open — bring to front. */
    SetForegroundWindow(g_city_dlg);
    return;
  }

  if (!ensure_cityname_class(hi)) { return; }

  g_city_tile_index = tile_index(unit_tile(punit));

  g_city_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      L"FreecivSR_CityName", L"Name New City",
      WS_POPUP | WS_CAPTION | WS_SYSMENU,
      CW_USEDEFAULT, CW_USEDEFAULT, 380, 110,
      NULL, NULL, hi, NULL);
  if (g_city_dlg == NULL) { return; }

  sr_window_register_popup(g_city_dlg);

  /* Pre-fill the suggested name. */
  edit = GetDlgItem(g_city_dlg, IDC_CITYNAME_EDIT);
  if (edit != NULL && suggestname != NULL) {
    SetWindowTextA(edit, suggestname);
  }

  ShowWindow(g_city_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_city_dlg);
  if (edit != NULL) {
    SetFocus(edit);
    SendMessageW(edit, EM_SETSEL, 0, -1);  /* select all */
  }

  fc_snprintf(buf, sizeof(buf),
              _("Name new city. Suggested: %s. Press Enter to accept."),
              suggestname != NULL ? suggestname : "");
  sr_say(buf, 1);
#else
  /* Non-Windows: auto-accept the suggested name. */
  if (suggestname != NULL && suggestname[0] != '\0') {
    finish_city(unit_tile(punit), suggestname);
  }
#endif
}

/**********************************************************************//**
  A turn done button should be provided for the player.  This function
  is called to toggle it between active/inactive.
**************************************************************************/
void set_turn_done_button_state(bool state)
{
  /* PORTME */
}

/**********************************************************************//**
  Draw a goto or patrol line at the current mouse position.
**************************************************************************/
void create_line_at_mouse_pos(void)
{
  /* PORTME */
}

/**********************************************************************//**
 The Area Selection rectangle. Called by center_tile_mapcanvas() and
 when the mouse pointer moves.
**************************************************************************/
void update_rect_at_mouse_pos(void)
{
  /* PORTME */
}
