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
  Map View: the keyboard-input surface for gameplay.

  Modelled on gui-gtk-4.0's map_canvas (GtkDrawingArea that owns key
  and mouse handlers) and gui-qt's map_view (QWidget with
  Qt::WheelFocus). We don't draw; we only capture input. Arrow keys,
  numpad, and single-letter hotkeys all route through the existing
  sr_cmd_dispatch_line vocabulary so there's exactly one definition
  of what each command means.

  Default keyboard focus lands here, same as the stock clients.
*/

#ifndef FC__SR_PLAY_H
#define FC__SR_PLAY_H

#ifdef _WIN32
#include <windows.h>

/* Register the Map View window class. Call once, before sr_play_create. */
int sr_play_register_class(HINSTANCE hi);

/* Create a Map View child under parent. Returns HWND. */
HWND sr_play_create(HWND parent);

/* The Map View's current HWND (NULL before create / after destroy). */
HWND sr_play_hwnd(void);

/* Emit a one-line "current situation" announcement via sr_say. Called
   automatically on focus-enter; exposed so sub-dialogs can re-trigger
   it on close. */
void sr_play_announce_situation(void);

#else  /* !_WIN32 */

static inline int sr_play_register_class(void *hi) { (void)hi; return 1; }
static inline void *sr_play_create(void *parent) { (void)parent; return 0; }
static inline void *sr_play_hwnd(void) { return 0; }
static inline void sr_play_announce_situation(void) {}

#endif  /* _WIN32 */

#endif  /* FC__SR_PLAY_H */
