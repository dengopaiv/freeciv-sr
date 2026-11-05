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
  City dialog: non-modal top-level window summarising one city.

  Shows a fixed field order (name · size · surplus · production ·
  improvements · supported units · happiness breakdown) as a read-only
  multiline EDIT. NVDA navigates the text natively with arrow keys;
  Escape closes the window and returns focus to the Map View.

  One instance at a time; opening a second city closes the first.
*/

#ifndef FC__SR_CITYDLG_H
#define FC__SR_CITYDLG_H

struct city;

#ifdef _WIN32
#include <windows.h>

void sr_citydlg_open(struct city *pcity);
void sr_citydlg_close(void);
void sr_citydlg_refresh(void);
void sr_citydlg_refresh_worklist(void);
void sr_citydlg_shutdown(void);
int  sr_citydlg_is_open(void);

#else
static inline void sr_citydlg_open(struct city *pcity) { (void)pcity; }
static inline void sr_citydlg_close(void) {}
static inline void sr_citydlg_refresh(void) {}
static inline void sr_citydlg_refresh_worklist(void) {}
static inline void sr_citydlg_shutdown(void) {}
static inline int  sr_citydlg_is_open(void) { return 0; }
#endif

#endif  /* FC__SR_CITYDLG_H */
