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
  Tile description builders for gui-sr. Compose existing localized
  text helpers into screen-reader friendly utterances.
*/

#ifndef FC__SR_TILE_H
#define FC__SR_TILE_H

#include "fc_types.h"

/* Speak a full or terse readout of ptile via sr_say.
   terse: TRUE = one-line condensed form; FALSE = thorough
   (flattened popup_info_text). Interrupts any pending speech. */
void sr_tile_say(const struct tile *ptile, bool terse);

/* Speak just coordinates / continent of ptile. */
void sr_tile_say_where(const struct tile *ptile);

#endif  /* FC__SR_TILE_H */
