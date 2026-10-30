#pragma once

#include <cstdint>

namespace cultivation {

// Deterministic randomness (DOC: "No clock and no true randomness inside
// generation, so a bug report can be reproduced from the seed").
//
// mix32 combines values into a seed; Rng is xorshift32 with 4 bytes of state.
constexpr uint32_t mix32(uint32_t x) {
  // splitmix32 / murmur3 finalizer
  x ^= x >> 16;
  x *= 0x7FEB352Du;
  x ^= x >> 15;
  x *= 0x846CA68Bu;
  x ^= x >> 16;
  return x;
}

constexpr uint32_t mix32(const uint32_t a, const uint32_t b) { return mix32(a ^ mix32(b + 0x9E3779B9u)); }

// Content key for "any piece of content = hash(world seed, kind, index)".
constexpr uint32_t contentSeed(const uint32_t worldSeed, const uint32_t kind, const uint32_t index) {
  return mix32(mix32(worldSeed, kind), index);
}

class Rng {
 public:
  explicit constexpr Rng(const uint32_t seed) : state_(seed != 0 ? seed : 0x6D2B79F5u) {}

  uint32_t next() {
    uint32_t x = state_;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    state_ = x;
    return x;
  }

  // Uniform in [0, bound) without modulo bias worth caring about at these
  // sizes (bound <= a few hundred): 64-bit multiply-shift.
  uint32_t below(const uint32_t bound) {
    return bound == 0 ? 0 : static_cast<uint32_t>((static_cast<uint64_t>(next()) * bound) >> 32);
  }

  // Uniform in [lo, hi].
  uint32_t range(const uint32_t lo, const uint32_t hi) { return lo + below(hi - lo + 1); }

 private:
  uint32_t state_;
};

}  // namespace cultivation
