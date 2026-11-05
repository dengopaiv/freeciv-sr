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

#ifndef FC__SR_RALLYPOINT_H
#define FC__SR_RALLYPOINT_H

/* Open the rally-point picker. Prompts for a city (from player's cities)
   and persistent/one-shot, then sends the rally using the current
   target tile (review cursor or focus unit's tile). */
void sr_rallypoint_open(void);

#endif  /* FC__SR_RALLYPOINT_H */
