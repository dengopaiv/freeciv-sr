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
#include "astring.h"
#include "fcintl.h"
#include "support.h"

/* common */
#include "city.h"
#include "game.h"
#include "improvement.h"
#include "map.h"
#include "player.h"
#include "specialist.h"
#include "terrain.h"
#include "tile.h"
#include "unit.h"
#include "unitlist.h"
#include "unittype.h"
#include "worklist.h"

/* client */
#include "citydlg_common.h"
#include "client_main.h"
#include "control.h"
#include "text.h"

/* gui-sr */
#include "sr_play.h"
#include "sr_speech.h"
#include "sr_window.h"

#include "sr_citydlg.h"

#define SR_CITYDLG_CLASS_NAME   L"FreecivSRCityDlg"
#define SR_PRODPICK_CLASS_NAME  L"FreecivSRProdPick"
#define SR_SELLPICK_CLASS_NAME  L"FreecivSRSellPick"
#define SR_WLEDIT_CLASS_NAME    L"FreecivSRWorklistEdit"
#define SR_TILEEDIT_CLASS_NAME  L"FreecivSRTileEdit"

#define IDC_CITY_EDIT       2001
#define IDC_BTN_PRODUCTION  2002
#define IDC_BTN_BUY         2003
#define IDC_BTN_SELL        2004
#define IDC_BTN_WORKLIST    2005
#define IDC_BTN_TILES       2006

/* Production picker controls */
#define IDC_PROD_LIST       2010
#define IDC_PROD_OK         2011
#define IDC_PROD_CANCEL     2012

/* Sell picker controls */
#define IDC_SELL_LIST       2020
#define IDC_SELL_OK         2021
#define IDC_SELL_CANCEL     2022

/* Worklist editor controls */
#define IDC_WL_DST          2030
#define IDC_WL_SRC          2031
#define IDC_WL_ADD          2032
#define IDC_WL_REMOVE       2033
#define IDC_WL_UP           2034
#define IDC_WL_DOWN         2035
#define IDC_WL_CLEAR        2036
#define IDC_WL_OK           2037
#define IDC_WL_CANCEL       2038

/* Tile editor controls */
#define IDC_TILE_WORKERS    2040
#define IDC_TILE_IDLE       2041
#define IDC_TILE_SPECS      2042
#define IDC_TILE_CLOSE      2043

static HWND g_dlg  = NULL;
static HWND g_edit = NULL;
static HWND g_btn_prod = NULL;
static HWND g_btn_buy  = NULL;
static HWND g_btn_sell = NULL;
static HWND g_btn_wl   = NULL;
static HWND g_btn_tiles = NULL;
static int  g_pcity_id = 0;   /* current city id, 0 = none */

/* Forward declarations. */
static void fill_text(void);

/* ====================================================================== */
/* Text composition                                                       */
/* ====================================================================== */

/* Safely extract a display name from a universal. Returns "?" on any
   invalid state (NULL pointer inside value union, unsupported kind).
   Defends against upstream dataio_raw.c:955 FIXME where a received
   universal can have a known kind but a NULL inner pointer. */
static const char *sr_universal_name(const struct universal *u)
{
  if (u == NULL) {
    return "?";
  }
  if (u->kind == VUT_UTYPE) {
    return u->value.utype != NULL
      ? utype_name_translation(u->value.utype) : "?";
  }
  if (u->kind == VUT_IMPROVEMENT) {
    return u->value.building != NULL
      ? improvement_name_translation(u->value.building) : "?";
  }
  return "?";
}

static const char *production_name(const struct city *pcity)
{
  const char *n = sr_universal_name(&pcity->production);

  return n[0] == '?' && n[1] == '\0' ? "(nothing)" : n;
}

static const char *compose_city_text(const struct city *pcity)
{
  static struct astring str = ASTRING_INIT;
  char buf[512];

  astr_clear(&str);

  astr_add_line(&str, "%s  —  size %d",
                city_name_get(pcity), city_size_get(pcity));
  if (city_owner(pcity) != NULL) {
    astr_add_line(&str, "Owner: %s", player_name(city_owner(pcity)));
  }

  astr_add_line(&str, "");
  astr_add_line(&str, "Surplus:");
  astr_add_line(&str, "  Food    %+d",  pcity->surplus[O_FOOD]);
  astr_add_line(&str, "  Shields %+d",  pcity->surplus[O_SHIELD]);
  astr_add_line(&str, "  Trade   %+d",  pcity->surplus[O_TRADE]);
  astr_add_line(&str, "  Gold    %+d",  pcity->surplus[O_GOLD]);
  astr_add_line(&str, "  Luxury  %+d",  pcity->surplus[O_LUXURY]);
  astr_add_line(&str, "  Science %+d",  pcity->surplus[O_SCIENCE]);

  astr_add_line(&str, "");
  astr_add_line(&str, "Production:");
  astr_add_line(&str, "  Building: %s", production_name(pcity));
  {
    int turns = city_production_turns_to_build(pcity, TRUE);

    astr_add_line(&str, "  Turns to complete: %d", turns);
    astr_add_line(&str, "  Shields stocked: %d / %d",
                  pcity->shield_stock,
                  city_production_build_shield_cost(pcity));
  }
  astr_add_line(&str, "  Buy cost: %d gold", pcity->client.buy_cost);

  astr_add_line(&str, "");
  astr_add_line(&str, "Citizens:");
  astr_add_line(&str, "  Happy    %d",
                pcity->feel[CITIZEN_HAPPY][FEELING_FINAL]);
  astr_add_line(&str, "  Content  %d",
                pcity->feel[CITIZEN_CONTENT][FEELING_FINAL]);
  astr_add_line(&str, "  Unhappy  %d",
                pcity->feel[CITIZEN_UNHAPPY][FEELING_FINAL]);
  astr_add_line(&str, "  Angry    %d",
                pcity->feel[CITIZEN_ANGRY][FEELING_FINAL]);

  /* Improvements built. */
  astr_add_line(&str, "");
  astr_add_line(&str, "Improvements:");
  {
    int n = 0;

    improvement_iterate(pimprove) {
      if (city_has_building(pcity, pimprove)) {
        int sell_price = impr_sell_gold(pimprove);

        if (sell_price > 0) {
          astr_add_line(&str, "  %s (sell: %d gold)",
                        improvement_name_translation(pimprove), sell_price);
        } else {
          astr_add_line(&str, "  %s",
                        improvement_name_translation(pimprove));
        }
        n++;
      }
    } improvement_iterate_end;
    if (n == 0) {
      astr_add_line(&str, "  (none)");
    }
  }

  /* Supported units. */
  astr_add_line(&str, "");
  astr_add_line(&str, "Supported units: %d",
                unit_list_size(pcity->units_supported));
  unit_list_iterate(pcity->units_supported, punit) {
    astr_add_line(&str, "  %s",
                  utype_name_translation(unit_type_get(punit)));
  } unit_list_iterate_end;

  /* Food breakdown. */
  astr_add_line(&str, "");
  astr_add_line(&str, "Food breakdown:");
  get_city_dialog_output_text(pcity, O_FOOD, buf, sizeof(buf));
  astr_add_line(&str, "%s", buf);

  astr_add_line(&str, "");
  /* Worklist summary. */
  astr_add_line(&str, "");
  astr_add_line(&str, "Worklist: %d queued", worklist_length(&pcity->worklist));
  {
    int i, len = worklist_length(&pcity->worklist);

    for (i = 0; i < len && i < 8; i++) {
      struct universal u;

      if (worklist_peek_ith(&pcity->worklist, &u, i)) {
        astr_add_line(&str, "  %d. %s", i + 1, sr_universal_name(&u));
      }
    }
    if (len > 8) {
      astr_add_line(&str, "  ... (%d more)", len - 8);
    }
  }

  astr_add_line(&str, "");
  astr_add_line(&str, "Keys: P = production, B = buy, S = sell, W = worklist, T = tiles, Escape = close.");

  return astr_str(&str);
}

/* ====================================================================== */
/* Production picker                                                      */
/* ====================================================================== */

/* Store buildable items for the picker list. */
#define MAX_PROD_ITEMS 512
static struct universal prod_items[MAX_PROD_ITEMS];
static int prod_item_count = 0;
static HWND g_prod_dlg = NULL;

static void open_production_picker(void);
static void close_production_picker(void);

static LRESULT CALLBACK sr_prodpick_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_PROD_LIST, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"OK",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_PROD_OK, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_PROD_CANCEL, hi, NULL);
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 8, btn_h = 28;

      GetClientRect(h, &rc);
      {
        HWND list = GetDlgItem(h, IDC_PROD_LIST);
        HWND ok = GetDlgItem(h, IDC_PROD_OK);
        HWND cancel = GetDlgItem(h, IDC_PROD_CANCEL);

        if (list) MoveWindow(list, pad, pad, rc.right - 2*pad,
                             rc.bottom - 3*pad - btn_h, TRUE);
        if (ok) MoveWindow(ok, pad, rc.bottom - pad - btn_h,
                           120, btn_h, TRUE);
        if (cancel) MoveWindow(cancel, pad + 128,
                               rc.bottom - pad - btn_h, 120, btn_h, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);

      if (id == IDC_PROD_OK && HIWORD(w) == BN_CLICKED) {
        HWND list = GetDlgItem(h, IDC_PROD_LIST);
        int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);

        if (sel >= 0 && sel < prod_item_count) {
          struct city *pcity = game_city_by_number(g_pcity_id);

          if (pcity != NULL) {
            city_change_production(pcity, &prod_items[sel]);
            {
              char msg[256];

              fc_snprintf(msg, sizeof(msg), _("Now building: %s"),
                          sr_universal_name(&prod_items[sel]));
              sr_say(msg, 1);
            }
          }
        }
        close_production_picker();
        return 0;
      }
      if (id == IDC_PROD_CANCEL && HIWORD(w) == BN_CLICKED) {
        close_production_picker();
        return 0;
      }
      if (id == IDC_PROD_LIST && HIWORD(w) == LBN_DBLCLK) {
        /* Double-click = OK */
        SendMessageW(h, WM_COMMAND, MAKEWPARAM(IDC_PROD_OK, BN_CLICKED), 0);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      close_production_picker();
      return 0;
    case WM_DESTROY:
      g_prod_dlg = NULL;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int ensure_prodpick_class(HINSTANCE hi)
{
  static int reg = 0;
  WNDCLASSEXW wc;

  if (reg) return 1;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = sr_prodpick_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_PRODPICK_CLASS_NAME;
  reg = RegisterClassExW(&wc) != 0
        || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return reg;
}

static void open_production_picker(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND list;
  char label[256];

  if (pcity == NULL) { return; }
  if (g_prod_dlg != NULL) { SetForegroundWindow(g_prod_dlg); return; }
  if (!ensure_prodpick_class(hi)) { return; }

  g_prod_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_PRODPICK_CLASS_NAME, L"Change Production",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 500, 400,
      NULL, NULL, hi, NULL);
  if (g_prod_dlg == NULL) { return; }
  sr_window_register_popup(g_prod_dlg);

  list = GetDlgItem(g_prod_dlg, IDC_PROD_LIST);
  prod_item_count = 0;

  /* Add buildable improvements. */
  improvement_iterate(pimprove) {
    if (can_city_build_improvement_now(pcity, pimprove)
        && prod_item_count < MAX_PROD_ITEMS) {
      int turns;

      prod_items[prod_item_count].kind = VUT_IMPROVEMENT;
      prod_items[prod_item_count].value.building = pimprove;
      turns = city_turns_to_build(pcity, &prod_items[prod_item_count],
                                  TRUE);
      fc_snprintf(label, sizeof(label), "[Building] %s (%d turns)",
                  improvement_name_translation(pimprove), turns);
      {
        wchar_t wlabel[256];

        MultiByteToWideChar(CP_UTF8, 0, label, -1, wlabel, 256);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wlabel);
      }
      prod_item_count++;
    }
  } improvement_iterate_end;

  /* Add buildable units. */
  unit_type_iterate(punittype) {
    if (can_city_build_unit_now(&(wld.map), pcity, punittype)
        && prod_item_count < MAX_PROD_ITEMS) {
      int turns;

      prod_items[prod_item_count].kind = VUT_UTYPE;
      prod_items[prod_item_count].value.utype = punittype;
      turns = city_turns_to_build(pcity, &prod_items[prod_item_count],
                                  TRUE);
      fc_snprintf(label, sizeof(label), "[Unit] %s (%d turns)",
                  utype_name_translation(punittype), turns);
      {
        wchar_t wlabel[256];

        MultiByteToWideChar(CP_UTF8, 0, label, -1, wlabel, 256);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wlabel);
      }
      prod_item_count++;
    }
  } unit_type_iterate_end;

  if (prod_item_count > 0) {
    SendMessageW(list, LB_SETCURSEL, 0, 0);
  }

  ShowWindow(g_prod_dlg, SW_SHOWNORMAL);
  if (list) { SetFocus(list); }

  {
    char msg[128];

    fc_snprintf(msg, sizeof(msg), _("Choose production. %d items available."),
                prod_item_count);
    sr_say(msg, 1);
  }
}

static void close_production_picker(void)
{
  if (g_prod_dlg != NULL) {
    sr_window_unregister_popup(g_prod_dlg);
    DestroyWindow(g_prod_dlg);
    g_prod_dlg = NULL;
  }
  if (g_dlg != NULL && g_edit != NULL) {
    fill_text();
    SetFocus(g_edit);
  }
}

/* ====================================================================== */
/* Sell improvement picker                                                */
/* ====================================================================== */

#define MAX_SELL_ITEMS 128
static Impr_type_id sell_items[MAX_SELL_ITEMS];
static int sell_item_count = 0;
static HWND g_sell_dlg = NULL;

static void open_sell_picker(void);
static void close_sell_picker(void);

static LRESULT CALLBACK sr_sellpick_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_SELL_LIST, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"Sell",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_SELL_OK, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_SELL_CANCEL, hi, NULL);
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 8, btn_h = 28;

      GetClientRect(h, &rc);
      {
        HWND list = GetDlgItem(h, IDC_SELL_LIST);
        HWND ok = GetDlgItem(h, IDC_SELL_OK);
        HWND cancel = GetDlgItem(h, IDC_SELL_CANCEL);

        if (list) MoveWindow(list, pad, pad, rc.right - 2*pad,
                             rc.bottom - 3*pad - btn_h, TRUE);
        if (ok) MoveWindow(ok, pad, rc.bottom - pad - btn_h,
                           120, btn_h, TRUE);
        if (cancel) MoveWindow(cancel, pad + 128,
                               rc.bottom - pad - btn_h, 120, btn_h, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);

      if (id == IDC_SELL_OK && HIWORD(w) == BN_CLICKED) {
        HWND list = GetDlgItem(h, IDC_SELL_LIST);
        int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);

        if (sel >= 0 && sel < sell_item_count) {
          struct city *pcity = game_city_by_number(g_pcity_id);

          if (pcity != NULL) {
            struct impr_type *pimprove = improvement_by_number(
                sell_items[sel]);
            char msg[256];

            city_sell_improvement(pcity, sell_items[sel]);
            fc_snprintf(msg, sizeof(msg), _("Sold %s for %d gold."),
                        improvement_name_translation(pimprove),
                        impr_sell_gold(pimprove));
            sr_say(msg, 1);
          }
        }
        close_sell_picker();
        return 0;
      }
      if (id == IDC_SELL_CANCEL && HIWORD(w) == BN_CLICKED) {
        close_sell_picker();
        return 0;
      }
      if (id == IDC_SELL_LIST && HIWORD(w) == LBN_DBLCLK) {
        SendMessageW(h, WM_COMMAND, MAKEWPARAM(IDC_SELL_OK, BN_CLICKED), 0);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      close_sell_picker();
      return 0;
    case WM_DESTROY:
      g_sell_dlg = NULL;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, m, w, l);
}

static int ensure_sellpick_class(HINSTANCE hi)
{
  static int reg = 0;
  WNDCLASSEXW wc;

  if (reg) return 1;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = sr_sellpick_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_SELLPICK_CLASS_NAME;
  reg = RegisterClassExW(&wc) != 0
        || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return reg;
}

static void open_sell_picker(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND list;
  char label[256];

  if (pcity == NULL) { return; }
  if (g_sell_dlg != NULL) { SetForegroundWindow(g_sell_dlg); return; }
  if (!ensure_sellpick_class(hi)) { return; }

  g_sell_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_SELLPICK_CLASS_NAME, L"Sell Improvement",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
      NULL, NULL, hi, NULL);
  if (g_sell_dlg == NULL) { return; }
  sr_window_register_popup(g_sell_dlg);

  list = GetDlgItem(g_sell_dlg, IDC_SELL_LIST);
  sell_item_count = 0;

  improvement_iterate(pimprove) {
    if (city_has_building(pcity, pimprove)
        && test_player_sell_building_now(client.conn.playing, pcity,
                                         pimprove) == TR_SUCCESS
        && sell_item_count < MAX_SELL_ITEMS) {
      sell_items[sell_item_count] = improvement_number(pimprove);
      fc_snprintf(label, sizeof(label), "%s (%d gold)",
                  improvement_name_translation(pimprove),
                  impr_sell_gold(pimprove));
      {
        wchar_t wlabel[256];

        MultiByteToWideChar(CP_UTF8, 0, label, -1, wlabel, 256);
        SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)wlabel);
      }
      sell_item_count++;
    }
  } improvement_iterate_end;

  if (sell_item_count == 0) {
    sr_say(_("No improvements can be sold."), 1);
    sr_window_unregister_popup(g_sell_dlg);
    DestroyWindow(g_sell_dlg);
    g_sell_dlg = NULL;
    return;
  }

  SendMessageW(list, LB_SETCURSEL, 0, 0);
  ShowWindow(g_sell_dlg, SW_SHOWNORMAL);
  if (list) { SetFocus(list); }

  {
    char msg[128];

    fc_snprintf(msg, sizeof(msg), _("Choose improvement to sell. %d available."),
                sell_item_count);
    sr_say(msg, 1);
  }
}

static void close_sell_picker(void)
{
  if (g_sell_dlg != NULL) {
    sr_window_unregister_popup(g_sell_dlg);
    DestroyWindow(g_sell_dlg);
    g_sell_dlg = NULL;
  }
  if (g_dlg != NULL && g_edit != NULL) {
    fill_text();
    SetFocus(g_edit);
  }
}

/* ====================================================================== */
/* Buy production                                                         */
/* ====================================================================== */

static void do_buy_production(void)
{
  struct city *pcity = game_city_by_number(g_pcity_id);
  char buf[256];

  if (pcity == NULL) { return; }

  if (pcity->client.buy_cost <= 0) {
    sr_say(_("Nothing to buy."), 1);
    return;
  }
  if (client_player()->economic.gold < pcity->client.buy_cost) {
    fc_snprintf(buf, sizeof(buf),
                _("Cannot afford %s. Cost: %d gold. Treasury: %d gold."),
                production_name(pcity), pcity->client.buy_cost,
                client_player()->economic.gold);
    sr_say(buf, 1);
    return;
  }
  fc_snprintf(buf, sizeof(buf), _("Buying %s for %d gold."),
              production_name(pcity), pcity->client.buy_cost);
  sr_say(buf, 1);
  city_buy_production(pcity);
}

/* ====================================================================== */
/* Worklist editor                                                        */
/* ====================================================================== */

#define MAX_WL_SRC 512
static struct universal wl_src_items[MAX_WL_SRC];
static int wl_src_count = 0;
static struct worklist wl_edit;       /* Working copy, committed on OK */
static HWND g_wl_dlg = NULL;

static void open_worklist_editor(void);
static void close_worklist_editor(void);
static void wl_rebuild_dst_list(void);
static void wl_rebuild_src_list(void);

/* Format a universal item with its turns-to-build estimate. */
static void format_universal(const struct city *pcity,
                             const struct universal *u,
                             char *buf, size_t sz)
{
  const char *kind = "?";
  int turns = 0;

  if (u->kind == VUT_UTYPE && u->value.utype != NULL) {
    kind = "Unit";
    turns = city_turns_to_build(pcity, u, TRUE);
  } else if (u->kind == VUT_IMPROVEMENT && u->value.building != NULL) {
    kind = is_great_wonder(u->value.building) ? "Wonder"
         : is_small_wonder(u->value.building) ? "S.Wonder"
         : "Building";
    turns = city_turns_to_build(pcity, u, TRUE);
  }
  fc_snprintf(buf, sz, "[%s] %s (%d turns)", kind,
              sr_universal_name(u), turns);
}

static void lb_add_utf8(HWND list, const char *utf8)
{
  wchar_t w[256];

  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, 256);
  SendMessageW(list, LB_ADDSTRING, 0, (LPARAM)w);
}

static void wl_rebuild_dst_list(void)
{
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND list;
  int i, len;
  char label[256];
  char line[288];

  if (g_wl_dlg == NULL || pcity == NULL) { return; }
  list = GetDlgItem(g_wl_dlg, IDC_WL_DST);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);

  len = worklist_length(&wl_edit);
  for (i = 0; i < len; i++) {
    struct universal u;

    if (worklist_peek_ith(&wl_edit, &u, i)) {
      format_universal(pcity, &u, label, sizeof(label));
      fc_snprintf(line, sizeof(line), "%d. %s", i + 1, label);
      lb_add_utf8(list, line);
    }
  }
}

static void wl_rebuild_src_list(void)
{
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND list;
  char label[256];

  if (g_wl_dlg == NULL || pcity == NULL) { return; }
  list = GetDlgItem(g_wl_dlg, IDC_WL_SRC);
  SendMessageW(list, LB_RESETCONTENT, 0, 0);
  wl_src_count = 0;

  improvement_iterate(pimprove) {
    if (can_city_build_improvement_now(pcity, pimprove)
        && wl_src_count < MAX_WL_SRC) {
      wl_src_items[wl_src_count].kind = VUT_IMPROVEMENT;
      wl_src_items[wl_src_count].value.building = pimprove;
      format_universal(pcity, &wl_src_items[wl_src_count],
                       label, sizeof(label));
      lb_add_utf8(list, label);
      wl_src_count++;
    }
  } improvement_iterate_end;

  unit_type_iterate(punittype) {
    if (can_city_build_unit_now(&(wld.map), pcity, punittype)
        && wl_src_count < MAX_WL_SRC) {
      wl_src_items[wl_src_count].kind = VUT_UTYPE;
      wl_src_items[wl_src_count].value.utype = punittype;
      format_universal(pcity, &wl_src_items[wl_src_count],
                       label, sizeof(label));
      lb_add_utf8(list, label);
      wl_src_count++;
    }
  } unit_type_iterate_end;
}

static void wl_add_selected(void)
{
  HWND src = GetDlgItem(g_wl_dlg, IDC_WL_SRC);
  int sel = (int)SendMessageW(src, LB_GETCURSEL, 0, 0);

  if (sel < 0 || sel >= wl_src_count) { return; }
  if (worklist_length(&wl_edit) >= MAX_LEN_WORKLIST) {
    sr_say(_("Worklist full."), 1);
    return;
  }
  if (worklist_append(&wl_edit, &wl_src_items[sel])) {
    char msg[128];

    fc_snprintf(msg, sizeof(msg), _("Added %s. %d items queued."),
                sr_universal_name(&wl_src_items[sel]),
                worklist_length(&wl_edit));
    sr_say(msg, 1);
    wl_rebuild_dst_list();
    {
      HWND dst = GetDlgItem(g_wl_dlg, IDC_WL_DST);

      SendMessageW(dst, LB_SETCURSEL,
                   worklist_length(&wl_edit) - 1, 0);
    }
  }
}

static void wl_remove_selected(void)
{
  HWND dst = GetDlgItem(g_wl_dlg, IDC_WL_DST);
  int sel = (int)SendMessageW(dst, LB_GETCURSEL, 0, 0);
  int len = worklist_length(&wl_edit);

  if (sel < 0 || sel >= len) { return; }
  worklist_remove(&wl_edit, sel);
  wl_rebuild_dst_list();
  len = worklist_length(&wl_edit);
  if (len > 0) {
    SendMessageW(dst, LB_SETCURSEL, sel < len ? sel : len - 1, 0);
  }
  {
    char msg[64];

    fc_snprintf(msg, sizeof(msg), _("Removed. %d items queued."), len);
    sr_say(msg, 1);
  }
}

static void wl_move_selected(int delta)
{
  HWND dst = GetDlgItem(g_wl_dlg, IDC_WL_DST);
  int sel = (int)SendMessageW(dst, LB_GETCURSEL, 0, 0);
  int len = worklist_length(&wl_edit);
  int other = sel + delta;
  struct universal tmp;

  if (sel < 0 || sel >= len || other < 0 || other >= len) { return; }
  tmp = wl_edit.entries[sel];
  wl_edit.entries[sel] = wl_edit.entries[other];
  wl_edit.entries[other] = tmp;
  wl_rebuild_dst_list();
  SendMessageW(dst, LB_SETCURSEL, other, 0);
  {
    char msg[64];

    fc_snprintf(msg, sizeof(msg), _("Moved to position %d."), other + 1);
    sr_say(msg, 1);
  }
}

static void wl_clear_all(void)
{
  worklist_init(&wl_edit);
  wl_rebuild_dst_list();
  sr_say(_("Worklist cleared."), 1);
}

static void wl_commit_and_close(void)
{
  struct city *pcity = game_city_by_number(g_pcity_id);

  if (pcity != NULL) {
    char msg[128];

    city_set_worklist(pcity, &wl_edit);
    fc_snprintf(msg, sizeof(msg), _("Worklist saved. %d items."),
                worklist_length(&wl_edit));
    sr_say(msg, 1);
  }
  close_worklist_editor();
}

/* Speak currently selected entry in either list. */
static void wl_speak_selection(HWND list, int is_dst)
{
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  struct city *pcity = game_city_by_number(g_pcity_id);
  char buf[256];

  if (sel < 0 || pcity == NULL) { return; }
  if (is_dst) {
    struct universal u;

    if (worklist_peek_ith(&wl_edit, &u, sel)) {
      format_universal(pcity, &u, buf, sizeof(buf));
      sr_say(buf, 1);
    }
  } else {
    if (sel < wl_src_count) {
      format_universal(pcity, &wl_src_items[sel], buf, sizeof(buf));
      sr_say(buf, 1);
    }
  }
}

static LRESULT CALLBACK sr_wledit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      CreateWindowExW(0, L"STATIC", L"Current queue (Delete = remove, Ctrl+Up/Down = reorder):",
          WS_CHILD | WS_VISIBLE,
          0, 0, 0, 0, h, NULL, hi, NULL);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_DST, hi, NULL);
      CreateWindowExW(0, L"STATIC", L"Available (Enter = append):",
          WS_CHILD | WS_VISIBLE,
          0, 0, 0, 0, h, NULL, hi, NULL);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_SRC, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"&Add >>",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_ADD, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"&Remove",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_REMOVE, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"&Up",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_UP, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"&Down",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_DOWN, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"&Clear",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_CLEAR, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"OK",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_OK, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"Cancel",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_WL_CANCEL, hi, NULL);
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 8, btn_h = 28, lbl_h = 18;
      int col_w, list_h, row_btn, row_action;

      GetClientRect(h, &rc);
      col_w = (rc.right - 3 * pad) / 2;
      row_btn = rc.bottom - pad - btn_h;
      row_action = row_btn - pad - btn_h;
      list_h = row_action - pad - lbl_h - pad;

      {
        HWND dst = GetDlgItem(h, IDC_WL_DST);
        HWND src = GetDlgItem(h, IDC_WL_SRC);
        HWND add = GetDlgItem(h, IDC_WL_ADD);
        HWND rm  = GetDlgItem(h, IDC_WL_REMOVE);
        HWND up  = GetDlgItem(h, IDC_WL_UP);
        HWND dn  = GetDlgItem(h, IDC_WL_DOWN);
        HWND cl  = GetDlgItem(h, IDC_WL_CLEAR);
        HWND ok  = GetDlgItem(h, IDC_WL_OK);
        HWND ca  = GetDlgItem(h, IDC_WL_CANCEL);
        HWND lbl_dst, lbl_src;
        HWND c;

        /* The two STATICs are children #0 and #2 in creation order,
           but safer: find them by iterating and skipping known IDs. */
        lbl_dst = lbl_src = NULL;
        for (c = GetWindow(h, GW_CHILD); c != NULL;
             c = GetWindow(c, GW_HWNDNEXT)) {
          int id = GetDlgCtrlID(c);

          if (id == 0 || id == -1) {
            if (lbl_dst == NULL) { lbl_dst = c; }
            else if (lbl_src == NULL) { lbl_src = c; }
          }
        }
        if (lbl_dst) MoveWindow(lbl_dst, pad, pad, col_w, lbl_h, TRUE);
        if (lbl_src) MoveWindow(lbl_src, pad + col_w + pad, pad,
                                col_w, lbl_h, TRUE);
        if (dst) MoveWindow(dst, pad, pad + lbl_h + pad,
                            col_w, list_h, TRUE);
        if (src) MoveWindow(src, pad + col_w + pad, pad + lbl_h + pad,
                            col_w, list_h, TRUE);
        if (add) MoveWindow(add, pad, row_action, 100, btn_h, TRUE);
        if (rm)  MoveWindow(rm,  pad + 108, row_action, 100, btn_h, TRUE);
        if (up)  MoveWindow(up,  pad + 216, row_action, 60, btn_h, TRUE);
        if (dn)  MoveWindow(dn,  pad + 280, row_action, 60, btn_h, TRUE);
        if (cl)  MoveWindow(cl,  pad + 344, row_action, 80, btn_h, TRUE);
        if (ok) MoveWindow(ok, pad, row_btn, 120, btn_h, TRUE);
        if (ca) MoveWindow(ca, pad + 128, row_btn, 120, btn_h, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);
      int code = HIWORD(w);

      if (code == BN_CLICKED) {
        if (id == IDC_WL_ADD) { wl_add_selected(); return 0; }
        if (id == IDC_WL_REMOVE) { wl_remove_selected(); return 0; }
        if (id == IDC_WL_UP) { wl_move_selected(-1); return 0; }
        if (id == IDC_WL_DOWN) { wl_move_selected(+1); return 0; }
        if (id == IDC_WL_CLEAR) { wl_clear_all(); return 0; }
        if (id == IDC_WL_OK) { wl_commit_and_close(); return 0; }
        if (id == IDC_WL_CANCEL) { close_worklist_editor(); return 0; }
      }
      if (code == LBN_DBLCLK) {
        if (id == IDC_WL_SRC) { wl_add_selected(); return 0; }
        if (id == IDC_WL_DST) { wl_remove_selected(); return 0; }
      }
      if (code == LBN_SELCHANGE) {
        HWND list = (HWND)l;

        wl_speak_selection(list, id == IDC_WL_DST);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      close_worklist_editor();
      return 0;
    case WM_DESTROY:
      g_wl_dlg = NULL;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, m, w, l);
}

/* Listbox subclasses to catch Enter / Delete / Ctrl+Up / Ctrl+Down. */
static WNDPROC g_wl_dst_default = NULL;
static WNDPROC g_wl_src_default = NULL;

static LRESULT CALLBACK wl_dst_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    int ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;

    if (w == VK_DELETE) { wl_remove_selected(); return 0; }
    if (ctrl && w == VK_UP) { wl_move_selected(-1); return 0; }
    if (ctrl && w == VK_DOWN) { wl_move_selected(+1); return 0; }
    if (w == VK_ESCAPE) { close_worklist_editor(); return 0; }
  }
  return CallWindowProcW(g_wl_dst_default, h, m, w, l);
}

static LRESULT CALLBACK wl_src_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { wl_add_selected(); return 0; }
    if (w == VK_ESCAPE) { close_worklist_editor(); return 0; }
  }
  return CallWindowProcW(g_wl_src_default, h, m, w, l);
}

static int ensure_wledit_class(HINSTANCE hi)
{
  static int reg = 0;
  WNDCLASSEXW wc;

  if (reg) return 1;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = sr_wledit_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_WLEDIT_CLASS_NAME;
  reg = RegisterClassExW(&wc) != 0
        || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return reg;
}

static void open_worklist_editor(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND dst, src;

  if (pcity == NULL) { return; }
  if (g_wl_dlg != NULL) { SetForegroundWindow(g_wl_dlg); return; }
  if (!ensure_wledit_class(hi)) { return; }

  worklist_copy(&wl_edit, &pcity->worklist);

  g_wl_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_WLEDIT_CLASS_NAME, L"Edit Worklist",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 760, 500,
      NULL, NULL, hi, NULL);
  if (g_wl_dlg == NULL) { return; }
  sr_window_register_popup(g_wl_dlg);

  wl_rebuild_src_list();
  wl_rebuild_dst_list();

  dst = GetDlgItem(g_wl_dlg, IDC_WL_DST);
  src = GetDlgItem(g_wl_dlg, IDC_WL_SRC);
  g_wl_dst_default = (WNDPROC)SetWindowLongPtrW(
      dst, GWLP_WNDPROC, (LONG_PTR)wl_dst_proc);
  g_wl_src_default = (WNDPROC)SetWindowLongPtrW(
      src, GWLP_WNDPROC, (LONG_PTR)wl_src_proc);

  if (worklist_length(&wl_edit) > 0) {
    SendMessageW(dst, LB_SETCURSEL, 0, 0);
  }
  if (wl_src_count > 0) {
    SendMessageW(src, LB_SETCURSEL, 0, 0);
  }

  ShowWindow(g_wl_dlg, SW_SHOWNORMAL);
  SetFocus(src);

  {
    char msg[192];

    fc_snprintf(msg, sizeof(msg),
                _("Worklist editor for %s. %d items queued. "
                  "Tab to switch lists."),
                city_name_get(pcity), worklist_length(&wl_edit));
    sr_say(msg, 1);
  }
}

static void close_worklist_editor(void)
{
  if (g_wl_dlg != NULL) {
    sr_window_unregister_popup(g_wl_dlg);
    DestroyWindow(g_wl_dlg);
    g_wl_dlg = NULL;
  }
  /* Drop any stale pointers into ruleset memory. */
  worklist_init(&wl_edit);
  if (g_dlg != NULL && g_edit != NULL) {
    fill_text();
    SetFocus(g_edit);
  }
}

/* ====================================================================== */
/* Tile / citizen editor                                                  */
/* ====================================================================== */

#define MAX_TILE_ENTRIES 128
/* Parallel arrays for the two tile listboxes: city-relative x,y for each
   visible row. */
static int tile_worker_x[MAX_TILE_ENTRIES];
static int tile_worker_y[MAX_TILE_ENTRIES];
static int tile_worker_count = 0;
static int tile_idle_x[MAX_TILE_ENTRIES];
static int tile_idle_y[MAX_TILE_ENTRIES];
static int tile_idle_count = 0;

static HWND g_tile_dlg = NULL;

static void open_tile_editor(void);
static void close_tile_editor(void);
static void tile_editor_rebuild(void);

static void format_tile_line(const struct city *pcity, int dx, int dy,
                             const struct tile *ptile, char *buf, size_t sz)
{
  const char *extras;
  char base[192];
  int f, s, t;

  f = city_tile_output_now(pcity, ptile, O_FOOD);
  s = city_tile_output_now(pcity, ptile, O_SHIELD);
  t = city_tile_output_now(pcity, ptile, O_TRADE);

  extras = get_infrastructure_text(ptile->extras);
  if (extras != NULL && *extras != '\0') {
    fc_snprintf(base, sizeof(base), "(%+d,%+d) %s, %s  F%d/S%d/T%d",
                dx, dy,
                terrain_name_translation(tile_terrain(ptile)),
                extras, f, s, t);
  } else {
    fc_snprintf(base, sizeof(base), "(%+d,%+d) %s  F%d/S%d/T%d",
                dx, dy,
                terrain_name_translation(tile_terrain(ptile)),
                f, s, t);
  }
  fc_strlcpy(buf, base, sz);
}

static void tile_editor_rebuild(void)
{
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND w_list, i_list, s_list;
  int radius_sq;
  int w_sel, i_sel, s_sel;

  if (g_tile_dlg == NULL || pcity == NULL) { return; }

  w_list = GetDlgItem(g_tile_dlg, IDC_TILE_WORKERS);
  i_list = GetDlgItem(g_tile_dlg, IDC_TILE_IDLE);
  s_list = GetDlgItem(g_tile_dlg, IDC_TILE_SPECS);

  /* Preserve selection across rebuild. */
  w_sel = (int)SendMessageW(w_list, LB_GETCURSEL, 0, 0);
  i_sel = (int)SendMessageW(i_list, LB_GETCURSEL, 0, 0);
  s_sel = (int)SendMessageW(s_list, LB_GETCURSEL, 0, 0);

  SendMessageW(w_list, LB_RESETCONTENT, 0, 0);
  SendMessageW(i_list, LB_RESETCONTENT, 0, 0);
  SendMessageW(s_list, LB_RESETCONTENT, 0, 0);
  tile_worker_count = 0;
  tile_idle_count = 0;

  radius_sq = city_map_radius_sq_get(pcity);

  city_map_iterate(radius_sq, idx, cx, cy) {
    struct tile *ptile;
    int dx, dy;
    char line[256];

    if (is_free_worked_index(idx)) { continue; }
    ptile = city_map_to_tile(&(wld.map), city_tile(pcity), radius_sq,
                             cx, cy);
    if (ptile == NULL) { continue; }

    dx = CITY_ABS2REL(cx);
    dy = CITY_ABS2REL(cy);

    if (tile_worked(ptile) == pcity) {
      if (tile_worker_count < MAX_TILE_ENTRIES) {
        format_tile_line(pcity, dx, dy, ptile, line, sizeof(line));
        lb_add_utf8(w_list, line);
        tile_worker_x[tile_worker_count] = cx;
        tile_worker_y[tile_worker_count] = cy;
        tile_worker_count++;
      }
    } else if (city_can_work_tile(pcity, ptile)) {
      if (tile_idle_count < MAX_TILE_ENTRIES) {
        format_tile_line(pcity, dx, dy, ptile, line, sizeof(line));
        lb_add_utf8(i_list, line);
        tile_idle_x[tile_idle_count] = cx;
        tile_idle_y[tile_idle_count] = cy;
        tile_idle_count++;
      }
    }
  } city_map_iterate_end;

  specialist_type_iterate(sp) {
    char line[128];

    fc_snprintf(line, sizeof(line), "%s: %d",
                specialist_plural_translation(specialist_by_number(sp)),
                pcity->specialists[sp]);
    lb_add_utf8(s_list, line);
  } specialist_type_iterate_end;

  /* Restore selection where still valid. */
  if (w_sel >= 0 && w_sel < tile_worker_count) {
    SendMessageW(w_list, LB_SETCURSEL, w_sel, 0);
  } else if (tile_worker_count > 0) {
    SendMessageW(w_list, LB_SETCURSEL, 0, 0);
  }
  if (i_sel >= 0 && i_sel < tile_idle_count) {
    SendMessageW(i_list, LB_SETCURSEL, i_sel, 0);
  } else if (tile_idle_count > 0) {
    SendMessageW(i_list, LB_SETCURSEL, 0, 0);
  }
  if (s_sel >= 0 && s_sel < (int)specialist_count()) {
    SendMessageW(s_list, LB_SETCURSEL, s_sel, 0);
  } else {
    SendMessageW(s_list, LB_SETCURSEL, 0, 0);
  }
}

static void tile_toggle_worker_at(int cx, int cy)
{
  struct city *pcity = game_city_by_number(g_pcity_id);

  if (pcity == NULL) { return; }
  if (city_owner(pcity) != client_player()) {
    sr_say(_("Cannot edit foreign city."), 1);
    return;
  }
  city_toggle_worker(pcity, cx, cy);
  /* Server will reply with city_info → sr_citydlg_refresh() →
     tile_editor_rebuild(). */
}

static void tile_unwork_selected(void)
{
  HWND list = GetDlgItem(g_tile_dlg, IDC_TILE_WORKERS);
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);

  if (sel < 0 || sel >= tile_worker_count) { return; }
  tile_toggle_worker_at(tile_worker_x[sel], tile_worker_y[sel]);
}

static void tile_work_selected(void)
{
  HWND list = GetDlgItem(g_tile_dlg, IDC_TILE_IDLE);
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);

  if (sel < 0 || sel >= tile_idle_count) { return; }
  tile_toggle_worker_at(tile_idle_x[sel], tile_idle_y[sel]);
}

static void tile_cycle_specialist_selected(void)
{
  HWND list = GetDlgItem(g_tile_dlg, IDC_TILE_SPECS);
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  struct city *pcity = game_city_by_number(g_pcity_id);
  int count = (int)specialist_count();
  Specialist_type_id from, to;

  if (pcity == NULL || sel < 0 || sel >= count) { return; }
  if (city_owner(pcity) != client_player()) {
    sr_say(_("Cannot edit foreign city."), 1);
    return;
  }
  from = (Specialist_type_id)sel;
  if (pcity->specialists[from] <= 0) {
    sr_say(_("No specialists of that type."), 1);
    return;
  }
  to = (Specialist_type_id)((sel + 1) % count);
  city_change_specialist(pcity, from, to);
}

static void tile_speak_selection(HWND list, int which)
{
  int sel = (int)SendMessageW(list, LB_GETCURSEL, 0, 0);
  wchar_t wbuf[320];
  char buf[640];

  if (sel < 0) { return; }
  if (SendMessageW(list, LB_GETTEXT, sel, (LPARAM)wbuf) == LB_ERR) { return; }
  WideCharToMultiByte(CP_UTF8, 0, wbuf, -1, buf, sizeof(buf), NULL, NULL);
  (void)which;
  sr_say(buf, 1);
}

static LRESULT CALLBACK sr_tileedit_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  switch (m) {
    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);

      CreateWindowExW(0, L"STATIC",
          L"Worked tiles (Enter = unwork):",
          WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, NULL, hi, NULL);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_TILE_WORKERS, hi, NULL);
      CreateWindowExW(0, L"STATIC",
          L"Idle tiles in radius (Enter = work):",
          WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, NULL, hi, NULL);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_TILE_IDLE, hi, NULL);
      CreateWindowExW(0, L"STATIC",
          L"Specialists (Enter = cycle role):",
          WS_CHILD | WS_VISIBLE, 0, 0, 0, 0, h, NULL, hi, NULL);
      CreateWindowExW(WS_EX_CLIENTEDGE, L"LISTBOX", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | LBS_NOINTEGRALHEIGHT | LBS_HASSTRINGS | LBS_NOTIFY,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_TILE_SPECS, hi, NULL);
      CreateWindowExW(0, L"BUTTON", L"Close",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_DEFPUSHBUTTON,
          0, 0, 0, 0, h, (HMENU)(LONG_PTR)IDC_TILE_CLOSE, hi, NULL);
      return 0;
    }
    case WM_SIZE: {
      RECT rc;
      int pad = 8, btn_h = 28, lbl_h = 18;
      int col_w, top_h, bot_h, row_btn, row_spec_label, row_spec_list;
      HWND c;
      HWND lbls[3] = { NULL, NULL, NULL };
      int li = 0;

      GetClientRect(h, &rc);
      col_w = (rc.right - 3 * pad) / 2;
      row_btn = rc.bottom - pad - btn_h;
      /* Bottom third: specialist label + list + close button. */
      bot_h = 140;
      row_spec_list = row_btn - pad - bot_h + lbl_h + pad;
      row_spec_label = row_btn - pad - bot_h;
      top_h = row_spec_label - pad - lbl_h - pad - pad;

      for (c = GetWindow(h, GW_CHILD); c != NULL && li < 3;
           c = GetWindow(c, GW_HWNDNEXT)) {
        int id = GetDlgCtrlID(c);

        if (id == 0 || id == -1) {
          lbls[li++] = c;
        }
      }
      {
        HWND wl = GetDlgItem(h, IDC_TILE_WORKERS);
        HWND il = GetDlgItem(h, IDC_TILE_IDLE);
        HWND sl = GetDlgItem(h, IDC_TILE_SPECS);
        HWND cb = GetDlgItem(h, IDC_TILE_CLOSE);

        if (lbls[0]) MoveWindow(lbls[0], pad, pad, col_w, lbl_h, TRUE);
        if (lbls[1]) MoveWindow(lbls[1], pad + col_w + pad, pad,
                                col_w, lbl_h, TRUE);
        if (wl) MoveWindow(wl, pad, pad + lbl_h + pad,
                           col_w, top_h, TRUE);
        if (il) MoveWindow(il, pad + col_w + pad, pad + lbl_h + pad,
                           col_w, top_h, TRUE);
        if (lbls[2]) MoveWindow(lbls[2], pad, row_spec_label,
                                rc.right - 2 * pad, lbl_h, TRUE);
        if (sl) MoveWindow(sl, pad, row_spec_list,
                           rc.right - 2 * pad, bot_h - lbl_h - pad, TRUE);
        if (cb) MoveWindow(cb, pad, row_btn, 120, btn_h, TRUE);
      }
      return 0;
    }
    case WM_COMMAND: {
      int id = LOWORD(w);
      int code = HIWORD(w);

      if (code == BN_CLICKED && id == IDC_TILE_CLOSE) {
        close_tile_editor();
        return 0;
      }
      if (code == LBN_DBLCLK) {
        if (id == IDC_TILE_WORKERS) { tile_unwork_selected(); return 0; }
        if (id == IDC_TILE_IDLE) { tile_work_selected(); return 0; }
        if (id == IDC_TILE_SPECS) {
          tile_cycle_specialist_selected();
          return 0;
        }
      }
      if (code == LBN_SELCHANGE) {
        HWND list = (HWND)l;

        tile_speak_selection(list, id);
        return 0;
      }
      break;
    }
    case WM_CLOSE:
      close_tile_editor();
      return 0;
    case WM_DESTROY:
      g_tile_dlg = NULL;
      return 0;
    default:
      break;
  }
  return DefWindowProcW(h, m, w, l);
}

/* Listbox subclasses — catch Enter / Escape. */
static WNDPROC g_tile_w_default = NULL;
static WNDPROC g_tile_i_default = NULL;
static WNDPROC g_tile_s_default = NULL;

static LRESULT CALLBACK tile_worker_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { tile_unwork_selected(); return 0; }
    if (w == VK_ESCAPE) { close_tile_editor(); return 0; }
  }
  return CallWindowProcW(g_tile_w_default, h, m, w, l);
}

static LRESULT CALLBACK tile_idle_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { tile_work_selected(); return 0; }
    if (w == VK_ESCAPE) { close_tile_editor(); return 0; }
  }
  return CallWindowProcW(g_tile_i_default, h, m, w, l);
}

static LRESULT CALLBACK tile_spec_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
  if (m == WM_GETDLGCODE) {
    return DLGC_WANTALLKEYS | DLGC_WANTCHARS | DLGC_WANTARROWS;
  }
  if (m == WM_KEYDOWN) {
    if (w == VK_RETURN) { tile_cycle_specialist_selected(); return 0; }
    if (w == VK_ESCAPE) { close_tile_editor(); return 0; }
  }
  return CallWindowProcW(g_tile_s_default, h, m, w, l);
}

static int ensure_tileedit_class(HINSTANCE hi)
{
  static int reg = 0;
  WNDCLASSEXW wc;

  if (reg) return 1;
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize = sizeof(wc);
  wc.lpfnWndProc = sr_tileedit_proc;
  wc.hInstance = hi;
  wc.hCursor = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_TILEEDIT_CLASS_NAME;
  reg = RegisterClassExW(&wc) != 0
        || GetLastError() == ERROR_CLASS_ALREADY_EXISTS;
  return reg;
}

static void open_tile_editor(void)
{
  HINSTANCE hi = GetModuleHandleW(NULL);
  struct city *pcity = game_city_by_number(g_pcity_id);
  HWND wl, il, sl;

  if (pcity == NULL) { return; }
  if (g_tile_dlg != NULL) { SetForegroundWindow(g_tile_dlg); return; }
  if (!ensure_tileedit_class(hi)) { return; }

  g_tile_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME,
      SR_TILEEDIT_CLASS_NAME, L"City Tiles",
      WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME,
      CW_USEDEFAULT, CW_USEDEFAULT, 820, 600,
      NULL, NULL, hi, NULL);
  if (g_tile_dlg == NULL) { return; }
  sr_window_register_popup(g_tile_dlg);

  tile_editor_rebuild();

  wl = GetDlgItem(g_tile_dlg, IDC_TILE_WORKERS);
  il = GetDlgItem(g_tile_dlg, IDC_TILE_IDLE);
  sl = GetDlgItem(g_tile_dlg, IDC_TILE_SPECS);
  g_tile_w_default = (WNDPROC)SetWindowLongPtrW(
      wl, GWLP_WNDPROC, (LONG_PTR)tile_worker_proc);
  g_tile_i_default = (WNDPROC)SetWindowLongPtrW(
      il, GWLP_WNDPROC, (LONG_PTR)tile_idle_proc);
  g_tile_s_default = (WNDPROC)SetWindowLongPtrW(
      sl, GWLP_WNDPROC, (LONG_PTR)tile_spec_proc);

  ShowWindow(g_tile_dlg, SW_SHOWNORMAL);
  SetFocus(wl);

  {
    char msg[192];
    int total_specs = 0;

    specialist_type_iterate(sp) {
      total_specs += pcity->specialists[sp];
    } specialist_type_iterate_end;
    fc_snprintf(msg, sizeof(msg),
                _("City tiles for %s. %d workers, %d idle, %d specialists. "
                  "Tab between lists."),
                city_name_get(pcity), tile_worker_count, tile_idle_count,
                total_specs);
    sr_say(msg, 1);
  }
}

static void close_tile_editor(void)
{
  if (g_tile_dlg != NULL) {
    sr_window_unregister_popup(g_tile_dlg);
    DestroyWindow(g_tile_dlg);
    g_tile_dlg = NULL;
  }
  if (g_dlg != NULL && g_edit != NULL) {
    fill_text();
    SetFocus(g_edit);
  }
}

/* ====================================================================== */
/* Window plumbing                                                        */
/* ====================================================================== */

static void fill_text(void)
{
  struct player *pl = client_player();
  struct city *pcity;
  const char *utf8;
  int wlen;
  wchar_t *w;

  if (g_dlg == NULL || g_edit == NULL || pl == NULL) {
    return;
  }
  pcity = player_city_by_number(pl, g_pcity_id);
  if (pcity == NULL) {
    pcity = game_city_by_number(g_pcity_id);
  }
  if (pcity == NULL) {
    SetWindowTextW(g_edit, L"(city no longer available)");
    return;
  }

  utf8 = compose_city_text(pcity);
  if (utf8 == NULL) { return; }

  wlen = MultiByteToWideChar(CP_UTF8, 0, utf8, -1, NULL, 0);
  if (wlen <= 0) { return; }
  w = (wchar_t *)malloc(sizeof(wchar_t) * (size_t)wlen);
  if (w == NULL) { return; }
  MultiByteToWideChar(CP_UTF8, 0, utf8, -1, w, wlen);
  SetWindowTextW(g_edit, w);
  free(w);

  /* Update the title bar. */
  {
    char title[128];
    wchar_t wtitle[128];

    fc_snprintf(title, sizeof(title), "City: %s", city_name_get(pcity));
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wtitle,
                        sizeof(wtitle) / sizeof(wtitle[0]));
    SetWindowTextW(g_dlg, wtitle);
  }
}

static LRESULT CALLBACK sr_citydlg_wnd_proc(HWND hWnd, UINT msg,
                                            WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

    case WM_CREATE: {
      HINSTANCE hi = GetModuleHandleW(NULL);
      int btn_w = 140, btn_h = 28, pad = 6;

      g_edit = CreateWindowExW(
          WS_EX_CLIENTEDGE,
          L"EDIT", L"",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
          | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
          0, 0, 0, 0,
          hWnd, (HMENU)(LONG_PTR)IDC_CITY_EDIT, hi, NULL);
      g_btn_prod = CreateWindowExW(
          0, L"BUTTON", L"&Production (P)",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          pad, 0, btn_w, btn_h,
          hWnd, (HMENU)(LONG_PTR)IDC_BTN_PRODUCTION, hi, NULL);
      g_btn_buy = CreateWindowExW(
          0, L"BUTTON", L"&Buy (B)",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          pad + btn_w + pad, 0, btn_w, btn_h,
          hWnd, (HMENU)(LONG_PTR)IDC_BTN_BUY, hi, NULL);
      g_btn_sell = CreateWindowExW(
          0, L"BUTTON", L"&Sell (S)",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          pad + 2 * (btn_w + pad), 0, btn_w, btn_h,
          hWnd, (HMENU)(LONG_PTR)IDC_BTN_SELL, hi, NULL);
      g_btn_wl = CreateWindowExW(
          0, L"BUTTON", L"&Worklist (W)",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          pad + 3 * (btn_w + pad), 0, btn_w, btn_h,
          hWnd, (HMENU)(LONG_PTR)IDC_BTN_WORKLIST, hi, NULL);
      g_btn_tiles = CreateWindowExW(
          0, L"BUTTON", L"&Tiles (T)",
          WS_CHILD | WS_VISIBLE | WS_TABSTOP,
          pad + 4 * (btn_w + pad), 0, btn_w, btn_h,
          hWnd, (HMENU)(LONG_PTR)IDC_BTN_TILES, hi, NULL);
      return 0;
    }

    case WM_SIZE: {
      RECT rc;
      int pad = 6, btn_h = 28;

      GetClientRect(hWnd, &rc);
      /* Buttons at top */
      if (g_btn_prod) MoveWindow(g_btn_prod, pad, pad, 140, btn_h, TRUE);
      if (g_btn_buy) MoveWindow(g_btn_buy, pad + 146, pad, 140, btn_h, TRUE);
      if (g_btn_sell) MoveWindow(g_btn_sell, pad + 292, pad, 140, btn_h, TRUE);
      if (g_btn_wl) MoveWindow(g_btn_wl, pad + 438, pad, 140, btn_h, TRUE);
      if (g_btn_tiles) MoveWindow(g_btn_tiles, pad + 584, pad, 140, btn_h, TRUE);
      /* Edit fills rest */
      if (g_edit) {
        MoveWindow(g_edit, pad, pad + btn_h + pad,
                   rc.right - 2 * pad,
                   rc.bottom - 3 * pad - btn_h, TRUE);
      }
      return 0;
    }

    case WM_COMMAND: {
      int id = LOWORD(wParam);

      if (id == IDC_BTN_PRODUCTION && HIWORD(wParam) == BN_CLICKED) {
        open_production_picker();
        return 0;
      }
      if (id == IDC_BTN_BUY && HIWORD(wParam) == BN_CLICKED) {
        do_buy_production();
        fill_text();
        return 0;
      }
      if (id == IDC_BTN_SELL && HIWORD(wParam) == BN_CLICKED) {
        open_sell_picker();
        return 0;
      }
      if (id == IDC_BTN_WORKLIST && HIWORD(wParam) == BN_CLICKED) {
        open_worklist_editor();
        return 0;
      }
      if (id == IDC_BTN_TILES && HIWORD(wParam) == BN_CLICKED) {
        open_tile_editor();
        return 0;
      }
      break;
    }

    case WM_KEYDOWN:
      if (wParam == VK_ESCAPE) {
        sr_citydlg_close();
        return 0;
      }
      break;

    case WM_CLOSE:
      sr_citydlg_close();
      return 0;

    case WM_DESTROY:
      g_dlg = NULL;
      g_edit = NULL;
      g_btn_prod = NULL;
      g_btn_buy = NULL;
      g_btn_sell = NULL;
      g_btn_wl = NULL;
      g_btn_tiles = NULL;
      g_pcity_id = 0;
      return 0;

    default:
      break;
  }
  return DefWindowProcW(hWnd, msg, wParam, lParam);
}

/* EDIT subclass to catch key shortcuts. */
static WNDPROC g_edit_default_proc = NULL;
static LRESULT CALLBACK citydlg_edit_proc(HWND hWnd, UINT msg,
                                          WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_KEYDOWN) {
    switch (wParam) {
      case VK_ESCAPE:
        sr_citydlg_close();
        return 0;
      case 'P':
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
          open_production_picker();
          return 0;
        }
        break;
      case 'B':
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
          do_buy_production();
          fill_text();
          return 0;
        }
        break;
      case 'S':
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
          open_sell_picker();
          return 0;
        }
        break;
      case 'W':
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
          open_worklist_editor();
          return 0;
        }
        break;
      case 'T':
        if (!(GetKeyState(VK_CONTROL) & 0x8000)) {
          open_tile_editor();
          return 0;
        }
        break;
    }
  }
  return CallWindowProcW(g_edit_default_proc, hWnd, msg, wParam, lParam);
}

static int ensure_class_registered(HINSTANCE hi)
{
  static int registered = 0;
  WNDCLASSEXW wc;

  if (registered) { return 1; }
  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = sr_citydlg_wnd_proc;
  wc.hInstance     = hi;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_CITYDLG_CLASS_NAME;

  if (RegisterClassExW(&wc) != 0
      || GetLastError() == ERROR_CLASS_ALREADY_EXISTS) {
    registered = 1;
    return 1;
  }
  return 0;
}

/* ====================================================================== */
/* Public                                                                 */
/* ====================================================================== */

void sr_citydlg_open(struct city *pcity)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  if (pcity == NULL) { return; }

  if (g_dlg != NULL) {
    g_pcity_id = pcity->id;
    fill_text();
    SetForegroundWindow(g_dlg);
    if (g_edit != NULL) { SetFocus(g_edit); }
    return;
  }

  if (!ensure_class_registered(hi)) { return; }

  g_pcity_id = pcity->id;

  g_dlg = CreateWindowExW(
      WS_EX_DLGMODALFRAME | WS_EX_APPWINDOW,
      SR_CITYDLG_CLASS_NAME,
      L"City",
      WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_THICKFRAME
      | WS_MINIMIZEBOX,
      CW_USEDEFAULT, CW_USEDEFAULT, 640, 540,
      NULL, NULL, hi, NULL);
  if (g_dlg == NULL) {
    g_pcity_id = 0;
    return;
  }

  /* Subclass the EDIT for keyboard shortcuts. */
  if (g_edit != NULL) {
    g_edit_default_proc = (WNDPROC)SetWindowLongPtrW(
        g_edit, GWLP_WNDPROC, (LONG_PTR)citydlg_edit_proc);
  }

  fill_text();

  ShowWindow(g_dlg, SW_SHOWNORMAL);
  UpdateWindow(g_dlg);
  if (g_edit != NULL) { SetFocus(g_edit); }

  {
    struct city *pc = game_city_by_number(g_pcity_id);
    char msg[256];

    if (pc != NULL) {
      fc_snprintf(msg, sizeof(msg),
                  _("%s, size %d. P for production, B for buy, S for sell."),
                  city_name_get(pc), city_size_get(pc));
      sr_say(msg, 1);
    }
  }
}

void sr_citydlg_close(void)
{
  HWND parent_play;

  close_production_picker();
  close_sell_picker();
  close_worklist_editor();
  close_tile_editor();

  if (g_dlg != NULL) {
    DestroyWindow(g_dlg);
  }
  parent_play = sr_play_hwnd();
  if (parent_play != NULL) {
    SetFocus(parent_play);
  }
}

void sr_citydlg_refresh(void)
{
  fill_text();
  tile_editor_rebuild();
}

void sr_citydlg_refresh_worklist(void)
{
  /* External change — refresh the city dialog text. Leave any in-progress
     editor alone so the user's pending edits aren't stomped. */
  fill_text();
}

int sr_citydlg_is_open(void)
{
  return g_dlg != NULL;
}

/* Drop all module-static state and destroy any surviving windows before
   the engine tears down ruleset data on save/quit. Called from
   gui_ui_exit() in gui_main.c. */
void sr_citydlg_shutdown(void)
{
  close_production_picker();
  close_sell_picker();
  close_worklist_editor();
  close_tile_editor();
  if (g_dlg != NULL) {
    DestroyWindow(g_dlg);
    g_dlg = NULL;
  }
  g_edit = g_btn_prod = g_btn_buy = g_btn_sell = NULL;
  g_btn_wl = g_btn_tiles = NULL;
  g_pcity_id = 0;
  worklist_init(&wl_edit);
  prod_item_count = 0;
  sell_item_count = 0;
  wl_src_count = 0;
  tile_worker_count = 0;
  tile_idle_count = 0;
}

#endif  /* _WIN32 */
