/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
***********************************************************************/

/*
  freeciv-sr-tools.exe — accessibility-friendly companion utilities for
  the freeciv-sr client. Phase 5a: a random map generator that drives the
  bundled freeciv-server.exe headlessly via a generated .serv script.
  Future sub-modes (Phase 5b: tile editor) plug into the same mode picker.

  This binary is intentionally tiny: it reuses sr_pick.c (picker UI) and
  sr_speech.c (NVDA / SAPI routing) from gui-sr verbatim, and supplies
  its own minimal stubs for the sr_window / sr_log hooks those files
  expect (no scrollback log pane, no main window — just a chain of
  Win32 popup dialogs).
*/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <windows.h>

#include "sr_pick.h"
#include "sr_speech.h"

#include "mode_mapgen.h"

/* ---------------------------------------------------------------------
   Stubs for sr_pick.c / sr_speech.c host hooks.

   The full gui-sr client owns a popup-table inside sr_window.c plus a
   real log EDIT control. The tools binary has neither: it is just a
   chain of modal pickers. The active picker is tracked here and fed
   to IsDialogMessageW in the message loop below so Tab / arrow keys
   navigate as expected. Log calls are silently dropped; speech still
   reaches NVDA via sr_speech_init -> nvdaControllerClient.dll.
   ------------------------------------------------------------------ */

#define TOOLS_MAX_POPUPS 4
static HWND g_popups[TOOLS_MAX_POPUPS];

void sr_window_register_popup(HWND h)
{
  int i;

  if (h == NULL) { return; }
  for (i = 0; i < TOOLS_MAX_POPUPS; i++) {
    if (g_popups[i] == NULL) { g_popups[i] = h; return; }
  }
}

void sr_window_unregister_popup(HWND h)
{
  int i;

  for (i = 0; i < TOOLS_MAX_POPUPS; i++) {
    if (g_popups[i] == h) { g_popups[i] = NULL; }
  }
}

/* sr_log stubs: tools binary has no log pane, so calls become no-ops.
   sr_speech still calls these to mirror text into the log; harmless. */
void sr_log_attach(HWND edit_hwnd) { (void) edit_hwnd; }
int  sr_log_is_up(void) { return 0; }
void sr_log_append(const char *utf8_line) { (void) utf8_line; }
void sr_log_handle_posted(LPARAM lparam) { (void) lparam; }

/* ---------------------------------------------------------------------
   Mode picker — entry screen of the tool. One entry per sub-mode.
   ------------------------------------------------------------------ */

static const char *const TOOLS_MODES[] = {
  "Random map generator",
  "Quit"
};

static int g_quit_requested = 0;

static void on_pick_mode(int idx, const char *value, void *ud)
{
  (void) value;
  (void) ud;

  switch (idx) {
    case 0:
      mode_mapgen_open();
      break;
    case 1:
    default:
      g_quit_requested = 1;
      PostQuitMessage(0);
      break;
  }
}

void tools_open_mode_picker(void)
{
  sr_pick_enum("freeciv-sr-tools", TOOLS_MODES,
               (int)(sizeof(TOOLS_MODES) / sizeof(TOOLS_MODES[0])),
               0, on_pick_mode, NULL);
}

/* ---------------------------------------------------------------------
   Message loop. Mirrors gui-sr's loop shape minus the play-pane carve
   out and the accelerator table.
   ------------------------------------------------------------------ */

static int run_message_loop(void)
{
  MSG msg;

  while (GetMessageW(&msg, NULL, 0, 0) > 0) {
    int handled = 0;
    int i;

    for (i = 0; i < TOOLS_MAX_POPUPS; i++) {
      HWND p = g_popups[i];

      if (p != NULL && IsWindow(p)
          && (msg.hwnd == p || IsChild(p, msg.hwnd))) {
        if (IsDialogMessageW(p, &msg)) { handled = 1; }
        break;
      }
    }
    if (handled) { continue; }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);

    if (g_quit_requested) { break; }
  }
  return (int) msg.wParam;
}

/* ---------------------------------------------------------------------
   Entry point. Console subsystem so the user can pipe scripts in or
   redirect output for debugging; the actual UI is a Win32 popup chain.
   ------------------------------------------------------------------ */

int main(int argc, char **argv)
{
  (void) argc;
  (void) argv;

  sr_speech_init();
  sr_say("freeciv-sr tools. Random map generator ready.", 1);

  tools_open_mode_picker();
  run_message_loop();

  sr_speech_exit();
  return 0;
}
