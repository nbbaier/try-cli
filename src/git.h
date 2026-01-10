#ifndef GIT_H
#define GIT_H
#include <stdbool.h>

bool is_git_repo(const char *path);
int git_clone_local(const char *src, const char *dest);
int git_init_with_commit(const char *path, const char *message);
int git_remove_remotes(const char *path);
#endif
