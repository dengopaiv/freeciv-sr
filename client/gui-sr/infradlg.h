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

#ifndef FC__INFRADLG_H
#define FC__INFRADLG_H

#include "dialogs_g.h"             /* gui_update_infra_dialog */

/* Open the infra placement picker. Uses the current target tile (review
   cursor / focus unit's tile) and prompts for which extra to place. */
void infra_dialog_popup(void);

#endif  /* FC__INFRADLG_H */
