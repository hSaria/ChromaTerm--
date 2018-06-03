/* This program is protected under the GNU GPL (See COPYING) */

#include "defs.h"

struct color_type {
  char *name;
  char *code;
};

struct color_type color_table[] = {
    {"bold", "<188>"},         {"dim", "<288>"},
    {"underscore", "<488>"},   {"blink", "<588>"},
    {"b azure", "<ABD>"},      {"b black", "<880>"},
    {"b blue", "<884>"},       {"b cyan", "<886>"},
    {"b ebony", "<G04>"},      {"b green", "<882>"},
    {"b jade", "<ADB>"},       {"b lime", "<BDA>"},
    {"b magenta", "<885>"},    {"b orange", "<DBA>"},
    {"b pink", "<DAB>"},       {"b red", "<881>"},
    {"b silver", "<CCC>"},     {"b tan", "<CBA>"},
    {"b violet", "<BAD>"},     {"b white", "<887>"},
    {"b yellow", "<883>"},     {"azure", "<abd>"},
    {"black", "<808>"},        {"blue", "<848>"},
    {"cyan", "<868>"},         {"ebony", "<g04>"},
    {"green", "<828>"},        {"jade", "<adb>"},
    {"light azure", "<acf>"},  {"light ebony", "<bbb>"},
    {"light jade", "<afc>"},   {"light lime", "<cfa>"},
    {"light orange", "<fca>"}, {"light pink", "<fac>"},
    {"light silver", "<eee>"}, {"light tan", "<eda>"},
    {"light violet", "<caf>"}, {"lime", "<bda>"},
    {"magenta", "<858>"},      {"orange", "<dba>"},
    {"pink", "<dab>"},         {"red", "<818>"},
    {"silver", "<ccc>"},       {"tan", "<cba>"},
    {"violet", "<bad>"},       {"white", "<878>"},
    {"yellow", "<838>"},       {"", "<088>"}};

/* Used to search for the start of a color */

void check_highlights(char *string) {
  int i;

  /* Apply from the top since the bottom ones may not match after the action of
   * one of the top ones is applied */
  for (i = 0; i < gd.highlights_used; i++) {
    char *pti = string;
    struct regex_r res = regex_compare(gd.highlights[i]->compiled_regex, pti);

    if (res.start != -1) {
      char output[INPUT_MAX * 2];
      *output = 0;

      do {
        strncat(output, pti, res.start);                   /* Before Match */
        strcat(output, gd.highlights[i]->compiled_action); /* Action */
        strncat(output, pti += res.start, res.end - res.start); /* Match */
        strcat(output, "\033[0m");                              /* Reset */

        pti += res.end - res.start; /* Move pto to after the match */

        res = regex_compare(gd.highlights[i]->compiled_regex, pti);
      } while (res.start != -1);

      /* Add the remainder of the string and then copy it to*/
      strcat(output, pti);
      strcpy(string, output);
    }
  }
}

int find_highlight_index(char *condition) {
  int i;
  for (i = 0; i < gd.highlights_used; i++) {
    if (!strcmp(condition, gd.highlights[i]->condition)) {
      return i;
    }
  }
  return -1;
}

int get_highlight_codes(char *string, char *result) {
  int match_found = FALSE;
  *result = 0;

  while (isspace((int)*string)) {
    string++;
  }

  if (string[0] == '<' && string[4] == '>') {
    substitute(string, result);
    return TRUE;
  }

  while (*string) {
    if (isalpha((int)*string)) {
      int cnt;
      for (cnt = 0; *color_table[cnt].name; cnt++) {
        if (is_abbrev(color_table[cnt].name, string)) {
          substitute(color_table[cnt].code, result);

          match_found = TRUE;
          result += strlen(result);
          break;
        }
      }

      if (*color_table[cnt].name == 0) {
        return FALSE;
      }

      /* Skip until the next action (maybe there are multiple colors) */
      string += strlen(color_table[cnt].name);
    } else {
      string++;
    }

    while (isspace((int)*string) || *string == ',') {
      string++;
    }
  }

  if (match_found) {
    return TRUE;
  } else {
    return FALSE;
  }
}

struct regex_r regex_compare(PCRE_CODE *compiled_regex, char *str) {
  struct regex_r res;
#ifdef HAVE_PCRE2_H
  PCRE2_SIZE *res_pos;
  pcre2_match_data *match =
      pcre2_match_data_create_from_pattern(compiled_regex, NULL);

  if (pcre2_match(compiled_regex, (PCRE2_SPTR)str, (int)strlen(str), 0, 0,
                  match, NULL) <= 0) {
    pcre2_match_data_free(match);
    res.start = -1;
    return res;
  }

  res_pos = pcre2_get_ovector_pointer(match);

  res.start = (int)res_pos[0];
  res.end = (int)res_pos[1];

  pcre2_match_data_free(match);
#else
  int res_pos[600];

  if (pcre_exec(compiled_regex, NULL, str, (int)strlen(str), 0, 0, res_pos,
                600) <= 0) {
    res.start = -1;
    return res;
  }

  res.start = (int)res_pos[0];
  res.end = (int)res_pos[1];
#endif

  if (res.start > res.end) {
    res.start = -1;
    return res;
  }

  return res;
}

void highlight(char *args) {
  char condition[BUFFER_SIZE], action[BUFFER_SIZE], priority[BUFFER_SIZE];

  args = get_arg(args, condition);
  args = get_arg(args, action);
  get_arg(args, priority);

  if (*priority == 0) {
    strcpy(priority, "1000");
  }

  if (*condition == 0 || *action == 0) {
    if (gd.highlights_used == 0) {
      err_printf("HIGHLIGHT: No rules configured");
    } else {
      int i;
      for (i = 0; i < gd.highlights_used; i++) {
        err_printf("HIGHLIGHT "
                   "\033[1;31m{\033[0m%s\033[1;31m}\033[1;36m "
                   "\033[1;31m{\033[0m%s\033[1;31m}\033[1;36m "
                   "\033[1;31m{\033[0m%s\033[1;31m}\033[0m",
                   gd.highlights[i]->condition, gd.highlights[i]->action,
                   gd.highlights[i]->priority);
      }
    }
  } else {
    char temp[BUFFER_SIZE];
    if (get_highlight_codes(action, temp) == FALSE) {
      err_printf("ERROR: Invalid color code {%s}; see `man ct`", action);
    } else {
      PCRE_ERR_P err_p;
      struct highlight *highlight;
      int err_n, index, insert_index;

      /* Remove if already exists */
      if ((index = find_highlight_index(condition)) != -1) {
        unhighlight(gd.highlights[index]->condition);
      }

      highlight = (struct highlight *)calloc(1, sizeof(struct highlight));

      strcpy(highlight->condition, condition);
      strcpy(highlight->action, action);
      strcpy(highlight->priority, priority);

      get_highlight_codes(action, highlight->compiled_action);

      PCRE_COMPILE(highlight->compiled_regex, condition, &err_n, &err_p);

      if (highlight->compiled_regex == NULL) {
        err_printf("WARNING: Couldn't compile regex at %i: %s", err_n, err_p);
      }

      /* Find the insertion index; start at the bottom of the list */
      insert_index = gd.highlights_used - 1;

      /* Highest value priority is at the bottom of the list (highest index) */
      while (insert_index > -1) {
        double diff =
            atof(priority) - atof(gd.highlights[insert_index]->priority);

        if (diff >= 0) {
          insert_index++; /* Same priority or higher; insert after */
          break;
        }
        insert_index--; /* Our priority is less than insert_index's priorty */
      }

      /* index must be 0 or higher */
      index = 0 > insert_index ? 0 : insert_index;

      gd.highlights_used++;

      /* Resize if full; make it twice as big */
      if (gd.highlights_used == gd.highlights_size) {
        gd.highlights_size *= 2;

        gd.highlights = (struct highlight **)realloc(
            gd.highlights, gd.highlights_size * sizeof(struct highlight *));
      }

      memmove(&gd.highlights[index + 1], &gd.highlights[index],
              (gd.highlights_used - index) * sizeof(struct highlight *));

      gd.highlights[index] = highlight;
    }
  }
}

/* copy *string into *result, but replace colors with terminal codes */
void substitute(char *string, char *result) {
  char *pti = string, *pto = result;

  while (*pti) {
    if (pti[0] == '<' && pti[4] == '>') {
      if (isdigit((int)pti[1]) && isdigit((int)pti[2]) &&
          isdigit((int)pti[3])) {
        if (pti[1] != '8' || pti[2] != '8' || pti[3] != '8') {
          *pto++ = ESCAPE;
          *pto++ = '[';

          switch (pti[1]) {
          case '8':
            break;
          default:
            *pto++ = pti[1];
            *pto++ = ';';
          }
          switch (pti[2]) {
          case '8':
            break;
          default:
            *pto++ = '3';
            *pto++ = pti[2];
            *pto++ = ';';
            break;
          }
          switch (pti[3]) {
          case '8':
            break;
          default:
            *pto++ = '4';
            *pto++ = pti[3];
            *pto++ = ';';
            break;
          }
          pto--;
          *pto++ = 'm';
        }
        pti += 5;
      } else if ((pti[1] >= 'a' && pti[1] <= 'f' && pti[2] >= 'a' &&
                  pti[2] <= 'f' && pti[3] >= 'a' && pti[3] <= 'f') ||
                 (pti[1] >= 'A' && pti[1] <= 'F' && pti[2] >= 'A' &&
                  pti[2] <= 'F' && pti[3] >= 'A' && pti[3] <= 'F') ||
                 ((pti[1] == 'g' || pti[1] == 'G') && isdigit((int)pti[2]) &&
                  isdigit((int)pti[3]))) {
        int cnt, grayscale = (pti[1] == 'g' || pti[1] == 'G');
        char g = (pti[1] >= 'a' && pti[1] <= 'f') || pti[1] == 'g' ? '3' : '4';

        *pto++ = ESCAPE;
        *pto++ = '[';
        *pto++ = g;
        *pto++ = '8';
        *pto++ = ';';
        *pto++ = '5';
        *pto++ = ';';

        if (grayscale) {
          cnt = 232 + (pti[2] - '0') * 10 + (pti[3] - '0');
        } else {
          g = g == '3' ? 'a' : 'A'; /* 3 means foreground */
          cnt = 16 + (pti[1] - g) * 36 + (pti[2] - g) * 6 + (pti[3] - g);
        }

        *pto++ = '0' + cnt / 100;
        *pto++ = '0' + cnt % 100 / 10;
        *pto++ = '0' + cnt % 10;
        *pto++ = 'm';

        pti += 5;
      } else {
        *pto++ = *pti++;
      }
    } else {
      *pto++ = *pti++;
    }
  }

  *pto = 0;
}

void unhighlight(char *args) {
  char condition[BUFFER_SIZE];
  int index;

  get_arg(args, condition);

  if (*condition == 0) {
    err_printf("SYNTAX: UNHIGHLIGHT {CONDITION}");

  } else if ((index = find_highlight_index(condition)) != -1) {
    struct highlight *highlight = gd.highlights[index];

    if (highlight->compiled_regex != NULL) {
      PCRE_FREE(highlight->compiled_regex);
    }

    free(highlight);

    memmove(&gd.highlights[index], &gd.highlights[index + 1],
            (gd.highlights_used - index) * sizeof(struct highlight *));

    gd.highlights_used--;
  } else {
    err_printf("ERROR: Highlight rule not found");
  }
}
