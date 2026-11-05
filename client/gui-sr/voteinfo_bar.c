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

/* client */
#include "voteinfo.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_speech.h"

/* gui main header */
#include "gui_sr.h"

#include "voteinfo_bar.h"

/* ---------------------------------------------------------------------
   Server vote awareness for the SR client.

   The voteinfo subsystem fires voteinfo_gui_update() on every state
   change (new vote, count change, resolution). For an audio-first
   client we only want to speak meaningful transitions, not every
   incremental yes/no count tick — so track the last announced vote
   and its resolution flag here and only speak on change.

   Voting itself is keyboard-driven via the new SR verbs `voteyes`,
   `voteno`, `voteabstain` (registered in sr_cmd.c) which call
   voteinfo_do_vote() on the current vote.
   ------------------------------------------------------------------ */

static int  last_vote_no = -1;
static bool last_resolved = FALSE;

/************************************************************************//**
  Refresh all vote related GUI widgets. Called by the voteinfo module when
  the client receives new vote information from the server.
****************************************************************************/
void voteinfo_gui_update(void)
{
  struct voteinfo *vi = voteinfo_queue_get_current(NULL);
  char buf[640];

  if (vi == NULL) {
    /* No active vote — clear so a future vote with the same number
       (after a wrap) re-announces. */
    last_vote_no = -1;
    last_resolved = FALSE;
    return;
  }

  /* New vote — announce who proposed and what. */
  if (vi->vote_no != last_vote_no) {
    fc_snprintf(buf, sizeof(buf),
                _("Vote %d by %s: %s. Use voteyes, voteno or voteabstain."),
                vi->vote_no, vi->user, vi->desc);
    sr_say(buf, 1);
    sr_log_append(buf);
    last_vote_no = vi->vote_no;
    last_resolved = vi->resolved;
    return;
  }

  /* Same vote — only re-announce when it transitions to resolved. */
  if (vi->resolved && !last_resolved) {
    fc_snprintf(buf, sizeof(buf),
                vi->passed
                  ? _("Vote %d passed: %s")
                  : _("Vote %d failed: %s"),
                vi->vote_no, vi->desc);
    sr_say(buf, 1);
    sr_log_append(buf);
    last_resolved = TRUE;
  }
}
