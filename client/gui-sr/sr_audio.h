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
  Dedicated Sound settings dialog for gui-sr.

  A keyboard-only menu-of-pickers: the root sr_pick_enum lists every
  sound-related option with its current value baked into the label, plus
  entries for the tag inspector (T4) and the soundspec auditor (T8).
  Committing a row either toggles a boolean in place, opens a sub-picker
  (sr_pick_int for volume, sr_pick_enum for soundset/musicset/plugin and
  the mute-prefix editor), or launches a separate dialog. Sub-pickers
  return focus here by reopening the root dialog.

  Changes to default_sound_set_name are followed by an explicit
  audio_restart() because that option has no val_change_callback; music
  and volume already have their own callbacks in options.c.

  For T3 (event-to-speech captions) and T5 (per-event mute prefixes),
  sr_audio_say_for_tag() is the hook invoked from audio_play_sound in
  client/audio.c. Returning FALSE means "suppress this sound" (T5).
*/

#ifndef FC__SR_AUDIO_H
#define FC__SR_AUDIO_H

#include "support.h"            /* bool */

/* Open (or reopen) the Sound settings root dialog. Safe to call any
   time after audio_init(). */
void sr_audio_open(void);

/* Open the soundset tag inspector (T4). Lists every tag in the active
   soundspec; Enter plays via audio_play_sound(). */
void sr_audio_tag_inspector_open(void);

/* Audit the active soundspec against the known code-side event tags
   (T8). Speaks a short summary and appends a per-tag report to the
   log. */
void sr_audio_audit(void);

/* Emit a scaffold .soundspec with every known tag commented out, so
   authors have a complete starting point (T8). Writes to `path`;
   returns TRUE on success. */
bool sr_audio_scaffold_write(const char *path);

/* T3/T5 hook: called by audio.c on every audio_play_sound(tag, ...).
   Returns TRUE if the caller should proceed with normal playback,
   FALSE if playback should be suppressed (mute-prefix match). Also
   emits a spoken caption when sr_speak_sound_events is enabled. */
bool sr_audio_say_for_tag(const char *tag);

/* Look up the hand-curated human caption for a sound tag. Returns NULL
   if the tag has no entry in the exact or prefix tables. The returned
   pointer is into a static table and must not be freed. */
const char *sr_audio_caption_for_tag(const char *tag);

#endif  /* FC__SR_AUDIO_H */
