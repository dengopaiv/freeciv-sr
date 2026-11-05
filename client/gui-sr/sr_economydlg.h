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

#ifndef FC__SR_ECONOMYDLG_H
#define FC__SR_ECONOMYDLG_H

#ifdef _WIN32
#include <windows.h>
void sr_economydlg_open(void);
void sr_economydlg_close(void);
void sr_economydlg_refresh(void);
int  sr_economydlg_is_open(void);
#else
static inline void sr_economydlg_open(void) {}
static inline void sr_economydlg_close(void) {}
static inline void sr_economydlg_refresh(void) {}
static inline int  sr_economydlg_is_open(void) { return 0; }
#endif

#endif  /* FC__SR_ECONOMYDLG_H */
