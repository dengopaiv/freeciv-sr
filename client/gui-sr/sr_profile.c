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

#ifdef _WIN32
#include <windows.h>
#endif

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* utility */
#include "fcintl.h"
#include "shared.h"             /* freeciv_storage_dir, make_dir,
                                   DIR_SEPARATOR */
#include "support.h"            /* fc_snprintf, fc_fopen, fc_strlcpy */

/* client */
#include "options.h"

/* gui-sr */
#include "sr_cmd.h"             /* sr_cmd_dispatch_line */
#include "sr_log.h"
#include "sr_speech.h"

#include "sr_profile.h"

#define SR_PROFILE_EXT ".profile"
#define SR_PROFILE_LINE_MAX 512

/* ------------------------------------------------------------------ */
/* Paths                                                              */
/* ------------------------------------------------------------------ */

static void profile_dir(char *out, size_t out_sz)
{
  fc_snprintf(out, out_sz, "%s" DIR_SEPARATOR "sr-profiles",
              freeciv_storage_dir());
}

static void profile_path(const char *name, char *out, size_t out_sz)
{
  char dir[512];

  profile_dir(dir, sizeof(dir));
  fc_snprintf(out, out_sz, "%s" DIR_SEPARATOR "%s" SR_PROFILE_EXT,
              dir, name);
}

/* Reject names that would let a user escape the profile directory. We
   allow letters, digits, dash, underscore, dot (for multi-part names
   like "work.sounds"). Everything else is rejected. */
static bool valid_profile_name(const char *name)
{
  size_t i;

  if (name == NULL || *name == '\0') {
    return FALSE;
  }
  for (i = 0; name[i] != '\0'; i++) {
    unsigned char c = (unsigned char) name[i];

    if (!(isalnum(c) || c == '-' || c == '_' || c == '.')) {
      return FALSE;
    }
  }
  /* No leading dot — avoids hidden files and "..". */
  if (name[0] == '.') {
    return FALSE;
  }
  return TRUE;
}

static bool ensure_profile_dir(void)
{
  char dir[512];

  profile_dir(dir, sizeof(dir));
  /* make_dir is idempotent — returns TRUE if the directory already
     exists too. */
  return make_dir(dir);
}

/* ------------------------------------------------------------------ */
/* setopt                                                             */
/* ------------------------------------------------------------------ */

static bool parse_bool_value(const char *v, bool *out)
{
  if (v == NULL) { return FALSE; }
  if (!strcmp(v, "on")    || !strcmp(v, "true")
      || !strcmp(v, "1")  || !strcmp(v, "yes")) {
    *out = TRUE;
    return TRUE;
  }
  if (!strcmp(v, "off")   || !strcmp(v, "false")
      || !strcmp(v, "0")  || !strcmp(v, "no")) {
    *out = FALSE;
    return TRUE;
  }
  return FALSE;
}

void sr_profile_setopt(const char *opt_name, const char *value)
{
  struct option *opt;
  char msg[256];

  if (opt_name == NULL || *opt_name == '\0') {
    sr_say(_("Usage: setopt NAME VALUE"), 1);
    return;
  }
  opt = optset_option_by_name(client_optset, opt_name);
  if (opt == NULL) {
    fc_snprintf(msg, sizeof(msg),
                _("Unknown option: %s."), opt_name);
    sr_say(msg, 1);
    sr_log_append(msg);
    return;
  }
  if (!option_is_changeable(opt)) {
    fc_snprintf(msg, sizeof(msg),
                _("Option %s cannot be changed here."), opt_name);
    sr_say(msg, 1);
    return;
  }
  if (value == NULL) {
    value = "";
  }

  /* All client option setters return FALSE both for "no change" AND
     for "value out of range / invalid". We can't distinguish from the
     return alone, so call the setter, then read the value back and
     compare to the intended one. Equal = success; different = the
     set was actually rejected. */

  switch (option_type(opt)) {
    case OT_BOOLEAN: {
      bool b;

      if (!parse_bool_value(value, &b)) {
        fc_snprintf(msg, sizeof(msg),
                    _("Boolean value must be on/off for %s."), opt_name);
        sr_say(msg, 1);
        return;
      }
      option_bool_set(opt, b);
      if (option_bool_get(opt) != b) {
        fc_snprintf(msg, sizeof(msg),
                    _("%s rejected the new value."), opt_name);
        sr_say(msg, 1);
        return;
      }
      fc_snprintf(msg, sizeof(msg), _("%s: %s."),
                  opt_name, b ? "on" : "off");
      break;
    }
    case OT_INTEGER: {
      int v = atoi(value);

      option_int_set(opt, v);
      if (option_int_get(opt) != v) {
        fc_snprintf(msg, sizeof(msg),
                    _("Value out of range for %s: %d (min %d, max %d)."),
                    opt_name, v,
                    option_int_min(opt), option_int_max(opt));
        sr_say(msg, 1);
        return;
      }
      fc_snprintf(msg, sizeof(msg), _("%s: %d."), opt_name, v);
      break;
    }
    case OT_STRING: {
      const char *after;

      option_str_set(opt, value);
      after = option_str_get(opt);
      if (after == NULL || strcmp(after, value) != 0) {
        fc_snprintf(msg, sizeof(msg),
                    _("Invalid value for %s."), opt_name);
        sr_say(msg, 1);
        return;
      }
      fc_snprintf(msg, sizeof(msg), _("%s: \"%s\"."),
                  opt_name, value);
      break;
    }
    case OT_ENUM: {
      const char *after;

      option_enum_set_str(opt, value);
      after = option_enum_get_str(opt);
      if (after == NULL || strcmp(after, value) != 0) {
        fc_snprintf(msg, sizeof(msg),
                    _("%s: %s is not one of the valid values."),
                    opt_name, value);
        sr_say(msg, 1);
        return;
      }
      fc_snprintf(msg, sizeof(msg), _("%s: %s."), opt_name, value);
      break;
    }
    default:
      fc_snprintf(msg, sizeof(msg),
                  _("Option type of %s not supported by setopt."),
                  opt_name);
      sr_say(msg, 1);
      return;
  }

  sr_log_append(msg);
}

/* ------------------------------------------------------------------ */
/* saveprofile                                                        */
/* ------------------------------------------------------------------ */

void sr_profile_save(const char *name)
{
  char path[1024];
  FILE *f;
  int count = 0;
  char msg[256];

  if (!valid_profile_name(name)) {
    sr_say(_("Usage: saveprofile NAME (letters, digits, -, _, .)."), 1);
    return;
  }
  if (!ensure_profile_dir()) {
    sr_say(_("Could not create profile directory."), 1);
    return;
  }
  profile_path(name, path, sizeof(path));
  f = fc_fopen(path, "w");
  if (f == NULL) {
    fc_snprintf(msg, sizeof(msg),
                _("Cannot open %s for writing."), path);
    sr_say(msg, 1);
    return;
  }

  fprintf(f,
          "; freeciv-sr profile: %s\n"
          "; Each line is a command the in-game dispatcher runs at load.\n"
          "; Lines starting with ';' are comments; blank lines are skipped.\n"
          "; Edit freely — e.g. delete rows you don't want to restore.\n\n",
          name);

  options_iterate(client_optset, opt) {
    const char *opt_name = option_name(opt);

    if (opt_name == NULL || !option_is_changeable(opt)) {
      continue;
    }
    switch (option_type(opt)) {
      case OT_BOOLEAN:
        fprintf(f, "setopt %s %s\n",
                opt_name, option_bool_get(opt) ? "on" : "off");
        count++;
        break;
      case OT_INTEGER:
        fprintf(f, "setopt %s %d\n", opt_name, option_int_get(opt));
        count++;
        break;
      case OT_STRING: {
        const char *v = option_str_get(opt);

        fprintf(f, "setopt %s %s\n", opt_name, v != NULL ? v : "");
        count++;
        break;
      }
      case OT_ENUM: {
        const char *v = option_enum_get_str(opt);

        if (v != NULL) {
          fprintf(f, "setopt %s %s\n", opt_name, v);
          count++;
        }
        break;
      }
      default:
        /* OT_BITWISE / OT_FONT / OT_COLOR / OT_VIDEO_MODE are
           deliberately skipped — the setopt command doesn't model
           their value grammar yet. Phase 5 may add selective support. */
        break;
    }
  } options_iterate_end;

  fclose(f);
  fc_snprintf(msg, sizeof(msg),
              _("Saved profile %s (%d options)."), name, count);
  sr_say(msg, 1);
  sr_log_append(msg);
}

/* ------------------------------------------------------------------ */
/* loadprofile                                                        */
/* ------------------------------------------------------------------ */

void sr_profile_load(const char *name)
{
  char path[1024];
  FILE *f;
  char line[SR_PROFILE_LINE_MAX];
  int applied = 0;
  char msg[256];

  if (!valid_profile_name(name)) {
    sr_say(_("Usage: loadprofile NAME (letters, digits, -, _, .)."), 1);
    return;
  }
  profile_path(name, path, sizeof(path));
  f = fc_fopen(path, "r");
  if (f == NULL) {
    fc_snprintf(msg, sizeof(msg), _("Profile not found: %s."), name);
    sr_say(msg, 1);
    return;
  }

  while (fgets(line, sizeof(line), f) != NULL) {
    size_t len = strlen(line);
    size_t i;

    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
      line[--len] = '\0';
    }
    i = 0;
    while (line[i] != '\0' && isspace((unsigned char) line[i])) {
      i++;
    }
    if (line[i] == '\0' || line[i] == ';' || line[i] == '#') {
      continue;
    }
    sr_cmd_dispatch_line(line + i);
    applied++;
  }
  fclose(f);
  fc_snprintf(msg, sizeof(msg),
              _("Loaded profile %s (%d lines)."), name, applied);
  sr_say(msg, 1);
  sr_log_append(msg);
}

/* ------------------------------------------------------------------ */
/* listprofiles                                                       */
/* ------------------------------------------------------------------ */

#ifdef _WIN32
void sr_profile_list(void)
{
  char dir[512];
  wchar_t wpattern[1024];
  WIN32_FIND_DATAW fd;
  HANDLE h;
  int count = 0;
  char msg[256];

  profile_dir(dir, sizeof(dir));
  {
    char pattern[1024];
    int wlen;

    fc_snprintf(pattern, sizeof(pattern),
                "%s" DIR_SEPARATOR "*" SR_PROFILE_EXT, dir);
    wlen = MultiByteToWideChar(CP_UTF8, 0, pattern, -1,
                               wpattern,
                               (int)(sizeof(wpattern)/sizeof(wpattern[0])));
    if (wlen <= 0) {
      sr_say(_("Profile directory path too long."), 1);
      return;
    }
  }
  h = FindFirstFileW(wpattern, &fd);
  if (h == INVALID_HANDLE_VALUE) {
    fc_snprintf(msg, sizeof(msg),
                _("No profiles found in %s."), dir);
    sr_say(msg, 1);
    sr_log_append(msg);
    return;
  }
  sr_log_append(_("-- Profiles --"));
  do {
    char utf8name[256];
    size_t len;

    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      continue;
    }
    if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1,
                            utf8name, sizeof(utf8name),
                            NULL, NULL) <= 0) {
      continue;
    }
    /* Strip the .profile suffix for nicer listing. */
    len = strlen(utf8name);
    if (len > sizeof(SR_PROFILE_EXT) - 1
        && !strcmp(utf8name + len - (sizeof(SR_PROFILE_EXT) - 1),
                   SR_PROFILE_EXT)) {
      utf8name[len - (sizeof(SR_PROFILE_EXT) - 1)] = '\0';
    }
    sr_log_append(utf8name);
    count++;
  } while (FindNextFileW(h, &fd));
  FindClose(h);

  fc_snprintf(msg, sizeof(msg),
              _("%d profile(s) in %s."), count, dir);
  sr_say(msg, 1);
}

#else /* !_WIN32 */

void sr_profile_list(void)
{
  sr_say(_("listprofiles is implemented on Windows only for now."), 1);
}

#endif
