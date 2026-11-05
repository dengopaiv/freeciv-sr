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
  Nations dialog: listbox of all known alive players with their relation
  to us. Enter on a row requests a meeting (if allowed) — the server
  replies with PACKET_DIPLOMACY_INIT_MEETING, which opens sr_treaty.
*/

#ifndef FC__SR_DIPLODLG_H
#define FC__SR_DIPLODLG_H

#ifdef _WIN32
#include <windows.h>

void sr_diplodlg_open(void);
void sr_diplodlg_close(void);
void sr_diplodlg_refresh(void);
int  sr_diplodlg_is_open(void);

#else
static inline void sr_diplodlg_open(void) {}
static inline void sr_diplodlg_close(void) {}
static inline void sr_diplodlg_refresh(void) {}
static inline int  sr_diplodlg_is_open(void) { return 0; }
#endif

#endif  /* FC__SR_DIPLODLG_H */
