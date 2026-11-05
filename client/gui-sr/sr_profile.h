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
  Script-style option profiles for freeciv-sr.

  A profile is a plain-text file of commands the sr_cmd dispatcher
  already understands; loading one is just sr_cmd_dispatch_line() over
  every non-comment line. This keeps the storage format trivial to
  hand-edit and keeps the feature aligned with the command-first
  philosophy of the SR client (see PLAN-OPTION-PROFILES.md).

  Files live in  $FREECIV_STORAGE/sr-profiles/<name>.profile
  where FREECIV_STORAGE is whatever freeciv_storage_dir() resolves to
  (usually ~/.freeciv/).
*/

#ifndef FC__SR_PROFILE_H
#define FC__SR_PROFILE_H

/* Apply one `setopt NAME VALUE` assignment to the running client_optset.
   Supported types: OT_BOOLEAN (on/off/1/0/yes/no/true/false), OT_INTEGER,
   OT_STRING, OT_ENUM. Refuses unknown or non-changeable options with a
   spoken reason. */
void sr_profile_setopt(const char *opt_name, const char *value);

/* Dump every changeable client option to a new profile file. Overwrites
   an existing file with the same name. */
void sr_profile_save(const char *name);

/* Read a profile and feed every non-comment, non-blank line through the
   command dispatcher. Unknown commands in the file are reported but
   don't abort the load. */
void sr_profile_load(const char *name);

/* Speak and log the .profile files present in the profile directory. */
void sr_profile_list(void);

#endif /* FC__SR_PROFILE_H */
