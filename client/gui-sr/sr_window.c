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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>
#include <commctrl.h>
#include <winsock2.h>

/* client */
#include "client_main.h"
#include "clinet.h"

/* common */
#include "city.h"
#include "player.h"

/* gui-sr */
#include "gui_main.h"           /* sr_drain_idle */
#include "sr_citydlg.h"
#include "sr_citylist.h"
#include "sr_cmd.h"
#include "sr_diplodlg.h"
#include "sr_economydlg.h"
#include "sr_inteldlg.h"
#include "dialogs.h"            /* popup_races_dialog */
#include "sr_loadgame.h"
#include "sr_newgame.h"
#include "sr_optdlg.h"
#include "sr_sciencedlg.h"
#include "sr_unitsdlg.h"
#include "sr_log.h"
#include "sr_notify.h"
#include "sr_play.h"
#include "sr_sidebar.h"
#include "sr_speech.h"
#include "sr_status.h"
#include "sr_tree.h"

#include "sr_window.h"

#define SR_CLASS_NAME     L"FreecivSRMainWindow"
#define SR_WINDOW_TITLE   L"Freeciv (Screen Reader)"

#define SR_TIMER_ID       1
#define SR_TIMER_PERIOD_MS 100

/* Child-control identifiers (not the same as WM_COMMAND ids). */
#define IDC_LOG           1002
#define IDC_CMD           1003
#define IDC_CMD_GO        1004
#define IDC_STATUS        1005

/* Window-state globals. Single-window app; module globals are fine. */
static HWND g_hwnd        = NULL;
static HWND g_log         = NULL;
static HWND g_cmd         = NULL;
static HWND g_cmd_go      = NULL;
static HWND g_status      = NULL;
static HACCEL g_accel     = NULL;
static HMENU g_menu       = NULL;
static DWORD g_ui_thread  = 0;
static int   g_net_sock   = -1;
static WNDPROC g_cmd_default_proc = NULL;
static int   g_cmd_visible = 0;    /* command EDIT revealed? Ctrl+Shift+C toggles */

/* Forward decls. */
static LRESULT CALLBACK sr_wnd_proc(HWND hWnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam);
static LRESULT CALLBACK sr_cmd_edit_proc(HWND hWnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam);
static void sr_layout_children(HWND hWnd);
static HMENU sr_build_menu(void);
static HACCEL sr_build_accelerators(void);
static void sr_create_children(HWND hWnd);
static void sr_dispatch_command_id(int id);
static void sr_submit_command_edit(void);

/* ====================================================================== */
/* Public API                                                             */
/* ====================================================================== */

HWND sr_window_hwnd(void)
{
  return g_hwnd;
}

HWND sr_window_tree_hwnd(void)
{
  /* Retained for compatibility with sr_tree.c; no primary tree now. */
  return NULL;
}

DWORD sr_window_ui_thread_id(void)
{
  return g_ui_thread;
}

void sr_window_set_net_socket(int sock)
{
  g_net_sock = sock;
  if (g_hwnd != NULL) {
    if (sock >= 0) {
      /* Ask Windows to post WM_SR_NET whenever the socket is readable
         or closed. Replaces the select() loop. */
      WSAAsyncSelect((SOCKET)sock, g_hwnd, WM_SR_NET, FD_READ | FD_CLOSE);
    } else {
      /* Nothing to cancel: when the old socket is closed, Winsock
         stops posting automatically. */
    }
  }
}

int sr_window_create(void)
{
  INITCOMMONCONTROLSEX icc;
  HINSTANCE hInstance = GetModuleHandleW(NULL);
  WNDCLASSEXW wc;
  RECT desired;

  if (g_hwnd != NULL) {
    return 1;
  }

  g_ui_thread = GetCurrentThreadId();

  /* Enable modern common controls. The manifest makes v6 the default. */
  icc.dwSize = sizeof(icc);
  icc.dwICC  = ICC_TREEVIEW_CLASSES | ICC_STANDARD_CLASSES | ICC_BAR_CLASSES;
  InitCommonControlsEx(&icc);

  ZeroMemory(&wc, sizeof(wc));
  wc.cbSize        = sizeof(wc);
  wc.style         = CS_HREDRAW | CS_VREDRAW;
  wc.lpfnWndProc   = sr_wnd_proc;
  wc.hInstance     = hInstance;
  wc.hCursor       = LoadCursor(NULL, IDC_ARROW);
  wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
  wc.lpszClassName = SR_CLASS_NAME;
  wc.hIcon         = LoadIconW(NULL, IDI_APPLICATION);
  wc.hIconSm       = LoadIconW(NULL, IDI_APPLICATION);

  if (!RegisterClassExW(&wc)) {
    return 0;
  }

  g_menu = sr_build_menu();
  g_accel = sr_build_accelerators();

  /* Size and position. Centered-ish on primary monitor. */
  desired.left   = 80;
  desired.top    = 60;
  desired.right  = desired.left + 900;
  desired.bottom = desired.top + 720;

  g_hwnd = CreateWindowExW(
      0,
      SR_CLASS_NAME,
      SR_WINDOW_TITLE,
      WS_OVERLAPPEDWINDOW,
      desired.left, desired.top,
      desired.right - desired.left, desired.bottom - desired.top,
      NULL, g_menu, hInstance, NULL);

  if (g_hwnd == NULL) {
    return 0;
  }

  /* The Map View is its own window class — register it before child
     creation so CreateWindowEx can find it. */
  sr_play_register_class(hInstance);

  sr_create_children(g_hwnd);
  sr_log_attach(g_log);
  sr_status_attach(g_status);

  SetTimer(g_hwnd, SR_TIMER_ID, SR_TIMER_PERIOD_MS, NULL);

  ShowWindow(g_hwnd, SW_SHOWNORMAL);
  UpdateWindow(g_hwnd);

  /* Initial focus goes to the Map View — the primary gameplay surface.
     Same convention as gui-gtk-4.0's map_canvas and gui-qt's map_view. */
  if (sr_play_hwnd() != NULL) {
    SetFocus(sr_play_hwnd());
  }

  /* First render of the dynamic surfaces so NVDA has something to read. */
  sr_sidebar_update_game();
  sr_notify_recompute_waiting();
  sr_status_update();

  return 1;
}

void sr_window_destroy(void)
{
  if (g_hwnd != NULL) {
    KillTimer(g_hwnd, SR_TIMER_ID);
    DestroyWindow(g_hwnd);
    g_hwnd = NULL;
    g_log = g_cmd = g_cmd_go = g_status = NULL;
  }
  if (g_accel != NULL) {
    DestroyAcceleratorTable(g_accel);
    g_accel = NULL;
  }
  /* g_menu is owned by the window and freed when the window goes away. */
  g_menu = NULL;
}

/* Active modeless popup that should receive dialog-style key processing
   (Tab/Shift-Tab between controls, Enter on default button, Escape).
   Registered by sr_newgame_open / sr_loadgame_open and similar. */
#define SR_MAX_POPUPS 4
static HWND g_popups[SR_MAX_POPUPS];

void sr_window_register_popup(HWND h)
{
  int i;

  if (h == NULL) { return; }
  for (i = 0; i < SR_MAX_POPUPS; i++) {
    if (g_popups[i] == NULL) { g_popups[i] = h; return; }
  }
}

void sr_window_unregister_popup(HWND h)
{
  int i;

  for (i = 0; i < SR_MAX_POPUPS; i++) {
    if (g_popups[i] == h) { g_popups[i] = NULL; }
  }
}

int sr_window_run_message_loop(void)
{
  MSG msg;

  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    int handled = 0;
    int i;

    /* Drain the idle-callback queue on every message tick. The core
       client's update_queue.c enqueues per-packet UI updates (page
       transitions, focus changes, etc.) via add_idle_callback; without
       this drain they sit until process exit, so PAGE_START /
       PAGE_GAME callbacks never fire mid-session. */
    sr_drain_idle();

    if (is_client_quitting()) {
      PostQuitMessage(0);
      continue;
    }

    /* Let any registered popup handle Tab / arrow / default-button first. */
    for (i = 0; i < SR_MAX_POPUPS; i++) {
      HWND p = g_popups[i];
      if (p != NULL && IsWindow(p)
          && (msg.hwnd == p || IsChild(p, msg.hwnd))) {
        if (IsDialogMessageW(p, &msg)) { handled = 1; }
        break;
      }
    }
    if (handled) { continue; }

    if (g_accel != NULL && TranslateAcceleratorW(g_hwnd, g_accel, &msg)) {
      continue;
    }
    /* Skip IsDialogMessageW when the map pane has focus — it eats
       letter keys (as mnemonic searches) and arrow keys (as dialog
       navigation) that the play pane needs for game commands. */
    if (msg.message >= WM_KEYFIRST && msg.message <= WM_KEYLAST
        && sr_play_hwnd() != NULL && GetFocus() == sr_play_hwnd()) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      continue;
    }
    /* Also skip IsDialogMessageW for Escape: with focus on a child
       control of g_hwnd (which has WS_SYSMENU), IsDialogMessageW
       turns Escape into WM_CLOSE on the main window — i.e. it quits
       the game. Esc should just be consumed by whatever control has
       focus (default EDIT/TreeView ignore it, our subclassed pickers
       handle it themselves before this point). */
    if ((msg.message == WM_KEYDOWN || msg.message == WM_SYSKEYDOWN)
        && msg.wParam == VK_ESCAPE) {
      TranslateMessage(&msg);
      DispatchMessageW(&msg);
      continue;
    }
    if (IsDialogMessageW(g_hwnd, &msg)) {
      continue;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  return (int)msg.wParam;
}

/* ====================================================================== */
/* Child-control creation and layout                                      */
/* ====================================================================== */

static void sr_create_children(HWND hWnd)
{
  HINSTANCE hi = GetModuleHandleW(NULL);

  /* Sidebar (left column): labeled list of always-visible indicators.
     Own window created by sr_sidebar; sr_window owns layout. */
  sr_sidebar_create(hWnd);

  /* Map View: primary gameplay surface. Custom class, default focus. */
  sr_play_create(hWnd);

  /* Notifications: flat list of actionable items. */
  sr_notify_create(hWnd);

  /* Event log. Read-only multiline EDIT. */
  g_log = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      L"EDIT", L"",
      WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL
      | ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL,
      0, 0, 0, 0,
      hWnd, (HMENU)(LONG_PTR)IDC_LOG, hi, NULL);

  /* Command EDIT: hidden by default. Revealed by Ctrl+Shift+C. */
  g_cmd = CreateWindowExW(
      WS_EX_CLIENTEDGE,
      L"EDIT", L"",
      WS_CHILD | WS_TABSTOP | ES_AUTOHSCROLL,   /* no WS_VISIBLE */
      0, 0, 0, 0,
      hWnd, (HMENU)(LONG_PTR)IDC_CMD, hi, NULL);

  g_cmd_go = CreateWindowExW(
      0,
      L"BUTTON", L"Go",
      WS_CHILD | WS_TABSTOP | BS_DEFPUSHBUTTON,  /* no WS_VISIBLE */
      0, 0, 0, 0,
      hWnd, (HMENU)(LONG_PTR)IDC_CMD_GO, hi, NULL);

  g_status = CreateWindowExW(
      0,
      STATUSCLASSNAMEW, L"",
      WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
      0, 0, 0, 0,
      hWnd, (HMENU)(LONG_PTR)IDC_STATUS, hi, NULL);

  /* Intercept Enter inside the command EDIT so we can submit. A
     subclassed WndProc is simpler than rigging an accelerator that
     only fires when the EDIT has focus. */
  g_cmd_default_proc = (WNDPROC)SetWindowLongPtrW(
      g_cmd, GWLP_WNDPROC, (LONG_PTR)sr_cmd_edit_proc);

  sr_layout_children(hWnd);

  /* sr_tree's section creator is a safety-net call for code paths that
     may still reference it; the primary navigation is now sidebar +
     Map View + notifications. */
  sr_tree_create_sections();
}

static void sr_layout_children(HWND hWnd)
{
  RECT rc;
  RECT status_rect;
  int client_w, client_h;
  int status_h = 0;
  int pad = 4;
  int sidebar_w = 300;     /* left-column indicators */
  int bottom_row_h = 180;  /* Notifications + Event log */
  int notify_w;
  int cmd_row_h = 28, go_w = 80;
  int cmd_row_present = g_cmd_visible ? cmd_row_h + pad : 0;
  int top_h;

  HWND hplay = sr_play_hwnd();
  HWND hside = sr_sidebar_hwnd();
  HWND hnotify = sr_notify_hwnd();

  if (g_status == NULL) {
    return;
  }

  /* Let the status bar auto-resize to the bottom, then compute the
     available area above it. */
  SendMessageW(g_status, WM_SIZE, 0, 0);
  if (GetWindowRect(g_status, &status_rect)) {
    status_h = status_rect.bottom - status_rect.top;
  }

  GetClientRect(hWnd, &rc);
  client_w = rc.right - rc.left;
  client_h = rc.bottom - rc.top - status_h;
  if (client_h < 120 || client_w < 200) {
    return;
  }

  top_h = client_h - bottom_row_h - cmd_row_present - 2 * pad;
  if (top_h < 100) {
    top_h = 100;
  }
  notify_w = (client_w - 3 * pad) * 40 / 100;

  if (hside != NULL) {
    MoveWindow(hside, pad, pad, sidebar_w, top_h, TRUE);
  }
  if (hplay != NULL) {
    MoveWindow(hplay, pad + sidebar_w + pad, pad,
               client_w - sidebar_w - 3 * pad, top_h, TRUE);
  }
  if (hnotify != NULL) {
    MoveWindow(hnotify, pad, pad + top_h + pad,
               notify_w, bottom_row_h, TRUE);
  }
  MoveWindow(g_log,
             pad + notify_w + pad, pad + top_h + pad,
             client_w - notify_w - 3 * pad, bottom_row_h, TRUE);

  if (g_cmd_visible) {
    int y = pad + top_h + pad + bottom_row_h + pad;

    MoveWindow(g_cmd, pad, y,
               client_w - 3 * pad - go_w, cmd_row_h, TRUE);
    MoveWindow(g_cmd_go, client_w - pad - go_w, y,
               go_w, cmd_row_h, TRUE);
  }
}

/* ====================================================================== */
/* Menu and accelerators                                                  */
/* ====================================================================== */

static HMENU sr_build_menu(void)
{
  HMENU m = CreateMenu();
  HMENU game   = CreatePopupMenu();
  HMENU view   = CreatePopupMenu();
  HMENU select = CreatePopupMenu();
  HMENU unit   = CreatePopupMenu();
  HMENU work   = CreatePopupMenu();
  HMENU combat = CreatePopupMenu();
  HMENU city   = CreatePopupMenu();
  HMENU civ    = CreatePopupMenu();
  HMENU reports = CreatePopupMenu();
  HMENU map    = CreatePopupMenu();
  HMENU help   = CreatePopupMenu();

  AppendMenuW(game, MF_STRING, ID_SR_GAME_NEW,      L"&New game...\tCtrl+N");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_LOAD,     L"&Load game...\tCtrl+O");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_NATION,   L"Pick &nation...\tCtrl+Shift+N");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_START,    L"&Start local server");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_BEGIN,    L"&Begin game");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_END_TURN, L"&End turn\tCtrl+Enter");
  AppendMenuW(game, MF_SEPARATOR, 0, NULL);
  AppendMenuW(game, MF_STRING, ID_SR_GAME_SAVE,     L"&Save game\tCtrl+S");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_OPTIONS,  L"&Options\tCtrl+,");
  AppendMenuW(game, MF_STRING, ID_SR_GAME_MSGOPTS,  L"&Message options\tCtrl+Shift+M");
  AppendMenuW(game, MF_SEPARATOR, 0, NULL);
  AppendMenuW(game, MF_STRING, ID_SR_GAME_QUIT,     L"&Quit\tCtrl+Q");

  AppendMenuW(view, MF_STRING, ID_SR_VIEW_FOCUS_TREE, L"Focus &map view\tF1");
  AppendMenuW(view, MF_STRING, ID_SR_VIEW_FOCUS_UNITS, L"Focus &sidebar\tF2");
  AppendMenuW(view, MF_STRING, ID_SR_VIEW_FOCUS_CITIES, L"Focus &notifications\tF3");
  AppendMenuW(view, MF_STRING, ID_SR_VIEW_FOCUS_LOG,   L"Focus event &log\tCtrl+Shift+L");
  AppendMenuW(view, MF_STRING, ID_SR_VIEW_FOCUS_CMD,   L"Show &command prompt\tCtrl+Shift+C");

  AppendMenuW(select, MF_STRING, ID_SR_SELECT_NEXT, L"&Next unit\t.");
  AppendMenuW(select, MF_STRING, ID_SR_SELECT_PREV, L"&Previous unit\t,");
  AppendMenuW(select, MF_STRING, ID_SR_SELECT_WAIT, L"&Wait (defer)\tSpace");
  AppendMenuW(select, MF_SEPARATOR, 0, NULL);
  AppendMenuW(select, MF_STRING, ID_SR_SELECT_SAME_TILE, L"Same type on &tile");
  AppendMenuW(select, MF_STRING, ID_SR_SELECT_SAME_CONT, L"Same type on &continent");
  AppendMenuW(select, MF_STRING, ID_SR_SELECT_SAME_ALL,  L"Same type &anywhere");

  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_SPEAK,      L"Speak current &unit\tU");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_DONE,       L"&Done\tSpace");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_EXPLORE,    L"Auto e&xplore\tX");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_UPGRADE,    L"Up&grade in city");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_HOME,       L"Re&home in current city");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_PATROL,     L"&Patrol");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_PARADROP,   L"Para&drop");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_UNLOAD_ALL, L"Unload all from t&ransporter");
  AppendMenuW(unit, MF_STRING, ID_SR_UNIT_GOTO_CITY,  L"Goto cit&y...\tCtrl+Shift+G");
  AppendMenuW(unit, MF_STRING, ID_SR_MAP_TILE,        L"Speak &tile\tT");
  AppendMenuW(unit, MF_STRING, ID_SR_MAP_WHERE,       L"Speak coor&dinates\tW");

  AppendMenuW(work, MF_STRING, ID_SR_UNIT_BUILD_CITY, L"&Build city\tB");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_ROAD,     L"Build &road\tR");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_IRRIGATE, L"&Irrigate\tI");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_MINE,     L"&Mine\tM");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_PILLAGE,  L"&Pillage\tP");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_AUTO,     L"&Auto settler\tA");
  AppendMenuW(work, MF_SEPARATOR, 0, NULL);
  AppendMenuW(work, MF_STRING, ID_SR_WORK_TRANSFORM, L"&Transform terrain");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_CLEAN,     L"&Clean (pollution / fallout)");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_FORTRESS,  L"Build &fortress");
  AppendMenuW(work, MF_STRING, ID_SR_WORK_AIRBASE,   L"Build air&base");

  AppendMenuW(combat, MF_STRING, ID_SR_COMBAT_FORTIFY,      L"&Fortify\tF");
  AppendMenuW(combat, MF_STRING, ID_SR_COMBAT_SENTRY,       L"&Sentry\tS");
  AppendMenuW(combat, MF_STRING, ID_SR_COMBAT_WAKE,         L"Wa&ke");
  AppendMenuW(combat, MF_STRING, ID_SR_COMBAT_UNSENTRY_ALL, L"Wake &all sentried");
  AppendMenuW(combat, MF_STRING, ID_SR_COMBAT_DISBAND,      L"&Disband");
  AppendMenuW(combat, MF_STRING, ID_SR_COMBAT_ACTION_DO,    L"Diplomat / spy acti&on...");
  AppendMenuW(combat, MF_SEPARATOR, 0, NULL);
  AppendMenuW(combat, MF_STRING, ID_SR_BG1_SEL, L"Recall battle group &1\tCtrl+1");
  AppendMenuW(combat, MF_STRING, ID_SR_BG2_SEL, L"Recall battle group &2\tCtrl+2");
  AppendMenuW(combat, MF_STRING, ID_SR_BG3_SEL, L"Recall battle group &3\tCtrl+3");
  AppendMenuW(combat, MF_STRING, ID_SR_BG4_SEL, L"Recall battle group &4\tCtrl+4");
  AppendMenuW(combat, MF_STRING, ID_SR_BG1_SET, L"Assign to group 1\tCtrl+Shift+1");
  AppendMenuW(combat, MF_STRING, ID_SR_BG2_SET, L"Assign to group 2\tCtrl+Shift+2");
  AppendMenuW(combat, MF_STRING, ID_SR_BG3_SET, L"Assign to group 3\tCtrl+Shift+3");
  AppendMenuW(combat, MF_STRING, ID_SR_BG4_SET, L"Assign to group 4\tCtrl+Shift+4");

  AppendMenuW(city, MF_STRING, ID_SR_CITY_DIALOG,    L"Open city &dialog\tV");
  AppendMenuW(city, MF_STRING, ID_SR_CITY_CITIZENS,  L"&Citizens");
  AppendMenuW(city, MF_STRING, ID_SR_CITY_HAPPINESS, L"&Happiness");
  AppendMenuW(city, MF_STRING, ID_SR_CITY_CMA,       L"C&MA");
  AppendMenuW(city, MF_STRING, ID_SR_CITY_WORKLISTS, L"&Worklist editor\tCtrl+Shift+W");
  AppendMenuW(city, MF_STRING, ID_SR_CITY_INFRA,     L"&Infrastructure");
  AppendMenuW(city, MF_STRING, ID_SR_CITY_RALLY,     L"&Rally points");

  AppendMenuW(civ, MF_STRING, ID_SR_CIV_CITIES,     L"&Cities report");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_UNITS,      L"&Units report");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_PLAYERS,    L"&Nations / Diplomacy\tCtrl+D");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_INTEL,      L"&Intel (embassy)\tCtrl+I");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_RESEARCH,   L"&Research");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_ECONOMY,    L"&Economy");
  AppendMenuW(civ, MF_SEPARATOR, 0, NULL);
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_RATES,      L"Tax &rates...\tCtrl+R");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_POLICIES,   L"&Policies / multipliers...\tCtrl+Shift+P");
  AppendMenuW(civ, MF_STRING, ID_SR_CIV_REVOLUTION, L"Re&volution / government...\tCtrl+G");
  AppendMenuW(civ, MF_SEPARATOR, 0, NULL);
  AppendMenuW(reports, MF_STRING, ID_SR_CIV_DEMOGRAPHICS, L"&Demographics\tF11");
  AppendMenuW(reports, MF_STRING, ID_SR_CIV_TOPCITIES,    L"&Top cities");
  AppendMenuW(reports, MF_STRING, ID_SR_CIV_WONDERS,      L"&Wonders of the world");
  AppendMenuW(reports, MF_STRING, ID_SR_CIV_ACHIEVEMENTS, L"&Achievements");
  AppendMenuW(civ, MF_POPUP, (UINT_PTR)reports, L"Re&ports");

  AppendMenuW(map, MF_STRING, ID_SR_MAP_TILE,    L"Speak current &tile\tT");
  AppendMenuW(map, MF_STRING, ID_SR_MAP_LOOK,    L"Toggle &look mode\tL");
  AppendMenuW(map, MF_STRING, ID_SR_MAP_JUMP,    L"&Jump to review cursor\tJ");
  AppendMenuW(map, MF_STRING, ID_SR_MAP_WHERE,   L"Speak &coordinates\tW");
  AppendMenuW(map, MF_STRING, ID_SR_MAP_GOTO,    L"&Goto mode\tG");
  AppendMenuW(map, MF_STRING, ID_SR_MAP_CENTER,  L"Cente&r on focused unit");
  AppendMenuW(map, MF_STRING, ID_SR_MAP_VERBOSE_TOGGLE, L"Toggle ver&bose readout");
  AppendMenuW(map, MF_SEPARATOR, 0, NULL);
  AppendMenuW(map, MF_STRING, ID_SR_MAP_FIND_CITY, L"&Find city...\tCtrl+F");

  AppendMenuW(help, MF_STRING, ID_SR_HELP_COMMANDS, L"&Commands reference");

  AppendMenuW(m, MF_POPUP, (UINT_PTR)game,   L"&Game");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)view,   L"&View");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)select, L"&Select");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)unit,   L"&Unit");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)work,   L"&Work");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)combat, L"C&ombat");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)city,   L"C&ity");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)civ,    L"&Civilization");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)map,    L"&Map");
  AppendMenuW(m, MF_POPUP, (UINT_PTR)help,   L"&Help");

  return m;
}

static HACCEL sr_build_accelerators(void)
{
  ACCEL a[] = {
    /* Global (with Ctrl/Shift qualifiers) — work from any focus. */
    { FVIRTKEY | FCONTROL, 'Q',        ID_SR_GAME_QUIT        },
    { FVIRTKEY | FCONTROL, VK_RETURN,  ID_SR_GAME_END_TURN    },
    { FVIRTKEY | FCONTROL, 'T',        ID_SR_MAP_TILE         },
    { FVIRTKEY | FCONTROL, 'L',        ID_SR_MAP_LOOK         },
    { FVIRTKEY | FCONTROL, 'J',        ID_SR_MAP_JUMP         },
    { FVIRTKEY | FCONTROL, 'W',        ID_SR_MAP_WHERE        },
    { FVIRTKEY | FCONTROL, 'U',        ID_SR_UNIT_SPEAK       },
    { FVIRTKEY | FCONTROL, 'D',        ID_SR_CIV_PLAYERS      },
    { FVIRTKEY | FCONTROL, 'I',        ID_SR_CIV_INTEL        },
    { FVIRTKEY | FCONTROL, 'S',        ID_SR_GAME_SAVE        },
    { FVIRTKEY | FCONTROL, 'N',        ID_SR_GAME_NEW         },
    { FVIRTKEY | FCONTROL, 'O',        ID_SR_GAME_LOAD        },
    { FVIRTKEY | FCONTROL | FSHIFT, 'N', ID_SR_GAME_NATION    },
    { FVIRTKEY | FCONTROL, VK_OEM_COMMA, ID_SR_GAME_OPTIONS   },
    { FVIRTKEY | FCONTROL, 'R',          ID_SR_CIV_RATES      },
    { FVIRTKEY | FCONTROL, 'G',          ID_SR_CIV_REVOLUTION },
    { FVIRTKEY | FCONTROL | FSHIFT, 'P', ID_SR_CIV_POLICIES   },
    { FVIRTKEY | FCONTROL | FSHIFT, 'W', ID_SR_CITY_WORKLISTS },
    { FVIRTKEY | FCONTROL | FSHIFT, 'M', ID_SR_GAME_MSGOPTS   },
    { FVIRTKEY, VK_F11,                  ID_SR_CIV_DEMOGRAPHICS },
    /* Battle groups: Ctrl+1..4 recall, Ctrl+Shift+1..4 assign. */
    { FVIRTKEY | FCONTROL, '1',          ID_SR_BG1_SEL          },
    { FVIRTKEY | FCONTROL, '2',          ID_SR_BG2_SEL          },
    { FVIRTKEY | FCONTROL, '3',          ID_SR_BG3_SEL          },
    { FVIRTKEY | FCONTROL, '4',          ID_SR_BG4_SEL          },
    { FVIRTKEY | FCONTROL | FSHIFT, '1', ID_SR_BG1_SET          },
    { FVIRTKEY | FCONTROL | FSHIFT, '2', ID_SR_BG2_SET          },
    { FVIRTKEY | FCONTROL | FSHIFT, '3', ID_SR_BG3_SET          },
    { FVIRTKEY | FCONTROL | FSHIFT, '4', ID_SR_BG4_SET          },
    { FVIRTKEY | FCONTROL, 'F',          ID_SR_MAP_FIND_CITY    },
    { FVIRTKEY | FCONTROL | FSHIFT, 'G', ID_SR_UNIT_GOTO_CITY   },
    { FVIRTKEY | FCONTROL | FSHIFT, 'L', ID_SR_VIEW_FOCUS_LOG },
    { FVIRTKEY | FCONTROL | FSHIFT, 'C', ID_SR_VIEW_FOCUS_CMD },
    { FVIRTKEY | FCONTROL | FSHIFT, 'T', ID_SR_VIEW_FOCUS_TREE},
    /* Ctrl+Arrow — map movement / review cursor. Ctrl qualifier ensures
       NVDA passes them through even in browse mode. */
    { FVIRTKEY | FCONTROL, VK_UP,    ID_SR_MAP_MOVE_N  },
    { FVIRTKEY | FCONTROL, VK_DOWN,  ID_SR_MAP_MOVE_S  },
    { FVIRTKEY | FCONTROL, VK_LEFT,  ID_SR_MAP_MOVE_W  },
    { FVIRTKEY | FCONTROL, VK_RIGHT, ID_SR_MAP_MOVE_E  },
    { FVIRTKEY, VK_F1, ID_SR_VIEW_FOCUS_GAME     },
    { FVIRTKEY, VK_F2, ID_SR_VIEW_FOCUS_UNITS    },
    { FVIRTKEY, VK_F3, ID_SR_VIEW_FOCUS_CITIES   },
    { FVIRTKEY, VK_F4, ID_SR_VIEW_FOCUS_REVIEW   },
    { FVIRTKEY, VK_F5, ID_SR_VIEW_FOCUS_NATIONS  },
    { FVIRTKEY, VK_F6, ID_SR_VIEW_FOCUS_RESEARCH },
  };
  return CreateAcceleratorTableW(a, sizeof(a) / sizeof(a[0]));
}

/* ====================================================================== */
/* Command EDIT subclass — submit on Enter                                */
/* ====================================================================== */

static void sr_toggle_command_edit(int show_and_focus)
{
  if (show_and_focus) {
    g_cmd_visible = 1;
    ShowWindow(g_cmd, SW_SHOW);
    ShowWindow(g_cmd_go, SW_SHOW);
    sr_layout_children(g_hwnd);
    SetFocus(g_cmd);
  } else {
    g_cmd_visible = 0;
    SetWindowTextW(g_cmd, L"");
    ShowWindow(g_cmd, SW_HIDE);
    ShowWindow(g_cmd_go, SW_HIDE);
    sr_layout_children(g_hwnd);
    if (sr_play_hwnd() != NULL) {
      SetFocus(sr_play_hwnd());
    }
  }
}

static LRESULT CALLBACK sr_cmd_edit_proc(HWND hWnd, UINT msg,
                                         WPARAM wParam, LPARAM lParam)
{
  if (msg == WM_KEYDOWN && wParam == VK_RETURN) {
    sr_submit_command_edit();
    return 0;
  }
  if (msg == WM_CHAR && wParam == VK_RETURN) {
    return 0; /* eat the beep */
  }
  if (msg == WM_KEYDOWN && wParam == VK_ESCAPE) {
    sr_toggle_command_edit(0);
    return 0;
  }
  return CallWindowProcW(g_cmd_default_proc, hWnd, msg, wParam, lParam);
}

static void sr_submit_command_edit(void)
{
  wchar_t wbuf[1024];
  int wlen;
  char utf8[2048];

  wlen = GetWindowTextW(g_cmd, wbuf, sizeof(wbuf) / sizeof(wbuf[0]));
  if (wlen <= 0) {
    return;
  }
  WideCharToMultiByte(CP_UTF8, 0, wbuf, wlen + 1,
                      utf8, sizeof(utf8), NULL, NULL);
  sr_log_append(utf8);                     /* echo the line */
  sr_cmd_dispatch_line(utf8);              /* run the command */
  SetWindowTextW(g_cmd, L"");              /* clear for next */
}

/* ====================================================================== */
/* Command-id dispatch                                                    */
/* ====================================================================== */

/* Map menu/accelerator ids back onto the existing sr_cmd dispatcher so
   we have exactly one implementation of what each command means. */
static void sr_dispatch_command_id(int id)
{
  const char *line = NULL;

  switch (id) {
    case ID_SR_GAME_START:          line = "start";   break;
    case ID_SR_GAME_BEGIN:          line = "begin";   break;
    case ID_SR_GAME_END_TURN:       line = "end";     break;
    case ID_SR_GAME_QUIT:           line = "quit";    break;
    case ID_SR_UNIT_SPEAK:          line = "unit";    break;
    case ID_SR_UNIT_NEXT:           line = "next";    break;
    case ID_SR_UNIT_PREV:           line = "prev";    break;
    case ID_SR_UNIT_FORTIFY:        line = "fortify"; break;
    case ID_SR_UNIT_SENTRY:         line = "sentry";  break;
    case ID_SR_UNIT_DONE:           line = "done";    break;
    case ID_SR_UNIT_BUILD_CITY:     line = "build";   break;
    case ID_SR_MAP_TILE:            line = "tile";    break;
    case ID_SR_MAP_LOOK:            line = "look";    break;
    case ID_SR_MAP_JUMP:            line = "jump";    break;
    case ID_SR_MAP_WHERE:           line = "where";   break;
    case ID_SR_MAP_MOVE_N:          line = "n";       break;
    case ID_SR_MAP_MOVE_NE:         line = "ne";      break;
    case ID_SR_MAP_MOVE_E:          line = "e";       break;
    case ID_SR_MAP_MOVE_SE:         line = "se";      break;
    case ID_SR_MAP_MOVE_S:          line = "s";       break;
    case ID_SR_MAP_MOVE_SW:         line = "sw";      break;
    case ID_SR_MAP_MOVE_W:          line = "w";       break;
    case ID_SR_MAP_MOVE_NW:         line = "nw";      break;
    case ID_SR_HELP_COMMANDS:       line = "help";    break;

    /* Select menu */
    case ID_SR_SELECT_NEXT:         line = "next";    break;
    case ID_SR_SELECT_PREV:         line = "prev";    break;
    case ID_SR_SELECT_WAIT:         line = "wait";    break;

    /* Work menu */
    case ID_SR_WORK_ROAD:           line = "road";     break;
    case ID_SR_WORK_IRRIGATE:       line = "irrigate"; break;
    case ID_SR_WORK_MINE:           line = "mine";     break;
    case ID_SR_WORK_PILLAGE:        line = "pillage";  break;
    case ID_SR_WORK_AUTO:           line = "auto";     break;

    /* Combat menu */
    case ID_SR_COMBAT_FORTIFY:      line = "fortify";  break;
    case ID_SR_COMBAT_SENTRY:       line = "sentry";   break;
    case ID_SR_COMBAT_WAKE:         line = "wake";     break;
    case ID_SR_COMBAT_DISBAND:      line = "disband";  break;

    /* Civilization menu — v1 stubs that log a "not yet implemented"
       line; real sub-windows come in sr_citydlg / sr_sciencedlg / etc. */
    case ID_SR_CIV_CITIES: {
      struct player *pl = client_player();
      int n = (pl != NULL) ? city_list_size(pl->cities) : 0;

      if (n == 0) {
        sr_log_append("No cities to show yet.");
      } else if (n == 1) {
        sr_citydlg_open(city_list_get(pl->cities, 0));
      } else {
        sr_citylist_open();
      }
      return;
    }
    case ID_SR_CIV_PLAYERS:
      sr_diplodlg_open();
      return;
    case ID_SR_CIV_DIPLOMACY:
      sr_diplodlg_open();
      return;
    case ID_SR_CIV_RESEARCH:
      sr_sciencedlg_open();
      return;
    case ID_SR_CIV_ECONOMY:
      sr_economydlg_open();
      return;
    case ID_SR_CIV_UNITS:
      sr_unitsdlg_open();
      return;
    case ID_SR_CIV_INTEL:
      sr_cmd_dispatch_line("intel");
      return;
    case ID_SR_GAME_SAVE:
      sr_cmd_dispatch_line("save");
      return;
    case ID_SR_GAME_OPTIONS:
      sr_optdlg_open();
      return;
    case ID_SR_MAP_GOTO:
      sr_cmd_dispatch_line("goto");
      return;
    case ID_SR_GAME_NEW:
      sr_newgame_open();
      return;
    case ID_SR_GAME_LOAD:
      sr_loadgame_open();
      return;
    case ID_SR_GAME_NATION:
      if (client_player() != NULL) {
        popup_races_dialog(client_player());
      } else {
        sr_say("Pick nation only works in pregame, after taking a player.",
               1);
      }
      return;

    /* Phase 7a — surface existing sr_cmd verbs. All route through the
       canonical command path so menu and typed-verb behavior stay in
       lock-step. */
    case ID_SR_GAME_MSGOPTS:    sr_cmd_dispatch_line("msgopts");      return;
    case ID_SR_CIV_RATES:       sr_cmd_dispatch_line("rates");        return;
    case ID_SR_CIV_POLICIES:    sr_cmd_dispatch_line("policies");     return;
    case ID_SR_CIV_REVOLUTION:  sr_cmd_dispatch_line("revolution");   return;
    case ID_SR_CIV_DEMOGRAPHICS: sr_cmd_dispatch_line("demographics"); return;
    case ID_SR_CIV_TOPCITIES:   sr_cmd_dispatch_line("topcities");    return;
    case ID_SR_CIV_WONDERS:     sr_cmd_dispatch_line("wonders");      return;
    case ID_SR_CIV_ACHIEVEMENTS: sr_cmd_dispatch_line("achievements"); return;
    case ID_SR_CITY_DIALOG:     sr_cmd_dispatch_line("viewcity");     return;
    case ID_SR_CITY_CITIZENS:   sr_cmd_dispatch_line("citizens");     return;
    case ID_SR_CITY_HAPPINESS:  sr_cmd_dispatch_line("happiness");    return;
    case ID_SR_CITY_CMA:        sr_cmd_dispatch_line("cma");          return;
    case ID_SR_CITY_WORKLISTS:  sr_cmd_dispatch_line("worklists");    return;
    case ID_SR_CITY_INFRA:      sr_cmd_dispatch_line("infra");        return;
    case ID_SR_CITY_RALLY:      sr_cmd_dispatch_line("rally");        return;
    case ID_SR_MAP_CENTER:      sr_cmd_dispatch_line("center");       return;
    case ID_SR_MAP_VERBOSE_TOGGLE: sr_cmd_toggle_terse();             return;
    case ID_SR_UNIT_EXPLORE:    sr_cmd_dispatch_line("explore");      return;
    case ID_SR_UNIT_UPGRADE:    sr_cmd_dispatch_line("upgrade");      return;
    case ID_SR_UNIT_HOME:       sr_cmd_dispatch_line("home");         return;

    /* Phase 7b — new unit actions, all dispatched as canonical verbs. */
    case ID_SR_UNIT_PATROL:        sr_cmd_dispatch_line("patrol");      return;
    case ID_SR_UNIT_PARADROP:      sr_cmd_dispatch_line("paradrop");    return;
    case ID_SR_UNIT_UNLOAD_ALL:    sr_cmd_dispatch_line("unloadall");   return;
    case ID_SR_COMBAT_UNSENTRY_ALL: sr_cmd_dispatch_line("unsentry");   return;
    case ID_SR_COMBAT_ACTION_DO:   sr_cmd_dispatch_line("action");      return;
    case ID_SR_WORK_TRANSFORM:     sr_cmd_dispatch_line("transform");   return;
    case ID_SR_WORK_CLEAN:         sr_cmd_dispatch_line("clean");       return;
    case ID_SR_WORK_FORTRESS:      sr_cmd_dispatch_line("fortress");    return;
    case ID_SR_WORK_AIRBASE:       sr_cmd_dispatch_line("airbase");     return;
    case ID_SR_SELECT_SAME_TILE:   sr_cmd_dispatch_line("seltile");     return;
    case ID_SR_SELECT_SAME_CONT:   sr_cmd_dispatch_line("selcont");     return;
    case ID_SR_SELECT_SAME_ALL:    sr_cmd_dispatch_line("selall");      return;
    case ID_SR_BG1_SEL:            sr_cmd_dispatch_line("bg1");         return;
    case ID_SR_BG2_SEL:            sr_cmd_dispatch_line("bg2");         return;
    case ID_SR_BG3_SEL:            sr_cmd_dispatch_line("bg3");         return;
    case ID_SR_BG4_SEL:            sr_cmd_dispatch_line("bg4");         return;
    case ID_SR_BG1_SET:            sr_cmd_dispatch_line("bgsave1");     return;
    case ID_SR_BG2_SET:            sr_cmd_dispatch_line("bgsave2");     return;
    case ID_SR_BG3_SET:            sr_cmd_dispatch_line("bgsave3");     return;
    case ID_SR_BG4_SET:            sr_cmd_dispatch_line("bgsave4");     return;
    case ID_SR_MAP_FIND_CITY:      sr_cmd_dispatch_line("findcity");    return;
    case ID_SR_UNIT_GOTO_CITY:     sr_cmd_dispatch_line("gotocity");    return;

    case ID_SR_VIEW_FOCUS_LOG:      SetFocus(g_log);  return;
    case ID_SR_VIEW_FOCUS_CMD:      sr_toggle_command_edit(1); return;
    case ID_SR_VIEW_FOCUS_TREE:
      if (sr_play_hwnd() != NULL) {
        SetFocus(sr_play_hwnd());
      }
      return;
    case ID_SR_VIEW_FOCUS_GAME:
      if (sr_play_hwnd() != NULL) {
        SetFocus(sr_play_hwnd());
      }
      return;
    case ID_SR_VIEW_FOCUS_UNITS:
      if (sr_sidebar_hwnd() != NULL) {
        SetFocus(sr_sidebar_hwnd());
      }
      return;
    case ID_SR_VIEW_FOCUS_CITIES:
      if (sr_notify_hwnd() != NULL) {
        SetFocus(sr_notify_hwnd());
      }
      return;
    case ID_SR_VIEW_FOCUS_REVIEW:
      /* F4 → toggle look mode on the map. */
      sr_cmd_dispatch_line("look");
      if (sr_play_hwnd() != NULL) {
        SetFocus(sr_play_hwnd());
      }
      return;
    case ID_SR_VIEW_FOCUS_NATIONS:
      SetFocus(g_log);
      return;
    case ID_SR_VIEW_FOCUS_RESEARCH:
      sr_toggle_command_edit(1);
      return;

    default:
      return;
  }

  if (line != NULL) {
    sr_cmd_dispatch_line(line);
  }
}

/* ====================================================================== */
/* Window procedure                                                       */
/* ====================================================================== */

static LRESULT CALLBACK sr_wnd_proc(HWND hWnd, UINT msg,
                                    WPARAM wParam, LPARAM lParam)
{
  switch (msg) {

    case WM_SIZE:
      sr_layout_children(hWnd);
      return 0;

    case WM_COMMAND: {
      int id = LOWORD(wParam);

      if (id == IDC_CMD_GO && HIWORD(wParam) == BN_CLICKED) {
        sr_submit_command_edit();
        return 0;
      }
      if (id >= ID_SR_MENU_FIRST) {
        sr_dispatch_command_id(id);
        return 0;
      }
      return 0;
    }

    case WM_SR_NET: {
      int event = WSAGETSELECTEVENT(lParam);

      if (g_net_sock >= 0 && (event & (FD_READ | FD_CLOSE))) {
        input_from_server(g_net_sock);
      }
      return 0;
    }

    case WM_SR_LOG_APPEND:
      sr_log_handle_posted(lParam);
      return 0;

    case WM_SR_STDIN: {
      char *line = (char *)lParam;

      if (line != NULL) {
        sr_cmd_dispatch_line(line);
        free(line);
      }
      return 0;
    }

    case WM_TIMER:
      if (wParam == SR_TIMER_ID) {
        /* Drain any stdin lines that the reader thread has queued, then
           let freeciv's internal scheduling tick. */
        sr_cmd_poll();
        real_timer_callback();
        if (is_client_quitting()) {
          PostQuitMessage(0);
        }
      }
      return 0;

    case WM_CLOSE:
      /* "X" button: synthesise a quit. start_quitting() sets the flag
         that makes is_client_quitting() return true, and that tears
         down the main loop cleanly. */
      sr_cmd_dispatch_line("quit");
      return 0;

    case WM_DESTROY:
      PostQuitMessage(0);
      return 0;

    default:
      return DefWindowProcW(hWnd, msg, wParam, lParam);
  }
}
