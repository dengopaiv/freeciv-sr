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

#ifndef FC__SR_CMA_H
#define FC__SR_CMA_H

struct city;

/* Citizen Management Agent frontend. Opens a keyboard-driven action
   menu (view / apply preset / save / delete / edit / release) and
   commits via the existing cma_core / cmafec API. */
void sr_cma_open(struct city *pcity);

#endif  /* FC__SR_CMA_H */
