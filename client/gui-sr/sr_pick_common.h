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
  Shared (id, label) accumulator for sr_pick_enum callers.

  Several SR dialogs (infra, messages, goto, find, ...) build a list of
  (int id, UTF-8 label) tuples and hand the labels to sr_pick_enum.
  They all used to open-code the same parallel-array pattern + free
  helper. This header exposes a reusable replacement.
*/

#ifndef FC__SR_PICK_COMMON_H
#define FC__SR_PICK_COMMON_H

#include "support.h"            /* bool */

struct sr_pick_list {
  int n;
  int cap;
  int *ids;
  char **labels;
  const char **label_ptrs;      /* parallel to labels, for sr_pick_enum */
};

/* Allocates the three parallel arrays with capacity `cap`. */
void sr_pick_list_init(struct sr_pick_list *list, int cap);

/* Appends (id, label). Duplicates `label`; caller retains its buffer.
   Returns FALSE (and is a no-op) once the list is full. */
bool sr_pick_list_append(struct sr_pick_list *list, int id,
                         const char *label);

/* Frees the owned labels and the three arrays. Does not free the struct
   itself. Safe to call twice. */
void sr_pick_list_free(struct sr_pick_list *list);

/* Fills `list` with cities known to client_player(): own cities first
   ("Name"), then foreign cities ("Name (nation)"). Stops silently if
   capacity is reached. Returns TRUE iff at least one city was added. */
bool sr_pick_list_fill_known_cities(struct sr_pick_list *list);

#endif /* FC__SR_PICK_COMMON_H */
