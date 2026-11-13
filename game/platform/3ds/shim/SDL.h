#ifndef CONKER_3DS_SDL_SHIM_H
#define CONKER_3DS_SDL_SHIM_H

#include <cstdlib>

#define SDLCALL

struct SDL_Window {};

inline void SDL_GetWindowSize(SDL_Window*, int* w, int* h) {
    if (w) *w = 400;
    if (h) *h = 240;
}

inline const char* SDL_getenv(const char* name) {
    return std::getenv(name);
}

enum {
    SDL_MOUSEWHEEL = 0x403,
    SDL_MOUSEWHEEL_FLIPPED = 1
};

struct SDL_MouseWheelEvent {
    int y;
    unsigned int direction;
};

union SDL_Event {
    unsigned int type;
    SDL_MouseWheelEvent wheel;
};

using SDL_EventFilter = int (*)(void*, SDL_Event*);

inline void SDL_AddEventWatch(SDL_EventFilter, void*) {}

#endif
