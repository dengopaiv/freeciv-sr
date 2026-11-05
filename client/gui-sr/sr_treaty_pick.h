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
  Sub-pickers for the treaty window: tech, gold, city.
  On confirm, each dispatches dsend_packet_diplomacy_create_clause_req.
*/

#ifndef FC__SR_TREATY_PICK_H
#define FC__SR_TREATY_PICK_H

#ifdef _WIN32
#include <windows.h>

/* counterpart_plrno = "them" (the other party in the meeting).
   giver_plrno = whoever is giving the tech/gold/city. */
void sr_treaty_pick_tech(int counterpart_plrno, int giver_plrno);
void sr_treaty_pick_gold(int counterpart_plrno, int giver_plrno);
void sr_treaty_pick_city(int counterpart_plrno, int giver_plrno);

#else
static inline void sr_treaty_pick_tech(int c, int g) { (void)c; (void)g; }
static inline void sr_treaty_pick_gold(int c, int g) { (void)c; (void)g; }
static inline void sr_treaty_pick_city(int c, int g) { (void)c; (void)g; }
#endif

#endif
