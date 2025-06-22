// header to implement SDL_rand functions for SDL2

#ifndef SDL_directory_h
#define SDL_directory_h

#include <SDL2/SDL_stdinc.h>

/**
 * Flags for path matching.
 *
 * \since This datatype is available since SDL 3.2.0.
 *
 * \sa SDL_GlobDirectory
 * \sa SDL_GlobStorageDirectory
 */
typedef Uint32 SDL_GlobFlags;
typedef void SDL_PathInfo;

#define SDL_GLOB_CASEINSENSITIVE (1u << 0)

extern "C" DECLSPEC char ** SDLCALL SDL_GlobDirectory(const char *path, const char *pattern, SDL_GlobFlags flags, int *count);
extern "C" DECLSPEC SDL_bool SDL_GetPathInfo(const char *path, SDL_PathInfo *info);

#endif // SDL_rand_h