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
  Screen-reader output backend for gui-sr.

  Kept intentionally self-contained (no freeciv headers) so this module can
  later be lifted out as the synth half of a standalone NVDA addon.
*/

#ifndef FC__SR_SPEECH_H
#define FC__SR_SPEECH_H

#ifdef __cplusplus
extern "C" {
#endif

/* Initialise the speech backend. Safe to call multiple times. */
void sr_speech_init(void);

/* Tear down. Safe if init was never called. */
void sr_speech_exit(void);

/* Speak UTF-8 text. If interrupt is non-zero, cancel any pending speech
   before speaking the new message. NULL or empty text is a no-op. */
void sr_say(const char *utf8_text, int interrupt);

/* Cancel any in-flight speech. */
void sr_interrupt(void);

/* Send a braille message (UTF-8). No-op if braille unsupported. */
void sr_braille(const char *utf8_text);

/* Non-zero iff a live screen reader is attached (NVDA detected).
   When zero, sr_say falls back to stderr. */
int sr_has_reader(void);

#ifdef __cplusplus
}
#endif

#endif  /* FC__SR_SPEECH_H */
