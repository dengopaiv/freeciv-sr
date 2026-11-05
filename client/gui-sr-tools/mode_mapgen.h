/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
***********************************************************************/

#ifndef FC__MODE_MAPGEN_H
#define FC__MODE_MAPGEN_H

/* Open the random-map-generator picker chain. When the user finishes
   (or cancels), the tool returns to the top-level mode picker via
   tools_open_mode_picker(). */
void mode_mapgen_open(void);

/* Defined in main.c — re-show the mode picker after a sub-mode finishes. */
void tools_open_mode_picker(void);

#endif
