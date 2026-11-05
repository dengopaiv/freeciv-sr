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

#include <ctype.h>
#include <string.h>

/* utility */
#include "fcintl.h"
#include "support.h"          /* fc_snprintf */

/* client */
#include "climisc.h"          /* for write_chatline_content */

/* gui main header */
#include "gui_sr.h"

/* gui-sr */
#include "sr_log.h"
#include "sr_speech.h"

#include "chatline.h"

/* Case-insensitive substring. strcasestr is a BSD extension and not
   reliably available across our toolchain, so roll our own. */
static const char *ci_strstr(const char *haystack, const char *needle)
{
  size_t nlen;

  if (haystack == NULL || needle == NULL) {
    return NULL;
  }
  nlen = strlen(needle);
  if (nlen == 0) {
    return haystack;
  }
  for (; *haystack != '\0'; haystack++) {
    size_t i;

    for (i = 0; i < nlen; i++) {
      char a = (char) tolower((unsigned char) haystack[i]);
      char b = (char) tolower((unsigned char) needle[i]);

      if (a == '\0' || a != b) {
        break;
      }
    }
    if (i == nlen) {
      return haystack;
    }
  }
  return NULL;
}

/* Substrings the server emits when a /set or command fails. Collected
   from server/settings.c (Value out of range, validation failed, ...)
   and server/stdinhand.c (Unknown option, is not allowed). A hit bumps
   the chat line from routine narration to an interrupting "Setting
   error" announcement and a clearly-tagged log entry. */
static const char *const REJECT_PATTERNS[] = {
  "out of range",
  "validation failed",
  "Unknown option",
  "Cannot set",
  "is not allowed",
  "not a valid",
  "Missing value",
  "Illegal value",
  "Illegal option",
  "Internal error",
  "setting '",        /* e.g. "Error restoring setting 'X'" */
};
#define N_REJECT_PATTERNS \
  ((int)(sizeof(REJECT_PATTERNS) / sizeof(REJECT_PATTERNS[0])))

static bool looks_like_rejection(const char *s)
{
  int i;

  if (s == NULL || *s == '\0') {
    return FALSE;
  }
  for (i = 0; i < N_REJECT_PATTERNS; i++) {
    if (ci_strstr(s, REJECT_PATTERNS[i]) != NULL) {
      return TRUE;
    }
  }
  return FALSE;
}

/**********************************************************************//**
  Appends the string to the chat output window.  The string should be
  inserted on its own line, although it will have no newline.
**************************************************************************/
void gui_real_output_window_append(const char *astring,
                                   const struct text_tag_list *tags,
                                   int conn_id)
{
  /* Tags carry colour/link formatting that means nothing to a screen
     reader; the plain UTF-8 string is what we want to speak. */
  if (looks_like_rejection(astring)) {
    /* Phase 3: raise the volume on setting-rejection text so it doesn't
       get lost in lobby chat. Interrupt any speech in flight and tag
       the log entry so it stands out in the history. */
    char tagged[1024];

    fc_snprintf(tagged, sizeof(tagged),
                _("Setting error: %s"), astring);
    sr_say(tagged, 1);
    sr_log_append(tagged);
    return;
  }
  sr_say(astring, 0);
}

/**********************************************************************//**
  Get the text of the output window, and call write_chatline_content() to
  log it. SR has no separate chat window — chat is spoken transient and
  the persistent record lives in sr_log. The default-path call to
  write_chatline_content(NULL) is the documented "log to file" behavior
  and is what the user wants here.
**************************************************************************/
void log_output_window(void)
{
  write_chatline_content(NULL);
}

/**********************************************************************//**
  Clear all text from the output window. SR's chat output is spoken and
  doesn't accumulate in a clearable widget; the persistent log lives in
  sr_log and is intentionally not cleared here. No-op.
**************************************************************************/
void clear_output_window(void)
{
}

/**********************************************************************//**
  Got version message from metaserver thread.
**************************************************************************/
void gui_version_message(const char *vertext)
{
  output_window_append(ftc_client, vertext);
}
