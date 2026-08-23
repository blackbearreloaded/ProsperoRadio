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
extern "C" int sceKernelDebugOutText(int channel, const char* text);
extern "C" void* mmap(void* address, std::size_t length, int protection,
    int flags, int descriptor, long offset);
extern "C" int munmap(void* address, std::size_t length);
extern "C" void* __dso_handle = nullptr;
extern "C" char __eh_frame_hdr_start[1] = {};
extern "C" char __eh_frame_hdr_end[1] = {};
extern "C" char __eh_frame_start[1] = {};
extern "C" char __eh_frame_end[1] = {};

namespace {

constexpr const char* kRuntimeLogPath = "/data/homebrew/PPSA99728/runtime.log";

void LogRuntime(const char* message) {
    sceKernelDebugOutText(0, message);
    std::FILE* log = std::fopen(kRuntimeLogPath, "ab");
    if (log) {
        std::fputs(message, log);
        std::fputc('\n', log);
        std::fclose(log);
    }
}

void LogRuntimeError(const char* stage, const char* detail) {
    char message[768]{};
    std::snprintf(message, sizeof(message), "[radio PPSA99728] %s: %s", stage,
        detail ? detail : "unknown error");
    LogRuntime(message);
}

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

bool RunSmoke() {
    LogRuntime("[radio PPSA99728] entering RunSmoke");
    if (SDL_SetMemoryFunctions(AllocateTracked, CallocTracked, ReallocTracked, FreeTracked) != 0) {
        LogRuntimeError("SDL_SetMemoryFunctions failed", SDL_GetError());
        return false;
    }

    setenv("LD_LIBRARY_PATH", "/app0", 1);
    void* osmesa_library = dlopen("/app0/libOSMesa.so.8", RTLD_LAZY | RTLD_GLOBAL);
    if (!osmesa_library) {
        LogRuntimeError("OSMesa preload failed", dlerror());
        return false;
    }
    LogRuntime("[radio PPSA99728] OSMesa preload succeeded");

    SDL_SetMainReady();
    if (SDL_Init(SDL_INIT_VIDEO) != 0) {
        LogRuntimeError("SDL_Init failed", SDL_GetError());
        return false;
    }
    LogRuntime("[radio PPSA99728] SDL video initialized");

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 2);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 1);
    SDL_Window* window = SDL_CreateWindow("Radio Browser PPSA99728", SDL_WINDOWPOS_UNDEFINED,
        SDL_WINDOWPOS_UNDEFINED, 1920, 1080, SDL_WINDOW_SHOWN | SDL_WINDOW_OPENGL);
    if (!window) {
        LogRuntimeError("SDL_CreateWindow failed", SDL_GetError());
        return false;
    }
    LogRuntime("[radio PPSA99728] SDL OpenGL window created");
    SDL_GLContext gl_context = window ? SDL_GL_CreateContext(window) : nullptr;
    if (!gl_context || SDL_GL_MakeCurrent(window, gl_context) != 0) {
        LogRuntimeError("OSMesa context failed", SDL_GetError());
        return false;
    }
    LogRuntime("[radio PPSA99728] OSMesa context current");

    AppSystemInterface system_interface;
    AppFileInterface file_interface;
    OSMesaRenderInterface render_interface(1920, 1080);
    if (!render_interface.Initialize()) {
        LogRuntime("[radio PPSA99728] OpenGL function loading failed");
        return false;
    }
    render_interface.BeginFrame();
    render_interface.Clear(0.45f, 0.05f, 0.55f, 1.0f);
    SDL_GL_SwapWindow(window);
    LogRuntime("[radio PPSA99728] OpenGL diagnostic frame presented");
    Rml::RenderInterface* adapted_render_interface = render_interface.GetAdaptedInterface();
    Rml::SetSystemInterface(&system_interface);
    Rml::SetFileInterface(&file_interface);
    Rml::SetRenderInterface(adapted_render_interface);

    bool running = Rml::Initialise();
    if (!running) LogRuntime("[radio PPSA99728] RmlUi initialization failed");
    if (running) running = LoadFonts();
    if (!running) LogRuntime("[radio PPSA99728] RmlUi font loading failed");
    Rml::Context* context = running ? Rml::CreateContext("radio-browser", {1920, 1080}, adapted_render_interface) : nullptr;
    Rml::ElementDocument* document = context ? context->LoadDocument("ui/main.rml") : nullptr;
    if (document) {
        document->Show();
        LogRuntime("[radio PPSA99728] RmlUi document shown");
    } else {
        render_interface.BeginFrame();
        render_interface.Clear(0.70f, 0.04f, 0.10f, 1.0f);
        SDL_GL_SwapWindow(window);
        running = false;
        LogRuntime("[radio PPSA99728] RmlUi document loading failed");
    }

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
        }
        context->Update();
        render_interface.BeginFrame();
        render_interface.Clear(7.0f / 255.0f, 16.0f / 255.0f, 22.0f / 255.0f, 1.0f);
        context->Render();
        SDL_GL_SwapWindow(window);
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
