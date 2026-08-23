#include <SDL2/SDL.h>

#include <cstdint>
#include <cstdlib>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" void* __dso_handle = nullptr;
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};

extern "C" void __assert(const char*, const char*, int, const char*) {
    std::abort();
}

extern "C" char* strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; ++haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n) {
            char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc) break;
            ++h;
            ++n;
        }
        if (!*n) return const_cast<char*>(haystack);
    }
    return nullptr;
}

[[noreturn]] void KeepProcessAlive() {
    for (;;) {
        sceKernelUsleep(1000000);
    }
}

int main() {
    sceSystemServiceHideSplashScreen();
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return 1;
    }

    SDL_Window* window = SDL_CreateWindow("Radio Browser PPSA99710", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!window || !renderer) {
        return 2;
    }

    SDL_SetRenderDrawColor(renderer, 7, 16, 22, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    if (SDL_UpdateWindowSurface(window) != 0) {
        return 3;
    }

    KeepProcessAlive();
}
