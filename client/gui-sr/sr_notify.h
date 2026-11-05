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
  Notifications panel: a short flat list of things the player might want
  to act on. Civ4+ style, flat for v1.

  Kinds:
    SR_NOTIFY_WAITING_UNIT  — ref = unit id; activate cycles to it
    SR_NOTIFY_TURN_DIGEST   — informational; activate is a no-op
    SR_NOTIFY_RESEARCH_DONE — activate opens the research picker
    SR_NOTIFY_NEW_CONTACT   — ref = player id; activate opens diplomacy
*/

#ifndef FC__SR_NOTIFY_H
#define FC__SR_NOTIFY_H

#ifdef _WIN32
#include <windows.h>

enum sr_notify_kind {
  SR_NOTIFY_WAITING_UNIT = 1,
  SR_NOTIFY_TURN_DIGEST,
  SR_NOTIFY_RESEARCH_DONE,
  SR_NOTIFY_NEW_CONTACT
};

int sr_notify_create(HWND parent);
HWND sr_notify_hwnd(void);

/* Clear all items and recompute waiting-units entries. Fast; called on
   unit-state changes and turn transitions. */
void sr_notify_recompute_waiting(void);

/* Append a "last turn" digest entry. */
void sr_notify_turn_digest(const char *utf8);

/* Generic push. Text is UTF-8; ref is interpreted according to kind. */
void sr_notify_push(enum sr_notify_kind kind, int ref, const char *utf8);

/* Activate the currently-selected notification — wired to the LISTBOX's
   LBN_DBLCLK and the Enter key in WndProc. */
void sr_notify_activate_current(void);

#else
static inline int sr_notify_create(void *p) { (void)p; return 1; }
static inline void *sr_notify_hwnd(void) { return 0; }
static inline void sr_notify_recompute_waiting(void) {}
static inline void sr_notify_turn_digest(const char *u) { (void)u; }
static inline void sr_notify_activate_current(void) {}
#endif

#endif  /* FC__SR_NOTIFY_H */
