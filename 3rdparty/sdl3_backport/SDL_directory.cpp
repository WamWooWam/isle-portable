#include <SDL2/SDL.h>
#include <SDL3/SDL_directory.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define INITIAL_CAPACITY 128
#define PATH_MAX 260

static SDL_bool add_file(char ***list, size_t *size, size_t *capacity, const char *path) {
    if (*size >= *capacity) {
        *capacity *= 2;
        *list = (char**)SDL_realloc((void*)*list, *capacity * sizeof(char *));
        if (!*list) {
            SDL_SetError("Failed to allocate memory");
            return SDL_FALSE;
        }
    }

    printf("%s\n", path);

    (*list)[(*size)++] = strdup(path);
    return SDL_TRUE;
}

static SDL_bool recurse(const char *dir_path, char ***list, size_t *size, size_t *capacity) {
    DIR *dir = opendir(dir_path);
    if (!dir) return SDL_FALSE;

    struct dirent *entry;
    while ((entry = readdir(dir)) != NULL) {
        // Skip "." and ".."
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;

        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir_path, entry->d_name);

        struct stat statbuf;
        if (stat(path, &statbuf) != 0)
            continue;

        if (S_ISDIR(statbuf.st_mode)) {
            recurse(path, list, size, capacity);
        } else if (S_ISREG(statbuf.st_mode)) {
            if (add_file(list, size, capacity, path) == SDL_FALSE) {
                goto fail;
            }
        }
    }

    closedir(dir);
    return SDL_TRUE;
fail:
    closedir(dir);
    return SDL_FALSE;
}

static char **list_files_recursive(const char *start_dir, int *out_count) {
    size_t size = 0, capacity = INITIAL_CAPACITY;
    char **file_list = (char**)SDL_malloc(capacity * sizeof(char *));
    if (!file_list) {
        SDL_SetError("Failed to allocate memory");
        return NULL;
    }

    recurse(start_dir, &file_list, &size, &capacity);
    *out_count = size;
    return file_list;
}


extern "C" char** SDL_GlobDirectory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count) {
    SDL_assert(pattern == NULL);
    SDL_assert(flags == 0);

    int cnt = 0;
    char** files = list_files_recursive(path, &cnt);

    *count = cnt;
    return files;
}

extern "C" SDL_bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info) {
    SDL_assert(info == NULL);
    
    return access(path, F_OK) == 0 ? SDL_TRUE : SDL_FALSE;
}