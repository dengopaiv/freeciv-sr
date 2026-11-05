/***********************************************************************
 Freeciv - Copyright (C) 1996 - A Kjeldberg, L Gregersen, P Unold
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2, or (at your option)
   any later version.
***********************************************************************/

#ifdef HAVE_CONFIG_H
#include <fc_config.h>
#endif

#include <stdio.h>

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "nation.h"
#include "player.h"
#include "spaceship.h"

/* client */
#include "client_main.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_speech.h"

/* gui main header */
#include "gui_sr.h"

#include "spaceshipdlg.h"

/* ---------------------------------------------------------------------
   Spaceship awareness for the SR client.

   The reference clients open a per-player popup with a 3D rendering
   of the spaceship being built. For SR we summarize the relevant
   numbers as a one-line announce on each meaningful change (any new
   structural / component / module placed, fuel/propulsion change, or
   state transition launched/arrived). De-duplicated so a refresh that
   carries no new information stays silent.
   ------------------------------------------------------------------ */

#define MAX_TRACKED 32

struct ship_snapshot {
  int player_id;
  int structurals;
  int components;
  int modules;
  int fuel;
  int propulsion;
  int state;
  bool used;
};

static struct ship_snapshot snapshots[MAX_TRACKED];

static struct ship_snapshot *snapshot_for(int player_id)
{
  int i, free_slot = -1;

  for (i = 0; i < MAX_TRACKED; i++) {
    if (snapshots[i].used && snapshots[i].player_id == player_id) {
      return &snapshots[i];
    }
    if (!snapshots[i].used && free_slot < 0) {
      free_slot = i;
    }
  }
  if (free_slot < 0) {
    return NULL;
  }
  snapshots[free_slot].used = TRUE;
  snapshots[free_slot].player_id = player_id;
  snapshots[free_slot].structurals = -1;
  snapshots[free_slot].components = -1;
  snapshots[free_slot].modules = -1;
  snapshots[free_slot].fuel = -1;
  snapshots[free_slot].propulsion = -1;
  snapshots[free_slot].state = -1;
  return &snapshots[free_slot];
}

static const char *state_name(enum spaceship_state s)
{
  switch (s) {
    case SSHIP_NONE:     return _("not started");
    case SSHIP_STARTED:  return _("under construction");
    case SSHIP_LAUNCHED: return _("launched");
    case SSHIP_ARRIVED:  return _("arrived");
    default:             return "?";
  }
}

/**********************************************************************//**
  Popup (or raise) the spaceship dialog for the given player.
**************************************************************************/
void popup_spaceship_dialog(struct player *pplayer)
{
  /* Read-only snapshot — same shape as a refresh. */
  refresh_spaceship_dialog(pplayer);
}

/**********************************************************************//**
  Close the spaceship dialog for the given player.
**************************************************************************/
void popdown_spaceship_dialog(struct player *pplayer)
{
  (void) pplayer;   /* No window to close — refresh is announce-only. */
}

/**********************************************************************//**
  Refresh (update) the spaceship dialog for the given player.
**************************************************************************/
void refresh_spaceship_dialog(struct player *pplayer)
{
  const struct player_spaceship *s;
  struct ship_snapshot *prev;
  char buf[320];
  bool is_self;

  if (pplayer == NULL) {
    return;
  }
  s = &pplayer->spaceship;
  prev = snapshot_for(player_number(pplayer));
  if (prev == NULL) {
    return;
  }

  /* Skip when nothing meaningful changed since last refresh. */
  if (prev->structurals == s->structurals
      && prev->components == s->components
      && prev->modules    == s->modules
      && prev->fuel       == s->fuel
      && prev->propulsion == s->propulsion
      && prev->state      == (int) s->state) {
    return;
  }

  is_self = (pplayer == client_player());

  if (s->state == SSHIP_LAUNCHED && prev->state != SSHIP_LAUNCHED) {
    if (is_self) {
      fc_snprintf(buf, sizeof(buf),
                  _("Your spaceship has launched. Travel time %.1f years,"
                    " success chance %.0f%%."),
                  s->travel_time, s->success_rate * 100.0);
    } else {
      fc_snprintf(buf, sizeof(buf),
                  _("%s spaceship has launched. Travel time %.1f years,"
                    " success chance %.0f%%."),
                  nation_adjective_for_player(pplayer),
                  s->travel_time, s->success_rate * 100.0);
    }
  } else if (s->state == SSHIP_ARRIVED && prev->state != SSHIP_ARRIVED) {
    if (is_self) {
      fc_snprintf(buf, sizeof(buf), _("Your spaceship has arrived."));
    } else {
      fc_snprintf(buf, sizeof(buf), _("%s spaceship has arrived."),
                  nation_adjective_for_player(pplayer));
    }
  } else if (is_self) {
    fc_snprintf(buf, sizeof(buf),
                _("Spaceship: %s. Structurals %d, components %d,"
                  " modules %d, fuel %d, propulsion %d."),
                state_name(s->state), s->structurals, s->components,
                s->modules, s->fuel, s->propulsion);
  } else {
    fc_snprintf(buf, sizeof(buf),
                _("%s spaceship: %s. Structurals %d, components %d,"
                  " modules %d."),
                nation_adjective_for_player(pplayer),
                state_name(s->state), s->structurals, s->components,
                s->modules);
  }
  sr_log_append(buf);
  /* Don't interrupt for spaceship updates — they fire alongside other
     turn-end announcements. Speak when nothing else is queued. */
  sr_say(buf, 0);

  prev->structurals = s->structurals;
  prev->components  = s->components;
  prev->modules     = s->modules;
  prev->fuel        = s->fuel;
  prev->propulsion  = s->propulsion;
  prev->state       = s->state;
}
