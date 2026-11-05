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
  Status bar wrapper: composes a one-line "Turn · Units · Cities · Gold ·
  Science" summary and pushes it into the msctls_statusbar32 HWND the
  main window creates.
*/

#ifndef FC__SR_STATUS_H
#define FC__SR_STATUS_H

#ifdef _WIN32
#include <windows.h>

void sr_status_attach(HWND status_hwnd);
void sr_status_update(void);

#else
static inline void sr_status_attach(void *h) { (void)h; }
static inline void sr_status_update(void) {}
#endif

#endif  /* FC__SR_STATUS_H */
