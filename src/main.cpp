#include <SDL2/SDL.h>

#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

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

namespace {

class ProbeSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return static_cast<double>(SDL_GetTicks64()) / 1000.0;
    }
};

class ProbeFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String&) override { return 0; }
    void Close(Rml::FileHandle) override {}
    size_t Read(void*, size_t, Rml::FileHandle) override { return 0; }
    bool Seek(Rml::FileHandle, long, int) override { return false; }
    size_t Tell(Rml::FileHandle) override { return 0; }
};

class ProbeRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex>,
        Rml::Span<const int>) override { return 0; }
    void ReleaseGeometry(Rml::CompiledGeometryHandle) override {}
    void RenderGeometry(Rml::CompiledGeometryHandle, Rml::Vector2f,
        Rml::TextureHandle) override {}
    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override {
        return 0;
    }
    void ReleaseTexture(Rml::TextureHandle) override {}
    void EnableScissorRegion(bool) override {}
    void SetScissorRegion(Rml::Rectanglei) override {}
};

void PresentColor(SDL_Renderer* renderer, SDL_Window* window,
    Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    SDL_UpdateWindowSurface(window);
}

[[noreturn]] void KeepProcessAlive() {
    for (;;) {
        sceKernelUsleep(1000000);
    }
}

} // namespace

int main() {
    sceSystemServiceHideSplashScreen();
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return 1;

    SDL_Window* window = SDL_CreateWindow("Radio Browser PPSA99711", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!window || !renderer) return 2;

    PresentColor(renderer, window, 20, 80, 180);
    sceKernelUsleep(3000000);

    ProbeSystemInterface system_interface;
    ProbeFileInterface file_interface;
    Rml::FontEngineInterface font_engine;
    ProbeRenderInterface render_interface;
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetFontEngineInterface(&font_engine);
    Rml::SetRenderInterface(&render_interface);

    PresentColor(renderer, window, 220, 130, 20);
    sceKernelUsleep(3000000);

    const bool initialized = Rml::Initialise();
    PresentColor(renderer, window, initialized ? 20 : 180, initialized ? 160 : 20, 40);
    KeepProcessAlive();
}
