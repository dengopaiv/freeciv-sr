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
  Cities picker: listbox of the local player's cities, one line each.
  Enter on a row opens sr_citydlg for that city; Escape returns to the
  Map View.
*/

#ifndef FC__SR_CITYLIST_H
#define FC__SR_CITYLIST_H

#ifdef _WIN32
#include <windows.h>

void sr_citylist_open(void);
void sr_citylist_close(void);
void sr_citylist_refresh(void);
int  sr_citylist_is_open(void);

#else
static inline void sr_citylist_open(void) {}
static inline void sr_citylist_close(void) {}
static inline void sr_citylist_refresh(void) {}
static inline int  sr_citylist_is_open(void) { return 0; }
#endif

#endif  /* FC__SR_CITYLIST_H */
