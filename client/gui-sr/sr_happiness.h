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

#ifndef FC__SR_HAPPINESS_H
#define FC__SR_HAPPINESS_H

struct city;

/* Read-only happiness breakdown for a city. Logs each contributor
   category (cities, luxuries, buildings, nationality, units, wonders)
   to the event log and announces the final happy/content/unhappy/angry
   tally. */
void sr_happiness_open(struct city *pcity);

#endif  /* FC__SR_HAPPINESS_H */
