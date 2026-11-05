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
  Treaty window: the clause-negotiation surface for one bilateral meeting.

  Opens via sr_treaty_open from gui_init_meeting; closes via
  sr_treaty_close_for from gui_recv_cancel_meeting. One active
  window at a time in v1.
*/

#ifndef FC__SR_TREATY_H
#define FC__SR_TREATY_H

struct Treaty;
struct player;

#ifdef _WIN32
#include <windows.h>

void sr_treaty_open(struct Treaty *ptreaty, struct player *they);
void sr_treaty_refresh(struct Treaty *ptreaty);
void sr_treaty_close_for(struct player *they);
void sr_treaty_close_all(void);
int  sr_treaty_is_open(void);

#else
static inline void sr_treaty_open(struct Treaty *p, struct player *they) {
  (void)p; (void)they;
}
static inline void sr_treaty_refresh(struct Treaty *p) { (void)p; }
static inline void sr_treaty_close_for(struct player *they) { (void)they; }
static inline void sr_treaty_close_all(void) {}
static inline int  sr_treaty_is_open(void) { return 0; }
#endif

#endif  /* FC__SR_TREATY_H */
