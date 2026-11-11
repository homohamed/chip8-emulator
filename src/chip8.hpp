#pragma once

#include <array>
#include <cstdint>
#include <random>
#include <string>

// A CHIP-8 virtual machine: memory, registers, timers, display and keypad.
class Chip8 {
public:
    static constexpr int kWidth = 64;
    static constexpr int kHeight = 32;

    Chip8();

    bool loadRom(const std::string& path);
    void cycle();       // fetch, decode and execute one instruction
    void tickTimers();  // call 60 times per second

    const std::array<uint8_t, kWidth * kHeight>& display() const { return display_; }
    bool soundOn() const { return soundTimer_ > 0; }
    void setKey(int key, bool pressed) { keys_[key & 0xF] = pressed; }

private:
    void execute(uint16_t opcode);
    static void reportUnknown(uint16_t opcode);

    std::array<uint8_t, 4096> memory_{};
    std::array<uint8_t, 16> V_{};          // registers V0-VF (VF is the flag register)
    uint16_t I_ = 0;                       // index register
    uint16_t pc_ = 0x200;                  // program counter
    std::array<uint16_t, 16> stack_{};
    uint8_t sp_ = 0;                       // stack pointer
    uint8_t delayTimer_ = 0;
    uint8_t soundTimer_ = 0;
    std::array<uint8_t, kWidth * kHeight> display_{};
    std::array<bool, 16> keys_{};
    int waitingKey_ = -1;                  // used by FX0A (wait for key)
    std::mt19937 rng_;
};
