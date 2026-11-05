/***********************************************************************
 Freeciv - Copyright (C) 1996-2004 - The Freeciv Team
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

/* client */
#include "chatline_common.h"   /* send_chat */
#include "connectdlg_g.h"

/* gui-sr */
#include "sr_citydlg.h"
#include "sr_loadgame.h"
#include "sr_mapedit.h"
#include "sr_newgame.h"
#include "sr_play.h"
#include "sr_speech.h"

#include "pages.h"

static enum client_pages current_page = PAGE_MAIN;

/**********************************************************************//**
  Sets the "page" that the client should show.  See also pages_g.h.
**************************************************************************/
void gui_real_set_client_page(enum client_pages page)
{
  const char *name;

  current_page = page;
  switch (page) {
  case PAGE_MAIN:
    name = "Main menu.";
    /* Returning to main menu means the game is being torn down.
       Drop any SR dialog statics that still hold pointers into the
       ruleset / city data that's about to be freed. */
    sr_citydlg_shutdown();
    break;
  case PAGE_START:    name = "Pregame lobby. Type begin to start."; break;
  case PAGE_SCENARIO: name = "Scenario selection.";             break;
  case PAGE_LOAD:     name = "Load game.";                      break;
  case PAGE_NETWORK:  name = "Network game.";                   break;
  case PAGE_GAME:
    name = "Game started. Press F1 for map view.";
    /* Give focus to the map pane so arrow keys move units. */
    if (sr_play_hwnd() != NULL) {
      SetFocus(sr_play_hwnd());
    }
    sr_mapedit_flush_at_game_if_pending();
    break;
  default:            name = NULL;                              break;
  }
  if (name != NULL) {
    sr_say(name, 1);
  }
  if (page == PAGE_START) {
    sr_newgame_flush_if_pending();
    sr_mapedit_flush_at_pregame_if_pending();
    /* If we just spawned a server with --file (load-game flow), the
       server is already loaded but no handle_game_load packet was sent
       (no client was attached at server-init time). Auto-/take a player
       to attach so the user lands on a usable slot. */
    if (sr_loadgame_consume_take_after_load()) {
      send_chat("/take -");
    }
  }
  sr_loadgame_flush_if_pending((int)page);
}

/**********************************************************************//**
  Set the list of available rulesets.  The default ruleset should be
  "default", and if the user changes this then set_ruleset() should be
  called.
**************************************************************************/
void gui_set_rulesets(int num_rulesets, char **rulesets)
{
  /* Hand the authoritative server list to the New Game dialog, in case
     it's currently open. */
  sr_newgame_set_rulesets(num_rulesets, rulesets);
}

/**********************************************************************//**
  Returns current client page
**************************************************************************/
enum client_pages gui_get_current_client_page(void)
{
  return current_page;
}

/**********************************************************************//**
  Update the start page.
**************************************************************************/
void update_start_page(void)
{
  /* Pregame player list changes are already spoken through the chat
     channel; no dedicated UI to refresh here. */
}
