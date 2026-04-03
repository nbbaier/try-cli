#include "fuzzy.h"
#include "tui.h"
#include <ctype.h>
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

// Helper to check for date prefix (YYYY-MM-DD-)
static bool has_date_prefix_text(const char *text, size_t len) {
  return (len >= 11 && isdigit((unsigned char)text[0]) &&
          isdigit((unsigned char)text[1]) &&
          isdigit((unsigned char)text[2]) &&
          isdigit((unsigned char)text[3]) && text[4] == '-' &&
          isdigit((unsigned char)text[5]) &&
          isdigit((unsigned char)text[6]) && text[7] == '-' &&
          isdigit((unsigned char)text[8]) &&
          isdigit((unsigned char)text[9]) && text[10] == '-');
}

static void lowercase_ascii_in_place(zstr *s) {
  char *data = zstr_data(s);
  size_t len = zstr_len(s);

  for (size_t i = 0; i < len; i++) {
    data[i] = (char)tolower((unsigned char)data[i]);
  }
}

void fuzzy_prepare_entry(TryEntry *entry) {
  const char *text = zstr_cstr(&entry->name);

  zstr_free(&entry->name_lower);
  entry->name_lower = zstr_dup(&entry->name);
  lowercase_ascii_in_place(&entry->name_lower);
  entry->name_len = zstr_len(&entry->name);
  entry->has_date_prefix = has_date_prefix_text(text, entry->name_len);
}

static void fuzzy_render_plain(TryEntry *entry) {
  const char *text = zstr_cstr(&entry->name);

  if (entry->has_date_prefix) {
    TuiStyleString ss = tui_wrap_zstr(&entry->rendered);
    tui_push(&ss, TUI_DARK);
    zstr_cat_len(&entry->rendered, text, 11);
    tui_pop(&ss);
    zstr_cat(&entry->rendered, text + 11);
  } else {
    zstr_cat(&entry->rendered, text);
  }
}

void fuzzy_match_with_context(TryEntry *entry, const FuzzyMatchContext *ctx) {
  if (entry->name_len == 0 && zstr_len(&entry->name) > 0 &&
      zstr_len(&entry->name_lower) == 0) {
    fuzzy_prepare_entry(entry);
  }

  const time_t now = (ctx && ctx->now != 0) ? ctx->now : time(NULL);
  const int query_len = ctx ? ctx->query_len : 0;
  const char *query_lower = (ctx && ctx->query_lower) ? ctx->query_lower : "";

  entry->score = 0.0f;
  tui_start_zstr(&entry->rendered);

  const char *text = zstr_cstr(&entry->name);
  const char *text_lower = zstr_cstr(&entry->name_lower);
  bool has_date = entry->has_date_prefix;

  if (query_len == 0) {
    fuzzy_render_plain(entry);

    double hours_since_access = difftime(now, entry->mtime) / 3600.0;
    entry->score += (float)(3.0 / sqrt(hours_since_access + 1));
    return;
  }

  TuiStyleString ss = tui_wrap_zstr(&entry->rendered);
  const char *t_ptr = text_lower;
  const char *orig_ptr = text;

  int query_idx = 0;
  int last_pos = -1;
  int current_pos = 0;
  bool in_date_section = false;
  float fuzzy_score = 0.0f;

  while (*t_ptr) {
    if (has_date && current_pos == 0) {
      tui_push(&ss, TUI_DARK);
      in_date_section = true;
    }

    if (query_idx < query_len && *t_ptr == query_lower[query_idx]) {
      fuzzy_score += 1.0f;

      if (current_pos == 0 || !isalnum((unsigned char)*(t_ptr - 1))) {
        fuzzy_score += 1.0f;
      }

      if (last_pos >= 0) {
        int gap = current_pos - last_pos - 1;
        fuzzy_score += (float)(2.0 / sqrt(gap + 1));
      }

      last_pos = current_pos;
      query_idx++;

      tui_push(&ss, TUI_MATCH);
      tui_putc(&ss, *orig_ptr);
      tui_pop(&ss);
    } else {
      tui_putc(&ss, *orig_ptr);
    }

    if (has_date && current_pos == 10 && in_date_section) {
      tui_pop(&ss);
      in_date_section = false;
    }

    t_ptr++;
    orig_ptr++;
    current_pos++;
  }

  if (query_idx < query_len) {
    entry->score = 0.0f;
    return;
  }

  if (last_pos >= 0) {
    fuzzy_score *= ((float)query_len / (last_pos + 1));
  }

  fuzzy_score *= (10.0f / ((float)entry->name_len + 10.0f));

  entry->score = fuzzy_score + (has_date ? 2.0f : 0.0f);

  double hours_since_access = difftime(now, entry->mtime) / 3600.0;
  entry->score += (float)(3.0 / sqrt(hours_since_access + 1));
}

void fuzzy_match(TryEntry *entry, const char *query) {
  Z_CLEANUP(zstr_free) zstr query_lower = query ? zstr_from(query) : zstr_init();
  if (query && *query) {
    lowercase_ascii_in_place(&query_lower);
  }

  FuzzyMatchContext ctx = {
      .query_lower = zstr_cstr(&query_lower),
      .query_len = (int)zstr_len(&query_lower),
      .now = time(NULL),
  };

  fuzzy_match_with_context(entry, &ctx);
}

float calculate_score(const char *text, const char *query, time_t mtime) {
  TryEntry tmp = {0};
  tmp.name = zstr_from(text);
  tmp.name_lower = zstr_init();
  tmp.rendered = zstr_init();
  tmp.path = zstr_init();
  tmp.mtime = mtime;

  fuzzy_prepare_entry(&tmp);
  fuzzy_match(&tmp, query);

  float score = tmp.score;

  zstr_free(&tmp.name);
  zstr_free(&tmp.name_lower);
  zstr_free(&tmp.rendered);
  zstr_free(&tmp.path);

  return score;
}
