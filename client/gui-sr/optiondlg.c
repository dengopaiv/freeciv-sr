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
#include "sr_optdlg.h"

#include "optiondlg.h"

void option_dialog_popup(const char *name, const struct option_set *poptset)
{
  (void)name;
  (void)poptset;
  sr_optdlg_open();
}

void option_dialog_popdown(const struct option_set *poptset)
{
  (void)poptset;
  if (sr_optdlg_is_open()) {
    sr_optdlg_close();
  }
}

/* Per-option callbacks: a full refresh is cheap enough for v1. */
void option_gui_update(struct option *poption)
{
  (void)poption;
  sr_optdlg_refresh();
}

void option_gui_add(struct option *poption)
{
  (void)poption;
  sr_optdlg_refresh();
}

void option_gui_remove(struct option *poption)
{
  (void)poption;
  /* No-op: option is about to be destroyed; the dialog will rebuild on
     next update if still open. */
}
