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
  Reusable keyboard-only sub-pickers for one int or one enum-ish value.
  Each opens a small popup window; Enter commits via the supplied
  callback, Escape cancels. Returns focus to the window that had focus
  when the picker opened.
*/

#ifndef FC__SR_PICK_H
#define FC__SR_PICK_H

#ifdef _WIN32
#include <windows.h>

typedef void (*sr_pick_int_cb)(int value, void *userdata);
typedef void (*sr_pick_enum_cb)(int index, const char *value_utf8,
                                void *userdata);
typedef void (*sr_pick_string_cb)(const char *value_utf8, void *userdata);

/* Single-line integer editor. min == max == 0 means "no bounds check". */
void sr_pick_int(const char *title_utf8, int current, int minv, int maxv,
                 sr_pick_int_cb on_commit, void *userdata);

/* Single-column listbox of string choices. Preselects `current`. */
void sr_pick_enum(const char *title_utf8,
                  const char * const *values_utf8, int n_values,
                  int current,
                  sr_pick_enum_cb on_commit, void *userdata);

/* Single-line free-text editor. `initial` is preselected (NULL == empty).
   Empty Enter still commits (callback receives ""). Escape cancels. */
void sr_pick_string(const char *title_utf8, const char *initial_utf8,
                    sr_pick_string_cb on_commit, void *userdata);

#else
static inline void sr_pick_int(const char *t, int c, int a, int b,
                               void *cb, void *ud) {
  (void)t; (void)c; (void)a; (void)b; (void)cb; (void)ud;
}
static inline void sr_pick_enum(const char *t, const char * const *v, int n,
                                int c, void *cb, void *ud) {
  (void)t; (void)v; (void)n; (void)c; (void)cb; (void)ud;
}
static inline void sr_pick_string(const char *t, const char *i,
                                  void *cb, void *ud) {
  (void)t; (void)i; (void)cb; (void)ud;
}
#endif

#endif
