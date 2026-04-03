#ifndef FUZZY_H
#define FUZZY_H

#include "tui.h" // Need full definition of TryEntry
#include <time.h>

typedef struct {
  const char *query_lower;
  int query_len;
  time_t now;
} FuzzyMatchContext;

void fuzzy_prepare_entry(TryEntry *entry);

// Updates entry->score and entry->rendered in-place
// Rendered string contains ANSI codes for highlighting matched characters
void fuzzy_match(TryEntry *entry, const char *query);
void fuzzy_match_with_context(TryEntry *entry, const FuzzyMatchContext *ctx);

// Legacy/Convenience: just calculate score (read-only)
float calculate_score(const char *text, const char *query, time_t mtime);

#endif // FUZZY_H
