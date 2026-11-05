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
#include "sr_citydlg.h"
#include "sr_tree.h"

#include "citydlg.h"

/**********************************************************************//**
  Pop up (or bring to the front) a dialog for the given city.  It may or
  may not be modal.
**************************************************************************/
void gui_real_city_dialog_popup(struct city *pcity)
{
  sr_citydlg_open(pcity);
}

/**********************************************************************//**
  Close the dialog for the given city.
**************************************************************************/
void gui_popdown_city_dialog(struct city *pcity)
{
  (void)pcity;
  if (sr_citydlg_is_open()) {
    sr_citydlg_close();
  }
}

/**********************************************************************//**
  Close the dialogs for all cities.
**************************************************************************/
void gui_popdown_all_city_dialogs(void)
{
  /* PORTME */
}

/**********************************************************************//**
  Refresh (update) all data for the given city's dialog.
**************************************************************************/
void gui_real_city_dialog_refresh(struct city *pcity)
{
  (void)pcity;
  if (sr_citydlg_is_open()) {
    sr_citydlg_refresh();
  }
}

/**********************************************************************//**
  Update city dialogs when the given unit's status changes.  This
  typically means updating both the unit's home city (if any) and the
  city in which it is present (if any).
**************************************************************************/
void gui_refresh_unit_city_dialogs(struct unit *punit)
{
  (void)punit;
  /* PORTME */
}

/**********************************************************************//**
  Return whether the dialog for the given city is open.
**************************************************************************/
bool gui_city_dialog_is_open(struct city *pcity)
{
  (void)pcity;
  return sr_citydlg_is_open() ? TRUE : FALSE;
}
