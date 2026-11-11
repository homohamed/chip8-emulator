#include "chip8.hpp"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <vector>

namespace {

constexpr uint16_t kFontStart = 0x050;
constexpr uint16_t kProgramStart = 0x200;

// Built-in hex font: 16 characters (0-F), 5 bytes each, 4x5 pixels.
constexpr std::array<uint8_t, 80> kFont = {
    0xF0, 0x90, 0x90, 0x90, 0xF0,  // 0
    0x20, 0x60, 0x20, 0x20, 0x70,  // 1
    0xF0, 0x10, 0xF0, 0x80, 0xF0,  // 2
    0xF0, 0x10, 0xF0, 0x10, 0xF0,  // 3
    0x90, 0x90, 0xF0, 0x10, 0x10,  // 4
    0xF0, 0x80, 0xF0, 0x10, 0xF0,  // 5
    0xF0, 0x80, 0xF0, 0x90, 0xF0,  // 6
    0xF0, 0x10, 0x20, 0x40, 0x40,  // 7
    0xF0, 0x90, 0xF0, 0x90, 0xF0,  // 8
    0xF0, 0x90, 0xF0, 0x10, 0xF0,  // 9
    0xF0, 0x90, 0xF0, 0x90, 0x90,  // A
    0xE0, 0x90, 0xE0, 0x90, 0xE0,  // B
    0xF0, 0x80, 0x80, 0x80, 0xF0,  // C
    0xE0, 0x90, 0x90, 0x90, 0xE0,  // D
    0xF0, 0x80, 0xF0, 0x80, 0xF0,  // E
    0xF0, 0x80, 0xF0, 0x80, 0x80,  // F
};

}  // namespace

Chip8::Chip8() : rng_(std::random_device{}()) {
    for (size_t i = 0; i < kFont.size(); ++i) {
        memory_[kFontStart + i] = kFont[i];
    }
}

bool Chip8::loadRom(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::fprintf(stderr, "Could not open ROM: %s\n", path.c_str());
        return false;
    }

    std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                              std::istreambuf_iterator<char>());

    if (data.empty() || data.size() > memory_.size() - kProgramStart) {
        std::fprintf(stderr, "ROM is empty or too large (%zu bytes)\n", data.size());
        return false;
    }

    std::copy(data.begin(), data.end(), memory_.begin() + kProgramStart);
    return true;
}

void Chip8::tickTimers() {
    if (delayTimer_ > 0) --delayTimer_;
    if (soundTimer_ > 0) --soundTimer_;
}

void Chip8::cycle() {
    // Fetch: each instruction is 2 bytes, stored big-endian.
    uint16_t opcode = static_cast<uint16_t>(memory_[pc_ & 0xFFF] << 8 |
                                            memory_[(pc_ + 1) & 0xFFF]);
    pc_ += 2;

    execute(opcode);
}

void Chip8::execute(uint16_t opcode) {
    // Decode: split the opcode into the fields instructions use.
    const uint8_t x = (opcode >> 8) & 0x0F;   // second nibble: register index
    const uint8_t y = (opcode >> 4) & 0x0F;   // third nibble: register index
    const uint8_t n = opcode & 0x000F;        // fourth nibble: 4-bit number
    const uint8_t nn = opcode & 0x00FF;       // second byte: 8-bit number
    const uint16_t nnn = opcode & 0x0FFF;     // last 12 bits: memory address

    switch (opcode & 0xF000) {
        case 0x0000:
            if (opcode == 0x00E0) {                 // 00E0: clear screen
                display_.fill(0);
            } else if (opcode == 0x00EE) {          // 00EE: return from subroutine
                if (sp_ == 0) { std::fprintf(stderr, "Stack underflow\n"); return; }
                pc_ = stack_[--sp_];
            }
            // 0NNN (machine code routine) is ignored by modern emulators.
            break;

        case 0x1000:                                // 1NNN: jump
            pc_ = nnn;
            break;

        case 0x2000:                                // 2NNN: call subroutine
            if (sp_ >= stack_.size()) { std::fprintf(stderr, "Stack overflow\n"); return; }
            stack_[sp_++] = pc_;
            pc_ = nnn;
            break;

        case 0x3000:                                // 3XNN: skip if VX == NN
            if (V_[x] == nn) pc_ += 2;
            break;

        case 0x4000:                                // 4XNN: skip if VX != NN
            if (V_[x] != nn) pc_ += 2;
            break;

        case 0x5000:                                // 5XY0: skip if VX == VY
            if (V_[x] == V_[y]) pc_ += 2;
            break;

        case 0x6000:                                // 6XNN: VX = NN
            V_[x] = nn;
            break;

        case 0x7000:                                // 7XNN: VX += NN (no carry flag)
            V_[x] = static_cast<uint8_t>(V_[x] + nn);
            break;

        case 0x8000: {                              // 8XYN: arithmetic and logic
            uint8_t flag = 0;
            switch (n) {
                case 0x0: V_[x] = V_[y]; return;
                case 0x1: V_[x] |= V_[y]; return;
                case 0x2: V_[x] &= V_[y]; return;
                case 0x3: V_[x] ^= V_[y]; return;
                case 0x4: {                         // add with carry
                    uint16_t sum = V_[x] + V_[y];
                    flag = sum > 0xFF;
                    V_[x] = static_cast<uint8_t>(sum);
                    break;
                }
                case 0x5:                           // VX = VX - VY, VF = no borrow
                    flag = V_[x] >= V_[y];
                    V_[x] = static_cast<uint8_t>(V_[x] - V_[y]);
                    break;
                case 0x6:                           // shift right, VF = bit shifted out
                    flag = V_[x] & 0x01;
                    V_[x] >>= 1;
                    break;
                case 0x7:                           // VX = VY - VX, VF = no borrow
                    flag = V_[y] >= V_[x];
                    V_[x] = static_cast<uint8_t>(V_[y] - V_[x]);
                    break;
                case 0xE:                           // shift left, VF = bit shifted out
                    flag = (V_[x] >> 7) & 0x01;
                    V_[x] = static_cast<uint8_t>(V_[x] << 1);
                    break;
                default:
                    reportUnknown(opcode);
                    return;
            }
            // VF is written last so it holds the flag even when X is F.
            V_[0xF] = flag;
            break;
        }

        case 0x9000:                                // 9XY0: skip if VX != VY
            if (V_[x] != V_[y]) pc_ += 2;
            break;

        case 0xA000:                                // ANNN: I = NNN
            I_ = nnn;
            break;

        case 0xB000:                                // BNNN: jump to NNN + V0
            pc_ = static_cast<uint16_t>(nnn + V_[0]);
            break;

        case 0xC000: {                              // CXNN: VX = random & NN
            std::uniform_int_distribution<int> dist(0, 255);
            V_[x] = static_cast<uint8_t>(dist(rng_)) & nn;
            break;
        }

        case 0xD000: {                              // DXYN: draw sprite
            // The start position wraps; pixels past the edge are clipped.
            const int startX = V_[x] % kWidth;
            const int startY = V_[y] % kHeight;
            V_[0xF] = 0;

            for (int row = 0; row < n; ++row) {
                const int py = startY + row;
                if (py >= kHeight) break;
                const uint8_t spriteByte = memory_[(I_ + row) & 0xFFF];

                for (int col = 0; col < 8; ++col) {
                    const int px = startX + col;
                    if (px >= kWidth) break;
                    if ((spriteByte & (0x80 >> col)) == 0) continue;

                    uint8_t& pixel = display_[py * kWidth + px];
                    if (pixel) V_[0xF] = 1;         // collision: a pixel was erased
                    pixel ^= 1;
                }
            }
            break;
        }

        case 0xE000:
            if (nn == 0x9E) {                       // EX9E: skip if key VX pressed
                if (keys_[V_[x] & 0xF]) pc_ += 2;
            } else if (nn == 0xA1) {                // EXA1: skip if key VX not pressed
                if (!keys_[V_[x] & 0xF]) pc_ += 2;
            } else {
                reportUnknown(opcode);
            }
            break;

        case 0xF000:
            switch (nn) {
                case 0x07: V_[x] = delayTimer_; break;          // FX07
                case 0x15: delayTimer_ = V_[x]; break;          // FX15
                case 0x18: soundTimer_ = V_[x]; break;          // FX18
                case 0x1E: I_ = (I_ + V_[x]) & 0xFFF; break;    // FX1E

                case 0x0A: {                        // FX0A: wait for a key press and release
                    if (waitingKey_ < 0) {
                        for (int k = 0; k < 16; ++k) {
                            if (keys_[k]) { waitingKey_ = k; break; }
                        }
                    } else if (!keys_[waitingKey_]) {
                        V_[x] = static_cast<uint8_t>(waitingKey_);
                        waitingKey_ = -1;
                        break;                      // key released: continue
                    }
                    pc_ -= 2;                       // repeat this instruction
                    break;
                }

                case 0x29:                          // FX29: I = font sprite for digit VX
                    I_ = kFontStart + (V_[x] & 0xF) * 5;
                    break;

                case 0x33:                          // FX33: store BCD of VX at I, I+1, I+2
                    memory_[I_ & 0xFFF] = V_[x] / 100;
                    memory_[(I_ + 1) & 0xFFF] = (V_[x] / 10) % 10;
                    memory_[(I_ + 2) & 0xFFF] = V_[x] % 10;
                    break;

                case 0x55:                          // FX55: store V0..VX at I
                    for (int i = 0; i <= x; ++i) memory_[(I_ + i) & 0xFFF] = V_[i];
                    break;

                case 0x65:                          // FX65: load V0..VX from I
                    for (int i = 0; i <= x; ++i) V_[i] = memory_[(I_ + i) & 0xFFF];
                    break;

                default:
                    reportUnknown(opcode);
            }
            break;
    }
}

void Chip8::reportUnknown(uint16_t opcode) {
    std::fprintf(stderr, "Unknown opcode: 0x%04X\n", opcode);
}
