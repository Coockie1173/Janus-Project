#include "ge.h"
#include <SDL3/SDL.h>

namespace ge {

//port/package/icon.bmp as an array in the executable (goldeneye.cmake)
#include "ge_icon.h"

void window_icon(SDL_Window* win) {
    SDL_Surface* icon = SDL_LoadBMP_IO(SDL_IOFromConstMem(kIcon, sizeof(kIcon)), true);
    if (!icon) { rt_log("goldeneye: no window icon: %s", SDL_GetError()); return; }
    SDL_SetWindowIcon(win, icon);
    SDL_DestroySurface(icon);
}

} //namespace ge
