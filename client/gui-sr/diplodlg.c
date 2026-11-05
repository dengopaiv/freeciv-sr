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

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

/* gui main header */
#include "gui_sr.h"

/* gui-sr */
#include "sr_diplodlg.h"
#include "sr_treaty.h"

#include "diplodlg.h"

/**********************************************************************//**
  Update a player's acceptance status of a treaty.
**************************************************************************/
void gui_gui_recv_accept_treaty(struct Treaty *ptreaty, struct player *they)
{
  (void)they;
  sr_treaty_refresh(ptreaty);
}

/**********************************************************************//**
  Handle the start of a diplomacy meeting — open the treaty window.
**************************************************************************/
void gui_gui_init_meeting(struct Treaty *ptreaty, struct player *they,
                          struct player *initiator)
{
  (void)initiator;
  sr_treaty_open(ptreaty, they);
}

/**********************************************************************//**
  Update the diplomacy dialog by adding a clause.
**************************************************************************/
void gui_gui_recv_create_clause(struct Treaty *ptreaty, struct player *they)
{
  (void)they;
  sr_treaty_refresh(ptreaty);
}

/**********************************************************************//**
  Update the diplomacy dialog when the meeting is canceled.
**************************************************************************/
void gui_gui_recv_cancel_meeting(struct Treaty *ptreaty, struct player *they,
                                 struct player *initiator)
{
  (void)ptreaty;
  (void)initiator;
  sr_treaty_close_for(they);
}

/**********************************************************************//**
  Update the diplomacy dialog by removing a clause.
**************************************************************************/
void gui_gui_recv_remove_clause(struct Treaty *ptreaty, struct player *they)
{
  (void)they;
  sr_treaty_refresh(ptreaty);
}

/**********************************************************************//**
  Prepare to clause creation or removal.
**************************************************************************/
void gui_gui_prepare_clause_updt(struct Treaty *ptreaty, struct player *they)
{
  (void)ptreaty;
  (void)they;
  /* No-op: refresh happens via recv_create / recv_remove. */
}

/**********************************************************************//**
  Close all open diplomacy dialogs. Called when the client disconnects.
**************************************************************************/
void close_all_diplomacy_dialogs(void)
{
  sr_treaty_close_all();
  sr_diplodlg_close();
}
