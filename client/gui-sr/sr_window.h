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

/*
  Native Win32 window for gui-sr.

  Owns a SysTreeView32 for game-state navigation, a read-only multiline
  EDIT for the event log, a single-line EDIT for typed commands, a menu
  bar, and a status bar. Every control is a native Windows control so
  NVDA reads them via default MSAA/UIA handling with no custom
  accessibility code on our side.
*/

#ifndef FC__SR_WINDOW_H
#define FC__SR_WINDOW_H

#include <windows.h>

/* User-defined window messages. */
#define WM_SR_NET        (WM_USER + 1)  /* lParam: WSAAsyncSelect payload  */
#define WM_SR_LOG_APPEND (WM_USER + 2)  /* lParam: malloc'd UTF-8 string    */
#define WM_SR_STDIN      (WM_USER + 3)  /* lParam: malloc'd UTF-8 line      */
#define WM_SR_TREE_DIRTY (WM_USER + 4)  /* wParam: section id               */
#define WM_SR_SHUTDOWN   (WM_USER + 5)  /* posted on client quit           */

/* Tree-section ids, used for WM_SR_TREE_DIRTY. Matches sr_tree.h. */
enum sr_tree_section {
  SR_TREE_GAME = 0,
  SR_TREE_UNITS,
  SR_TREE_CITIES,
  SR_TREE_REVIEW,
  SR_TREE_NATIONS,
  SR_TREE_RESEARCH,
  SR_TREE_PREGAME,
  SR_TREE_N_SECTIONS
};

/* WM_COMMAND identifiers for menu items and accelerators. */
enum sr_cmd_id {
  ID_SR_MENU_FIRST    = 100,
  ID_SR_GAME_START    = 100,
  ID_SR_GAME_BEGIN,
  ID_SR_GAME_END_TURN,
  ID_SR_GAME_QUIT,
  ID_SR_UNIT_SPEAK,
  ID_SR_UNIT_NEXT,
  ID_SR_UNIT_PREV,
  ID_SR_UNIT_FORTIFY,
  ID_SR_UNIT_SENTRY,
  ID_SR_UNIT_DONE,
  ID_SR_UNIT_BUILD_CITY,
  ID_SR_MAP_TILE,
  ID_SR_MAP_LOOK,
  ID_SR_MAP_JUMP,
  ID_SR_MAP_WHERE,
  ID_SR_MAP_MOVE_N,
  ID_SR_MAP_MOVE_NE,
  ID_SR_MAP_MOVE_E,
  ID_SR_MAP_MOVE_SE,
  ID_SR_MAP_MOVE_S,
  ID_SR_MAP_MOVE_SW,
  ID_SR_MAP_MOVE_W,
  ID_SR_MAP_MOVE_NW,
  ID_SR_VIEW_FOCUS_LOG,
  ID_SR_VIEW_FOCUS_CMD,
  ID_SR_VIEW_FOCUS_TREE,
  ID_SR_VIEW_FOCUS_GAME,
  ID_SR_VIEW_FOCUS_UNITS,
  ID_SR_VIEW_FOCUS_CITIES,
  ID_SR_VIEW_FOCUS_REVIEW,
  ID_SR_VIEW_FOCUS_NATIONS,
  ID_SR_VIEW_FOCUS_RESEARCH,
  ID_SR_HELP_COMMANDS,

  /* Select menu. */
  ID_SR_SELECT_NEXT,
  ID_SR_SELECT_PREV,
  ID_SR_SELECT_WAIT,

  /* Work menu (terrain actions). */
  ID_SR_WORK_ROAD,
  ID_SR_WORK_IRRIGATE,
  ID_SR_WORK_MINE,
  ID_SR_WORK_PILLAGE,
  ID_SR_WORK_AUTO,

  /* Combat / movement extras. */
  ID_SR_COMBAT_FORTIFY,
  ID_SR_COMBAT_SENTRY,
  ID_SR_COMBAT_WAKE,
  ID_SR_COMBAT_DISBAND,

  /* Civilization menu — reports + sub-screens (v1: log a stub until the
     modal dialogs land). */
  ID_SR_CIV_CITIES,
  ID_SR_CIV_PLAYERS,
  ID_SR_CIV_ECONOMY,
  ID_SR_CIV_RESEARCH,
  ID_SR_CIV_DIPLOMACY,
  ID_SR_CIV_UNITS,
  ID_SR_CIV_INTEL,

  ID_SR_GAME_SAVE,
  ID_SR_GAME_OPTIONS,
  ID_SR_GAME_NEW,
  ID_SR_GAME_LOAD,
  ID_SR_GAME_NATION,
  ID_SR_GAME_MSGOPTS,

  /* Civilization-wide actions surfaced from sr_cmd verbs (Phase 7a). */
  ID_SR_CIV_RATES,
  ID_SR_CIV_POLICIES,
  ID_SR_CIV_REVOLUTION,
  ID_SR_CIV_DEMOGRAPHICS,
  ID_SR_CIV_TOPCITIES,
  ID_SR_CIV_WONDERS,
  ID_SR_CIV_ACHIEVEMENTS,

  /* Current-city actions — own City menu (Phase 7a). */
  ID_SR_CITY_DIALOG,
  ID_SR_CITY_CITIZENS,
  ID_SR_CITY_HAPPINESS,
  ID_SR_CITY_CMA,
  ID_SR_CITY_WORKLISTS,
  ID_SR_CITY_INFRA,
  ID_SR_CITY_RALLY,

  /* Map readout extras (Phase 7a). */
  ID_SR_MAP_CENTER,
  ID_SR_MAP_VERBOSE_TOGGLE,

  /* Unit action extras (Phase 7a). */
  ID_SR_UNIT_EXPLORE,
  ID_SR_UNIT_UPGRADE,
  ID_SR_UNIT_HOME,

  /* Phase 7b — new unit actions (no plain-letter accelerators; menu
     items + Ctrl-modified accelerators only, to avoid stealing keys
     from sr_play.c). */
  ID_SR_UNIT_PATROL,
  ID_SR_UNIT_PARADROP,
  ID_SR_UNIT_UNLOAD_ALL,
  ID_SR_COMBAT_UNSENTRY_ALL,
  ID_SR_COMBAT_ACTION_DO,
  ID_SR_WORK_TRANSFORM,
  ID_SR_WORK_CLEAN,
  ID_SR_WORK_FORTRESS,
  ID_SR_WORK_AIRBASE,
  ID_SR_SELECT_SAME_TILE,
  ID_SR_SELECT_SAME_CONT,
  ID_SR_SELECT_SAME_ALL,
  ID_SR_BG1_SEL, ID_SR_BG2_SEL, ID_SR_BG3_SEL, ID_SR_BG4_SEL,
  ID_SR_BG1_SET, ID_SR_BG2_SET, ID_SR_BG3_SET, ID_SR_BG4_SET,

  /* Civ-II-style city pickers (Phase 7c). */
  ID_SR_MAP_FIND_CITY,
  ID_SR_UNIT_GOTO_CITY,

  ID_SR_MAP_GOTO,

  ID_SR_COMMAND_SUBMIT = 200,   /* from the command EDIT's Enter key     */
};

/* Called once during gui_ui_init. Registers the window class, creates the
   main window, wires it to child controls, attaches sr_log to the EDIT.
   After this returns non-zero, sr_log_is_up() becomes true. */
int sr_window_create(void);

/* Tear down. Called from gui_ui_exit. */
void sr_window_destroy(void);

/* Returns the window handle, or NULL before create / after destroy. */
HWND sr_window_hwnd(void);

/* Return the tree control's HWND. sr_tree.c uses this to issue TVM_*
   messages. Returns NULL if window isn't up yet. */
HWND sr_window_tree_hwnd(void);

/* The thread id that runs the message pump. sr_log uses this to decide
   whether to append directly or PostMessage. */
DWORD sr_window_ui_thread_id(void);

/* The main loop. Runs GetMessage/DispatchMessage until WM_QUIT. */
int sr_window_run_message_loop(void);

/* Register/unregister a non-modal popup whose children should get
   IsDialogMessage processing (Tab navigation, default-button Enter,
   Escape). Safe to call with NULL; quietly no-ops on unknown HWND. */
void sr_window_register_popup(HWND h);
void sr_window_unregister_popup(HWND h);

/* Called by net-input glue when the client core connects/disconnects. */
void sr_window_set_net_socket(int sock);

/* Posted by sr_log when off-thread append is requested. Defined in
   sr_log.c, declared here so WndProc can call it. */
void sr_log_handle_posted(LPARAM lparam);

#endif  /* FC__SR_WINDOW_H */
