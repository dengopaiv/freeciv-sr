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

/* client */
#include "messagewin_common.h"

/* gui main header */
#include "gui_sr.h"

/* gui-sr */
#include "sr_speech.h"

#include "messagewin.h"

/**********************************************************************//**
  Display the message dialog. Optionally raise it.
**************************************************************************/
void meswin_dialog_popup(bool raise)
{
  /* Nothing to raise; we're a stream, not a window. */
}

/**********************************************************************//**
  Return whether the message dialog is open.
**************************************************************************/
bool meswin_dialog_is_open(void)
{
  /* The "dialog" is always open from the core's point of view: messages
     arriving while we're running should be delivered to us. */
  return TRUE;
}

/**********************************************************************//**
  Do the work of updating (populating) the message dialog.

  Called by the core whenever the message queue grows. Speak every
  message we haven't yet announced, tracked by monotonic index.
**************************************************************************/
void real_meswin_dialog_update(void *unused)
{
  static int last_spoken = 0;
  int total = meswin_get_num_messages();
  int i;

  if (total < last_spoken) {
    /* Queue was cleared (new turn / new game). */
    last_spoken = 0;
  }
  for (i = last_spoken; i < total; i++) {
    const struct message *m = meswin_get_message(i);

    if (m != NULL && m->descr != NULL) {
      sr_say(m->descr, 0);
    }
  }
  last_spoken = total;
}
