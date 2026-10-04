// Copyright 2026
#include <cstdint>
#include <cstdio>
#include <vector>

#include "internal/bit_pack.h"

namespace {

uint32_t Next(uint32_t &state) {
  state = state * 1664525u + 1013904223u;
  return state;
}

bool RunOne(uint32_t &state, size_t count) {
  std::vector<uint8_t> nbits(count);
  std::vector<uint16_t> bits(count);
  std::vector<uint8_t> expected;
  std::vector<uint8_t> actual;

  for (size_t i = 0; i < count; ++i) {
    nbits[i] = static_cast<uint8_t>(1 + Next(state) % 15);
    bits[i] = static_cast<uint16_t>(Next(state));
    for (uint32_t b = 0; b < nbits[i]; ++b) {
      expected.push_back((bits[i] >> b) & 1u);
    }
  }

  fpnge_internal::PackCodeSequence(
      nbits.data(), bits.data(), count,
      [&](uint32_t n, uint64_t packed) {
        if (n > 48) return;
        for (uint32_t b = 0; b < n; ++b) {
          actual.push_back((packed >> b) & 1u);
        }
      });

  if (expected != actual) return false;

  for (size_t i = 0; i + 4 <= count; i += 4) {
    uint32_t packed_nbits = 0;
    const uint64_t packed = fpnge_internal::PackFourCodes(
        nbits.data() + i, bits.data() + i, &packed_nbits);
    uint32_t expected_nbits = 0;
    uint64_t expected_packed = 0;
    for (size_t j = 0; j < 4; ++j) {
      const uint32_t n = nbits[i + j];
      const uint64_t mask = (uint64_t{1} << n) - 1;
      expected_packed |=
          (static_cast<uint64_t>(bits[i + j]) & mask) << expected_nbits;
      expected_nbits += n;
    }
    if (packed_nbits != expected_nbits || packed != expected_packed) {
      return false;
    }
  }
  return true;
}

}  // namespace

int main() {
  uint32_t state = 0x4d34504bu;
  size_t cases = 0;
  for (size_t count = 0; count <= 64; ++count) {
    for (size_t rep = 0; rep < 257; ++rep) {
      ++cases;
      if (!RunOne(state, count)) {
        std::fprintf(stderr, "bit pack mismatch: count=%zu rep=%zu\n",
                     count, rep);
        return 1;
      }
    }
  }
  std::printf("semantic bit pack: %zu randomized sequences passed\n", cases);
  return 0;
}
