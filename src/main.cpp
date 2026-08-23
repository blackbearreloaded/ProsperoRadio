#include <cstdint>

extern "C" int sceKernelUsleep(std::uint32_t microseconds);
extern "C" int sceSystemServiceHideSplashScreen(void);

[[noreturn]] void KeepProcessAlive() {
    for (;;) {
        sceKernelUsleep(1000000);
    }
}

int main() {
    sceSystemServiceHideSplashScreen();
    KeepProcessAlive();
}
