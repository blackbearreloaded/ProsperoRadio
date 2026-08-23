#include <SDL2/SDL.h>

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/FontEngineInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <pthread.h>
#include <vector>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" void* __dso_handle = nullptr;
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};

extern "C" int pthread_once(pthread_once_t* once_control, void (*init_routine)(void)) {
    constexpr int running = 2;
    int state = __atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE);
    if (state == PTHREAD_DONE_INIT) return 0;

    int expected = PTHREAD_NEEDS_INIT;
    if (__atomic_compare_exchange_n(&once_control->state, &expected, running, false,
            __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
        init_routine();
        __atomic_store_n(&once_control->state, PTHREAD_DONE_INIT, __ATOMIC_RELEASE);
        return 0;
    }

    while (__atomic_load_n(&once_control->state, __ATOMIC_ACQUIRE) != PTHREAD_DONE_INIT) {
        sceKernelUsleep(100);
    }
    return 0;
}

extern "C" void __assert(const char*, const char*, int, const char*) {
    std::abort();
}

extern "C" float strtof(const char* value, char** end) {
    return static_cast<float>(strtod(value, end));
}

extern "C" char* strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return const_cast<char*>(haystack);
    for (; *haystack; ++haystack) {
        const char* h = haystack;
        const char* n = needle;
        while (*h && *n) {
            const char hc = *h >= 'A' && *h <= 'Z' ? static_cast<char>(*h + ('a' - 'A')) : *h;
            const char nc = *n >= 'A' && *n <= 'Z' ? static_cast<char>(*n + ('a' - 'A')) : *n;
            if (hc != nc) break;
            ++h;
            ++n;
        }
        if (!*n) return const_cast<char*>(haystack);
    }
    return nullptr;
}

namespace {

class AppSystemInterface final : public Rml::SystemInterface {
public:
    double GetElapsedTime() override {
        return static_cast<double>(SDL_GetTicks64() - start_ticks_) / 1000.0;
    }

private:
    Uint64 start_ticks_ = SDL_GetTicks64();
};

class AppFileInterface final : public Rml::FileInterface {
public:
    Rml::FileHandle Open(const Rml::String& path) override {
        std::FILE* file = std::fopen(path.c_str(), "rb");
        if (!file) {
            const Rml::String app_path = "/app0/" + path;
            file = std::fopen(app_path.c_str(), "rb");
        }
        return reinterpret_cast<Rml::FileHandle>(file);
    }

    void Close(Rml::FileHandle file) override {
        if (file) std::fclose(reinterpret_cast<std::FILE*>(file));
    }

    size_t Read(void* buffer, size_t size, Rml::FileHandle file) override {
        return std::fread(buffer, 1, size, reinterpret_cast<std::FILE*>(file));
    }

    bool Seek(Rml::FileHandle file, long offset, int origin) override {
        return fseeko(reinterpret_cast<std::FILE*>(file), offset, origin) == 0;
    }

    size_t Tell(Rml::FileHandle file) override {
        return static_cast<size_t>(ftello(reinterpret_cast<std::FILE*>(file)));
    }
};

struct Geometry {
    std::vector<Rml::Vertex> vertices;
    std::vector<int> indices;
};

class SdlRenderInterface final : public Rml::RenderInterface {
public:
    explicit SdlRenderInterface(SDL_Renderer* renderer) : renderer_(renderer) {}

    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
        Rml::Span<const int> indices) override {
        auto* geometry = new Geometry;
        geometry->vertices.assign(vertices.begin(), vertices.end());
        geometry->indices.assign(indices.begin(), indices.end());
        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle handle) override {
        delete reinterpret_cast<Geometry*>(handle);
    }

    void RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
        Rml::TextureHandle texture) override {
        auto* geometry = reinterpret_cast<Geometry*>(handle);
        std::vector<SDL_Vertex> vertices;
        vertices.reserve(geometry->vertices.size());
        for (const Rml::Vertex& vertex : geometry->vertices) {
            SDL_Vertex sdl_vertex{};
            sdl_vertex.position = {vertex.position.x + translation.x, vertex.position.y + translation.y};
            sdl_vertex.color = {vertex.colour.red, vertex.colour.green,
                vertex.colour.blue, vertex.colour.alpha};
            sdl_vertex.tex_coord = {vertex.tex_coord.x, vertex.tex_coord.y};
            vertices.push_back(sdl_vertex);
        }
        SDL_RenderGeometry(renderer_, reinterpret_cast<SDL_Texture*>(texture),
            vertices.data(), static_cast<int>(vertices.size()), geometry->indices.data(),
            static_cast<int>(geometry->indices.size()));
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i&, const Rml::String&) override { return 0; }
    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte>, Rml::Vector2i) override { return 0; }
    void ReleaseTexture(Rml::TextureHandle) override {}

    void EnableScissorRegion(bool enable) override {
        SDL_RenderSetClipRect(renderer_, enable ? &scissor_ : nullptr);
    }

    void SetScissorRegion(Rml::Rectanglei region) override {
        scissor_ = {region.Left(), region.Top(), region.Width(), region.Height()};
    }

private:
    SDL_Renderer* renderer_;
    SDL_Rect scissor_{};
};

void PresentColor(SDL_Renderer* renderer, SDL_Window* window, Uint8 red, Uint8 green, Uint8 blue) {
    SDL_SetRenderDrawColor(renderer, red, green, blue, 255);
    SDL_RenderClear(renderer);
    SDL_RenderFlush(renderer);
    SDL_UpdateWindowSurface(window);
}

[[noreturn]] void KeepProcessAlive() {
    for (;;) sceKernelUsleep(1000000);
}

bool RunSmoke() {
    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) return false;

    SDL_Window* window = SDL_CreateWindow("Radio Browser PPSA99715", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "software");
    SDL_Surface* surface = window ? SDL_GetWindowSurface(window) : nullptr;
    SDL_Renderer* renderer = surface ? SDL_CreateSoftwareRenderer(surface) : nullptr;
    if (!window || !renderer) return false;

    PresentColor(renderer, window, 20, 80, 180);

    AppSystemInterface system_interface;
    AppFileInterface file_interface;
    Rml::FontEngineInterface font_engine;
    SdlRenderInterface render_interface(renderer);
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetFontEngineInterface(&font_engine);
    Rml::SetRenderInterface(&render_interface);

    bool running = Rml::Initialise();
    Rml::Context* context = running ? Rml::CreateContext("radio-browser", {1920, 1080}, &render_interface) : nullptr;
    Rml::ElementDocument* document = context ? context->LoadDocument("ui/main.rml") : nullptr;
    if (document) {
        document->Show();
    } else {
        PresentColor(renderer, window, 180, 20, 40);
        running = false;
    }

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }
        context->Update();
        SDL_SetRenderDrawColor(renderer, 7, 16, 22, 255);
        SDL_RenderClear(renderer);
        context->Render();
        SDL_RenderFlush(renderer);
        SDL_UpdateWindowSurface(window);
        sceKernelUsleep(16667);
    }

    return running;
}

} // namespace

int main() {
    sceSystemServiceHideSplashScreen();
    RunSmoke();
    KeepProcessAlive();
}
