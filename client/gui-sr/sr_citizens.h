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

#ifndef FC__SR_CITIZENS_H
#define FC__SR_CITIZENS_H

struct city;

/* Read-only citizen breakdown for a city: total population, mood
   tally, specialist counts per type, and nationality breakdown.
   Logs to the event window and announces a one-line summary. */
void sr_citizens_open(struct city *pcity);

#endif  /* FC__SR_CITIZENS_H */
