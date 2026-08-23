#include <SDL2/SDL.h>

#include "osmesa_render_interface.h"

#include <RmlUi/Core/Context.h>
#include <RmlUi/Core/Core.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/FileInterface.h>
#include <RmlUi/Core/RenderInterface.h>
#include <RmlUi/Core/RenderInterfaceCompatibility.h>
#include <RmlUi/Core/SystemInterface.h>

#include <cstdio>
#include <cstddef>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <dlfcn.h>
#include <limits>
#include <new>
#include <pthread.h>
#include <vector>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceSystemServiceHideSplashScreen(void);
extern "C" void* mmap(void* address, std::size_t length, int protection,
    int flags, int descriptor, long offset);
extern "C" int munmap(void* address, std::size_t length);
extern "C" void* __dso_handle = nullptr;
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};

namespace {

constexpr std::size_t kMappedAllocationThreshold = 64 * 1024;
constexpr std::uint64_t kAllocationMagic = UINT64_C(0x524144494F4D454D);
constexpr int kProtectionReadWrite = 3;
constexpr int kMapPrivateAnonymous = 0x1002;

struct alignas(std::max_align_t) AllocationHeader {
    std::uint64_t magic;
    std::size_t requested_size;
    std::size_t mapped_size;
};

void* AllocateTracked(std::size_t size) {
    if (size == 0) size = 1;
    if (size > std::numeric_limits<std::size_t>::max() - sizeof(AllocationHeader)) return nullptr;

    const std::size_t total = sizeof(AllocationHeader) + size;
    AllocationHeader* header = nullptr;
    std::size_t mapped_size = 0;
    if (size >= kMappedAllocationThreshold) {
        mapped_size = (total + 0x3fff) & ~std::size_t(0x3fff);
        void* mapping = mmap(nullptr, mapped_size, kProtectionReadWrite,
            kMapPrivateAnonymous, -1, 0);
        if (mapping != reinterpret_cast<void*>(-1)) {
            header = static_cast<AllocationHeader*>(mapping);
        }
    } else {
        header = static_cast<AllocationHeader*>(std::malloc(total));
    }
    if (!header) return nullptr;

    header->magic = kAllocationMagic;
    header->requested_size = size;
    header->mapped_size = mapped_size;
    return header + 1;
}

void FreeTracked(void* allocation) noexcept {
    if (!allocation) return;
    auto* header = static_cast<AllocationHeader*>(allocation) - 1;
    // SDL can retain small allocations made by its original allocator before
    // custom memory functions are installed. Those remain libc-owned.
    if (header->magic != kAllocationMagic) {
        std::free(allocation);
        return;
    }
    if (header->mapped_size != 0) {
        munmap(header, header->mapped_size);
    } else {
        std::free(header);
    }
}

void* CallocTracked(std::size_t count, std::size_t size) {
    if (size != 0 && count > std::numeric_limits<std::size_t>::max() / size) return nullptr;
    const std::size_t total = count * size;
    void* allocation = AllocateTracked(total);
    if (allocation) std::memset(allocation, 0, total);
    return allocation;
}

void* ReallocTracked(void* allocation, std::size_t size) {
    if (!allocation) return AllocateTracked(size);
    if (size == 0) {
        FreeTracked(allocation);
        return nullptr;
    }

    auto* old_header = static_cast<AllocationHeader*>(allocation) - 1;
    if (old_header->magic != kAllocationMagic) std::abort();
    void* replacement = AllocateTracked(size);
    if (!replacement) return nullptr;
    std::memcpy(replacement, allocation,
        old_header->requested_size < size ? old_header->requested_size : size);
    FreeTracked(allocation);
    return replacement;
}

} // namespace

void* operator new(std::size_t size) {
    void* allocation = AllocateTracked(size);
    if (!allocation) std::abort();
    return allocation;
}

void* operator new[](std::size_t size) {
    void* allocation = AllocateTracked(size);
    if (!allocation) std::abort();
    return allocation;
}

void operator delete(void* allocation) noexcept { FreeTracked(allocation); }
void operator delete[](void* allocation) noexcept { FreeTracked(allocation); }
void operator delete(void* allocation, std::size_t) noexcept { FreeTracked(allocation); }
void operator delete[](void* allocation, std::size_t) noexcept { FreeTracked(allocation); }

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

extern "C" int fseek(std::FILE* file, long offset, int origin) {
    return fseeko(file, offset, origin);
}

extern "C" long ftell(std::FILE* file) {
    return static_cast<long>(ftello(file));
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

bool LoadFonts() {
    return Rml::LoadFontFace("ui/fonts/NotoSans-Regular.ttf") &&
        Rml::LoadFontFace("ui/fonts/NotoSans-Bold.ttf") &&
        Rml::LoadFontFace("ui/fonts/DejaVuSans.ttf", true) &&
        Rml::LoadFontFace("ui/fonts/NotoEmoji-Regular.ttf", true);
}

[[noreturn]] void KeepProcessAlive() {
    for (;;) sceKernelUsleep(1000000);
}

bool PaintSurface(SDL_Window* window, Uint32 color) {
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    return surface && SDL_FillRect(surface, nullptr, color) == 0 &&
        SDL_UpdateWindowSurface(window) == 0;
}

bool RunSmoke() {
    if (SDL_SetMemoryFunctions(AllocateTracked, CallocTracked, ReallocTracked, FreeTracked) != 0) {
        return false;
    }

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        return false;
    }
    SDL_Window* window = SDL_CreateWindow("Radio Browser PPSA99730", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN);
    if (!window) return false;

    // Blue: SDL software presentation is alive and the OSMesa probe is starting.
    PaintSurface(window, 0xff203b70u);

    void* library = dlopen("/app0/libOSMesa.so.8", RTLD_LAZY | RTLD_GLOBAL);
    if (!library) {
        PaintSurface(window, 0xff9f1d20u); // Red: absolute-path dlopen failed.
        return false;
    }

    using GetProcAddress = void* (*)(const char*);
    using CreateContext = void* (*)(unsigned int, void*);
    using MakeCurrent = unsigned char (*)(void*, void*, unsigned int, int, int);
    using PixelStore = void (*)(int, int);
    const auto get_proc = reinterpret_cast<GetProcAddress>(dlsym(library, "OSMesaGetProcAddress"));
    const auto create_context = reinterpret_cast<CreateContext>(dlsym(library, "OSMesaCreateContext"));
    const auto make_current = reinterpret_cast<MakeCurrent>(dlsym(library, "OSMesaMakeCurrent"));
    const auto pixel_store = reinterpret_cast<PixelStore>(dlsym(library, "OSMesaPixelStore"));
    if (!get_proc || !create_context || !make_current || !pixel_store) {
        PaintSurface(window, 0xffd16b20u); // Orange: a required OSMesa symbol is absent.
        return false;
    }

    void* context = create_context(0x1908u, nullptr); // GL_RGBA
    if (!context) {
        PaintSurface(window, 0xffd5ba35u); // Yellow: context creation failed.
        return false;
    }
    SDL_Surface* surface = SDL_GetWindowSurface(window);
    if (!surface || !make_current(context, surface->pixels, 0x1401u, surface->w, surface->h)) {
        PaintSurface(window, 0xffb1329bu); // Magenta: binding the SDL surface failed.
        return false;
    }
    pixel_store(0x11, 0); // OSMESA_Y_UP = false.

    using ClearColor = void (*)(float, float, float, float);
    using Clear = void (*)(unsigned int);
    using Flush = void (*)();
    const auto clear_color = reinterpret_cast<ClearColor>(get_proc("glClearColor"));
    const auto clear = reinterpret_cast<Clear>(get_proc("glClear"));
    const auto flush = reinterpret_cast<Flush>(get_proc("glFlush"));
    if (!clear_color || !clear || !flush) {
        PaintSurface(window, 0xff257f8du); // Teal: OpenGL entry-point lookup failed.
        return false;
    }

    clear_color(0.16f, 0.64f, 0.36f, 1.0f);
    clear(0x00004000u); // GL_COLOR_BUFFER_BIT
    flush();
    SDL_UpdateWindowSurface(window); // Green: OSMesa rendered into the SDL surface.
    return true;
}

} // namespace

int main() {
    sceSystemServiceHideSplashScreen();
    RunSmoke();
    KeepProcessAlive();
}
