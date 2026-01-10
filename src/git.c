#include "git.h"
#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

bool is_git_repo(const char *path) {
    Z_CLEANUP(zstr_free) zstr dot_git = join_path(path, ".git");
    return dir_exists(zstr_cstr(&dot_git));
}

int git_clone_local(const char *src, const char *dest) {
    char abs_src[1024];
    if (realpath(src, abs_src) == NULL) return -1;

    Z_CLEANUP(zstr_free) zstr cmd = zstr_init();
    zstr_fmt(&cmd, "git clone --quiet \"file://%s\" \"%s\"", abs_src, dest);
    int res = system(zstr_cstr(&cmd));
    return (WIFEXITED(res)) ? WEXITSTATUS(res) : -1;
}

int git_init_with_commit(const char *path, const char *message) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return -1;
    if (chdir(path) != 0) return -1;

    Z_CLEANUP(zstr_free) zstr cmd = zstr_init();
    zstr_fmt(&cmd, "git init --quiet && git add . && git commit --quiet -m \"%s\"", message);
    int res = system(zstr_cstr(&cmd));

    if (chdir(cwd) != 0) {
        // Failed to return to original directory
    }
    return (WIFEXITED(res)) ? WEXITSTATUS(res) : -1;
}

int git_remove_remotes(const char *path) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) == NULL) return -1;
    if (chdir(path) != 0) return -1;

    int res = system("git remote | while read -r r; do git remote remove \"$r\"; done");

    if (chdir(cwd) != 0) {
    }
    return (WIFEXITED(res)) ? WEXITSTATUS(res) : -1;
}
