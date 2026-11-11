#include <SDL2/SDL.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>

#include "chip8.hpp"

namespace {

constexpr int kScale = 12;                 // window pixels per CHIP-8 pixel
constexpr int kInstructionsPerSecond = 700;
constexpr int kTimerHz = 60;
constexpr int kSampleRate = 44100;

// Maps the CHIP-8 hex keypad onto the left side of a QWERTY keyboard:
//   1 2 3 C        1 2 3 4
//   4 5 6 D   ->   Q W E R
//   7 8 9 E        A S D F
//   A 0 B F        Z X C V
int mapKey(SDL_Keycode key) {
    switch (key) {
        case SDLK_x: return 0x0;
        case SDLK_1: return 0x1;
        case SDLK_2: return 0x2;
        case SDLK_3: return 0x3;
        case SDLK_q: return 0x4;
        case SDLK_w: return 0x5;
        case SDLK_e: return 0x6;
        case SDLK_a: return 0x7;
        case SDLK_s: return 0x8;
        case SDLK_d: return 0x9;
        case SDLK_z: return 0xA;
        case SDLK_c: return 0xB;
        case SDLK_4: return 0xC;
        case SDLK_r: return 0xD;
        case SDLK_f: return 0xE;
        case SDLK_v: return 0xF;
        default:     return -1;
    }
}

// Generates a quiet square-wave beep while the sound timer is running.
struct Beeper {
    bool on = false;
    double phase = 0.0;
};

void audioCallback(void* userdata, Uint8* stream, int len) {
    auto* beeper = static_cast<Beeper*>(userdata);
    auto* samples = reinterpret_cast<Sint16*>(stream);
    const int count = len / static_cast<int>(sizeof(Sint16));
    const double step = 440.0 / kSampleRate;

    for (int i = 0; i < count; ++i) {
        if (beeper->on) {
            samples[i] = beeper->phase < 0.5 ? 2000 : -2000;
            beeper->phase = std::fmod(beeper->phase + step, 1.0);
        } else {
            samples[i] = 0;
        }
    }
}

}  // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::fprintf(stderr, "Usage: %s <rom file>\n", argv[0]);
        return EXIT_FAILURE;
    }

    Chip8 chip8;
    if (!chip8.loadRom(argv[1])) return EXIT_FAILURE;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) != 0) {
        std::fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return EXIT_FAILURE;
    }

    SDL_Window* window = SDL_CreateWindow(
        "CHIP-8", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        Chip8::kWidth * kScale, Chip8::kHeight * kScale, SDL_WINDOW_SHOWN);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    SDL_Texture* texture = SDL_CreateTexture(
        renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING,
        Chip8::kWidth, Chip8::kHeight);

    if (!window || !renderer || !texture) {
        std::fprintf(stderr, "SDL setup failed: %s\n", SDL_GetError());
        SDL_Quit();
        return EXIT_FAILURE;
    }

    // Audio is optional: the emulator still runs if no audio device exists.
    Beeper beeper;
    SDL_AudioSpec want{};
    want.freq = kSampleRate;
    want.format = AUDIO_S16SYS;
    want.channels = 1;
    want.samples = 512;
    want.callback = audioCallback;
    want.userdata = &beeper;
    SDL_AudioDeviceID audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0);
    if (audio) SDL_PauseAudioDevice(audio, 0);

    const Uint64 frequency = SDL_GetPerformanceFrequency();
    const double cpuStep = 1.0 / kInstructionsPerSecond;
    const double timerStep = 1.0 / kTimerHz;
    double cpuAccumulator = 0.0;
    double timerAccumulator = 0.0;
    Uint64 last = SDL_GetPerformanceCounter();

    bool running = true;
    bool paused = false;

    while (running) {
        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = false;
            } else if (event.type == SDL_KEYDOWN || event.type == SDL_KEYUP) {
                const bool down = event.type == SDL_KEYDOWN;
                const SDL_Keycode key = event.key.keysym.sym;

                if (down && key == SDLK_ESCAPE) running = false;
                if (down && key == SDLK_p) paused = !paused;

                const int chipKey = mapKey(key);
                if (chipKey >= 0) chip8.setKey(chipKey, down);
            }
        }

        // Run the CPU and timers at fixed rates, independent of frame rate.
        const Uint64 now = SDL_GetPerformanceCounter();
        const double elapsed = static_cast<double>(now - last) / frequency;
        last = now;

        if (!paused) {
            cpuAccumulator += elapsed;
            timerAccumulator += elapsed;

            while (cpuAccumulator >= cpuStep) {
                chip8.cycle();
                cpuAccumulator -= cpuStep;
            }
            while (timerAccumulator >= timerStep) {
                chip8.tickTimers();
                timerAccumulator -= timerStep;
            }
        }

        if (audio) {
            SDL_LockAudioDevice(audio);
            beeper.on = chip8.soundOn() && !paused;
            SDL_UnlockAudioDevice(audio);
        }

        // Draw the 64x32 display, scaled up to the window.
        Uint32 pixels[Chip8::kWidth * Chip8::kHeight];
        const auto& display = chip8.display();
        for (int i = 0; i < Chip8::kWidth * Chip8::kHeight; ++i) {
            pixels[i] = display[i] ? 0xFF33FF66 : 0xFF101010;
        }
        SDL_UpdateTexture(texture, nullptr, pixels, Chip8::kWidth * sizeof(Uint32));
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, nullptr, nullptr);
        SDL_RenderPresent(renderer);

        SDL_Delay(1);
    }

    if (audio) SDL_CloseAudioDevice(audio);
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return EXIT_SUCCESS;
}
