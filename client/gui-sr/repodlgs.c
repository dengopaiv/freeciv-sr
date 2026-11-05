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

#include <stdlib.h>
#include <string.h>

/* gui main header */
#include "gui_sr.h"

/* utility */
#include "fcintl.h"
#include "support.h"

/* common */
#include "packets.h"
#include "player.h"

/* client */
#include "repodlgs_common.h"

#include "cityrep.h"

/* gui-sr */
#include "sr_economydlg.h"
#include "sr_log.h"
#include "sr_sciencedlg.h"
#include "sr_speech.h"
#include "sr_tree.h"
#include "sr_unitsdlg.h"

#include "repodlgs.h"

/* Local cache for endgame report so per-player updates can echo
   category names. Server sends the report packet first, then one
   player packet per scored player. */
static struct packet_endgame_report endgame_cache;
static bool endgame_cache_valid = FALSE;

/**********************************************************************//**
  Update the science report.
**************************************************************************/
void real_science_report_dialog_update(void *unused)
{
  (void)unused;
  sr_sciencedlg_refresh();
}

/**********************************************************************//**
  Display the science report.  Optionally raise it.
  Typically triggered by F6.
**************************************************************************/
void science_report_dialog_popup(bool raise)
{
  (void)raise;
  sr_sciencedlg_open();
}

/**********************************************************************//**
  Update the economy report.
**************************************************************************/
void real_economy_report_dialog_update(void *unused)
{
  (void)unused;
  sr_economydlg_refresh();
}

/**********************************************************************//**
  Display the economy report.  Optionally raise it.
**************************************************************************/
void economy_report_dialog_popup(bool raise)
{
  (void)raise;
  sr_economydlg_open();
}

/**********************************************************************//**
  Update the units report.
**************************************************************************/
void real_units_report_dialog_update(void *unused)
{
  (void)unused;
  sr_unitsdlg_refresh();
}

/**********************************************************************//**
  Display the units report.  Optionally raise it.
**************************************************************************/
void units_report_dialog_popup(bool raise)
{
  (void)raise;
  sr_unitsdlg_open();
}

/**********************************************************************//**
  Show a dialog with player statistics at endgame.
**************************************************************************/
void endgame_report_dialog_start(const struct packet_endgame_report *packet)
{
  char buf[160];
  int i;

  endgame_cache = *packet;
  endgame_cache_valid = TRUE;

  fc_snprintf(buf, sizeof(buf),
              _("Endgame report: %d player(s), %d categor(y)(ies)."),
              packet->player_num, packet->category_num);
  sr_log_append(buf);
  sr_say(buf, 1);

  for (i = 0; i < packet->category_num; i++) {
    fc_snprintf(buf, sizeof(buf), _("  Category %d: %s"),
                i + 1, packet->category_name[i]);
    sr_log_append(buf);
  }
}

/**********************************************************************//**
  Received endgame report information about single player.
**************************************************************************/
void endgame_report_dialog_player(const struct packet_endgame_player *packet)
{
  const struct player *pplayer = player_by_number(packet->player_id);
  char buf[256];
  int i;

  fc_snprintf(buf, sizeof(buf), _("%s: total score %d%s"),
              pplayer != NULL ? player_name(pplayer) : _("(unknown)"),
              packet->score,
              packet->winner ? _(" — WINNER") : "");
  sr_log_append(buf);
  if (packet->winner) {
    sr_say(buf, 1);
  }

  for (i = 0; i < packet->category_num; i++) {
    const char *cname = (endgame_cache_valid
                         && i < endgame_cache.category_num)
      ? endgame_cache.category_name[i] : _("category");

    fc_snprintf(buf, sizeof(buf), _("    %s: %d"),
                cname, packet->category_score[i]);
    sr_log_append(buf);
  }
}

/**********************************************************************//**
  Resize and redraw the requirement tree.
**************************************************************************/
void science_report_dialog_redraw(void)
{
  /* PORTME */
}
