// header to implement SDL_rand functions for SDL2

#ifndef SDL_rand_h
#define SDL_rand_h

#include <SDL2/SDL_stdinc.h>

void SDL_srand(Uint64 seed);
Sint32 SDL_rand(Sint32 n);
float SDL_randf();

#endif // SDL_rand_h