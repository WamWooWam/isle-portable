#include <SDL3/SDL_rand.h>
#include <random>

static std::default_random_engine e;

void SDL_srand(Uint64 seed) {
    e.seed(seed);
}

Sint32 SDL_rand(Sint32 n) {
    std::uniform_real_distribution<> dis(0, n);
    return dis(e);
}

float SDL_randf() {
    static std::uniform_real_distribution<> dis(0,1);
	return dis(e);
}
