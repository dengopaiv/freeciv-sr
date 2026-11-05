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
  Event log surface: a read-only multiline EDIT that NVDA reads natively
  when focused. Any UTF-8 string appended here becomes the user's
  scrollable history of what happened. Windows-only; stubs on other
  platforms so non-Windows cross-compiles still link.
*/

#ifndef FC__SR_LOG_H
#define FC__SR_LOG_H

#ifdef _WIN32
#include <windows.h>
void sr_log_attach(HWND edit_hwnd);
void sr_log_handle_posted(LPARAM lparam);
#else
void sr_log_attach(void *edit_hwnd);
#endif

/* Thread-safe append of one line. A trailing newline is added if missing.
   NUL or empty input is a no-op. Safe to call from any thread:
   implementation dispatches via PostMessage when not on the UI thread. */
void sr_log_append(const char *utf8_line);

/* Return non-zero iff attach() has been called. Used by sr_speech.c to
   decide between the EDIT and the stderr fallback during early startup. */
int sr_log_is_up(void);

#endif  /* FC__SR_LOG_H */
