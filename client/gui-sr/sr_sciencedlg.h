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
  Research dialog: pick current research target and long-term goal.
  Non-modal top-level window. Same skeleton as sr_citydlg / sr_citylist
  so NVDA behaviour is consistent across screens.
*/

#ifndef FC__SR_SCIENCEDLG_H
#define FC__SR_SCIENCEDLG_H

#ifdef _WIN32
#include <windows.h>

void sr_sciencedlg_open(void);
void sr_sciencedlg_close(void);
void sr_sciencedlg_refresh(void);
int  sr_sciencedlg_is_open(void);

#else
static inline void sr_sciencedlg_open(void) {}
static inline void sr_sciencedlg_close(void) {}
static inline void sr_sciencedlg_refresh(void) {}
static inline int  sr_sciencedlg_is_open(void) { return 0; }
#endif

#endif  /* FC__SR_SCIENCEDLG_H */
