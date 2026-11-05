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
  Sidebar: always-visible summary strip alongside the Map View.

  Implemented as a single SysListBox so NVDA can arrow-navigate through
  rows and the user only spends one Tab stop on the whole strip. Rebuilt
  wholesale on each game/unit-state change — cheap because a game never
  has that many top-level indicators.
*/

#ifndef FC__SR_SIDEBAR_H
#define FC__SR_SIDEBAR_H

#ifdef _WIN32
#include <windows.h>

int sr_sidebar_create(HWND parent);
HWND sr_sidebar_hwnd(void);
void sr_sidebar_update_game(void);
void sr_sidebar_update_unit(void);

#else
static inline int sr_sidebar_create(void *parent) { (void)parent; return 1; }
static inline void *sr_sidebar_hwnd(void) { return 0; }
static inline void sr_sidebar_update_game(void) {}
static inline void sr_sidebar_update_unit(void) {}
#endif

#endif  /* FC__SR_SIDEBAR_H */
