// Feature test macros for cross-platform compatibility
#if defined(__APPLE__)
#define _DARWIN_C_SOURCE
#else
#define _GNU_SOURCE
#endif

#include "utils.h"
#include "config.h"
#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <fnmatch.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

char *trim(char *str) {
  char *end;
  while (isspace((unsigned char)*str))
    str++;
  if (*str == 0)
    return str;
  end = str + strlen(str) - 1;
  while (end > str && isspace((unsigned char)*end))
    end--;
  end[1] = '\0';
  return str;
}

zstr get_home_dir(void) {
  const char *home = getenv("HOME");
  if (!home) {
    struct passwd *pw = getpwuid(getuid());
    if (pw)
      home = pw->pw_dir;
  }
  return home ? zstr_from(home) : zstr_init();
}

zstr join_path(const char *dir, const char *file) {
  zstr s = zstr_from(dir);
  zstr_cat(&s, "/");
  zstr_cat(&s, file);
  return s;
}

zstr get_default_tries_path(void) {
  Z_CLEANUP(zstr_free) zstr home = get_home_dir();
  if (zstr_is_empty(&home))
    return home;
  zstr path = join_path(zstr_cstr(&home), DEFAULT_TRIES_PATH_SUFFIX);
  zstr_free(&home);
  return path;
}

bool dir_exists(const char *path) {
  struct stat sb;
  return (stat(path, &sb) == 0 && S_ISDIR(sb.st_mode));
}

bool file_exists(const char *path) {
  struct stat sb;
  return (stat(path, &sb) == 0 && S_ISREG(sb.st_mode));
}

int mkdir_p(const char *path) {
  Z_CLEANUP(zstr_free) zstr tmp = zstr_from(path);
  
  // Remove trailing slash
  if (zstr_len(&tmp) > 0) {
      char *data = zstr_data(&tmp);
      if (data[zstr_len(&tmp) - 1] == '/') {
          zstr_pop_char(&tmp);
      }
  }

  char *p_start = zstr_data(&tmp);
  // Start after the first char to avoid stopping at root /
  char *p = p_start + 1;

  while (*p) {
    if (*p == '/') {
      *p = 0;
      if (mkdir(p_start, S_IRWXU) != 0 && errno != EEXIST) {
        return -1;
      }
      *p = '/';
    }
    p++;
  }
  if (mkdir(p_start, S_IRWXU) != 0 && errno != EEXIST) {
    return -1;
  }
  return 0;
}

zstr format_relative_time(time_t mtime) {
  time_t now = time(NULL);
  double diff = difftime(now, mtime);
  zstr s = zstr_init();

  if (diff < 60) {
    zstr_cat(&s, "just now");
  } else if (diff < 3600) {
    zstr_fmt(&s, "%dm ago", (int)(diff / 60));
  } else if (diff < 86400) {
    zstr_fmt(&s, "%dh ago", (int)(diff / 3600));
  } else {
    zstr_fmt(&s, "%dd ago", (int)(diff / 86400));
  }
  return s;
}

// Check if a character is valid for directory names
// Valid: alphanumeric, underscore, hyphen, dot
static bool is_valid_dir_char(char c) {
  return isalnum((unsigned char)c) || c == '_' || c == '-' || c == '.';
}

// Check if name contains only valid directory name characters
// (after normalization, so no spaces)
bool is_valid_dir_name(const char *name) {
  if (!name || !*name) return false;

  for (const char *p = name; *p; p++) {
    if (!is_valid_dir_char(*p) && *p != ' ') {
      return false;
    }
  }
  return true;
}

// Normalize directory name:
// - Convert spaces to hyphens
// - Collapse multiple consecutive hyphens to single hyphen
// - Strip leading/trailing hyphens and spaces
// - Return empty string if invalid characters found
zstr normalize_dir_name(const char *name) {
  zstr result = zstr_init();
  if (!name || !*name) return result;

  // First pass: check for invalid characters
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (!is_valid_dir_char(c) && !isspace((unsigned char)c)) {
      // Invalid character found - return empty string
      return result;
    }
  }

  // Second pass: normalize
  bool last_was_hyphen = true;  // Start true to strip leading hyphens/spaces
  for (const char *p = name; *p; p++) {
    char c = *p;
    if (isspace((unsigned char)c) || c == '-') {
      // Convert space to hyphen, collapse multiple hyphens
      if (!last_was_hyphen) {
        zstr_push(&result, '-');
        last_was_hyphen = true;
      }
    } else {
      zstr_push(&result, c);
      last_was_hyphen = false;
    }
  }

  // Strip trailing hyphen
  while (zstr_len(&result) > 0) {
    char *data = zstr_data(&result);
    if (data[zstr_len(&result) - 1] == '-') {
      zstr_pop_char(&result);
    } else {
      break;
    }
  }

  return result;
}

GitignorePatterns parse_gitignore(const char *dir_path) {
  GitignorePatterns patterns = {0};
  patterns.patterns = vec_init_capacity_zstr(16);
  patterns.valid = false;

  Z_CLEANUP(zstr_free) zstr path = join_path(dir_path, ".gitignore");
  FILE *f = fopen(zstr_cstr(&path), "r");
  if (!f) return patterns;

  patterns.valid = true;
  char *line = NULL;
  size_t len = 0;
  ssize_t read;
  while ((read = getline(&line, &len, f)) != -1) {
    char *trimmed = trim(line);
    if (trimmed[0] == '#' || trimmed[0] == '\0') continue;

    zstr pattern = zstr_from(trimmed);
    vec_push_zstr(&patterns.patterns, pattern);
  }
  free(line);
  fclose(f);
  return patterns;
}

void free_gitignore_patterns(GitignorePatterns *patterns) {
  if (!patterns) return;
  for (size_t i = 0; i < patterns->patterns.length; i++) {
    zstr_free(&patterns->patterns.data[i]);
  }
  vec_free_zstr(&patterns->patterns);
  patterns->valid = false;
}

bool should_skip_path(const char *path, const char *base_path, const GitignorePatterns *patterns) {
  // Always skip .git
  if (strcmp(path, ".git") == 0) return true;

  // Also check if it's a sub-path like "dir/.git"
  const char *last_slash = strrchr(path, '/');
  if (last_slash) {
    if (strcmp(last_slash + 1, ".git") == 0) return true;
  } else {
    if (strcmp(path, ".git") == 0) return true;
  }

  if (!patterns || !patterns->valid) return false;

  // Get relative path from base_path
  const char *rel_path = path;
  if (base_path && strncmp(path, base_path, strlen(base_path)) == 0) {
    rel_path = path + strlen(base_path);
    while (rel_path[0] == '/') rel_path++;
  }

  if (rel_path[0] == '\0') return false;

  for (size_t i = 0; i < patterns->patterns.length; i++) {
    const char *p = zstr_cstr(&patterns->patterns.data[i]);
    bool negate = false;
    if (p[0] == '!') {
      negate = true;
      p++;
    }

    // Basic support for directory-only patterns (trailing /)
    size_t p_len = strlen(p);
    bool dir_only = (p_len > 0 && p[p_len - 1] == '/');

    // Create a temporary pattern without trailing slash for fnmatch
    char pattern_buf[1024];
    strncpy(pattern_buf, p, sizeof(pattern_buf) - 1);
    pattern_buf[sizeof(pattern_buf) - 1] = '\0';
    if (dir_only) pattern_buf[p_len - 1] = '\0';

    // Simple glob matching using fnmatch
    // Note: fnmatch doesn't support ** in a standard way, but we'll use FNM_PATHNAME
    if (fnmatch(pattern_buf, rel_path, FNM_PATHNAME) == 0) {
      return !negate;
    }

    // Also try matching just the filename if the pattern doesn't have slashes
    if (strchr(pattern_buf, '/') == NULL) {
      const char *filename = strrchr(rel_path, '/');
      if (filename) filename++;
      else filename = rel_path;

      if (fnmatch(pattern_buf, filename, 0) == 0) {
        return !negate;
      }
    }
  }
  return false;
}

int copy_directory(const char *src, const char *dest, bool respect_gitignore) {
  GitignorePatterns patterns = {0};
  if (respect_gitignore) {
    patterns = parse_gitignore(src);
  }

  int result = 0;
  DIR *dir = opendir(src);
  if (!dir) {
    if (respect_gitignore) free_gitignore_patterns(&patterns);
    return -1;
  }

  struct stat src_st;
  if (stat(src, &src_st) != 0) {
    if (respect_gitignore) free_gitignore_patterns(&patterns);
    return -1;
  }

  if (mkdir(dest, src_st.st_mode) != 0 && errno != EEXIST) {
    closedir(dir);
    if (respect_gitignore) free_gitignore_patterns(&patterns);
    return -1;
  }

  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) continue;

    Z_CLEANUP(zstr_free) zstr src_path = join_path(src, entry->d_name);

    // Check if we should skip this path
    if (respect_gitignore && should_skip_path(zstr_cstr(&src_path), src, &patterns)) {
      continue;
    }

    Z_CLEANUP(zstr_free) zstr dest_path = join_path(dest, entry->d_name);
    struct stat st;
    if (lstat(zstr_cstr(&src_path), &st) != 0) continue;

    if (S_ISDIR(st.st_mode)) {
      // Recursively copy directory
      if (copy_directory(zstr_cstr(&src_path), zstr_cstr(&dest_path), respect_gitignore) != 0) {
        result = -1;
      }
    } else if (S_ISREG(st.st_mode)) {
      // Copy regular file
      FILE *in = fopen(zstr_cstr(&src_path), "rb");
      FILE *out = fopen(zstr_cstr(&dest_path), "wb");
      if (in && out) {
        char buf[8192];
        size_t n;
        while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
          if (fwrite(buf, 1, n, out) != n) {
            result = -1;
            break;
          }
        }
        chmod(zstr_cstr(&dest_path), st.st_mode);
      } else {
        result = -1;
      }
      if (in) fclose(in);
      if (out) fclose(out);
    } else if (S_ISLNK(st.st_mode)) {
      // Follow symlinks: copy contents, not links
      struct stat target_st;
      if (stat(zstr_cstr(&src_path), &target_st) == 0) {
        if (S_ISDIR(target_st.st_mode)) {
          if (copy_directory(zstr_cstr(&src_path), zstr_cstr(&dest_path), respect_gitignore) != 0) {
            result = -1;
          }
        } else if (S_ISREG(target_st.st_mode)) {
          FILE *in = fopen(zstr_cstr(&src_path), "rb");
          FILE *out = fopen(zstr_cstr(&dest_path), "wb");
          if (in && out) {
            char buf[8192];
            size_t n;
            while ((n = fread(buf, 1, sizeof(buf), in)) > 0) {
              if (fwrite(buf, 1, n, out) != n) {
                result = -1;
                break;
              }
            }
            chmod(zstr_cstr(&dest_path), target_st.st_mode);
          } else {
            result = -1;
          }
          if (in) fclose(in);
          if (out) fclose(out);
        }
      }
    }
  }

  closedir(dir);
  if (respect_gitignore) {
    free_gitignore_patterns(&patterns);
  }
  return result;
}

zstr generate_fork_name(const char *source_name, const char *tries_path) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  char date[11];
  strftime(date, sizeof(date), "%Y-%m-%d", t);

  zstr base_name = zstr_from(source_name);

  // Check if source_name starts with YYYY-MM-DD- and strip it if it does
  const char *start_data = zstr_cstr(&base_name);
  if (zstr_len(&base_name) > 11 && start_data[4] == '-' && start_data[7] == '-' && start_data[10] == '-') {
    bool has_prefix_date = true;
    for (int i = 0; i < 10; i++) {
      if (i == 4 || i == 7) continue;
      if (!isdigit(start_data[i])) { has_prefix_date = false; break; }
    }
    if (has_prefix_date) {
      zstr new_base = zstr_from(start_data + 11);
      zstr_free(&base_name);
      base_name = new_base;
    }
  }

  // Check if source_name ends with -YYYY-MM-DD and strip it if it does
  size_t blen = zstr_len(&base_name);
  if (blen >= 11) {
    const char *data = zstr_cstr(&base_name);
    bool has_date = true;
    if (data[blen-11] != '-') has_date = false;
    else {
      for (int i = 0; i < 10; i++) {
        if (i == 4 || i == 7) {
          if (data[blen-10+i] != '-') { has_date = false; break; }
        } else {
          if (!isdigit(data[blen-10+i])) { has_date = false; break; }
        }
      }
    }
    if (has_date) {
      for (int i = 0; i < 11; i++) zstr_pop_char(&base_name);
    }
  }

  zstr fork_name = zstr_from(date);
  zstr_cat(&fork_name, "-");
  zstr_cat(&fork_name, zstr_cstr(&base_name));

  Z_CLEANUP(zstr_free) zstr full_path = join_path(tries_path, zstr_cstr(&fork_name));
  if (!dir_exists(zstr_cstr(&full_path))) {
    zstr_free(&base_name);
    return fork_name;
  }

  // Collision handling: append -2, -3, ...
  int counter = 2;
  while (true) {
    zstr candidate = zstr_from(zstr_cstr(&fork_name));
    zstr_fmt(&candidate, "-%d", counter);

    Z_CLEANUP(zstr_free) zstr candidate_full_path = join_path(tries_path, zstr_cstr(&candidate));
    if (!dir_exists(zstr_cstr(&candidate_full_path))) {
      zstr_free(&base_name);
      zstr_free(&fork_name);
      return candidate;
    }
    zstr_free(&candidate);
    counter++;
  }
}
