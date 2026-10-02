// Test the portable Crc32 implementation directly. Include the implementation
// into this test translation unit so the anonymous-namespace Crc32 type remains
// internal to production code while still being directly exercisable here.
#include "../fpnge.cc"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace {
uint32_t ReferenceCrc32(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
  }
  return ~crc;
}

uint32_t Next(uint32_t* state) {
  uint32_t v = *state;
  v ^= v << 13;
  v ^= v >> 17;
  v ^= v << 5;
  *state = v;
  return v;
}
}

int main() {
#ifdef __PCLMUL__
#error crc_portable_test must be compiled without PCLMUL so it exercises the portable backend
#endif
  size_t cases = 0;
  for (size_t len = 0; len <= 1024; ++len) {
    for (size_t offset = 0; offset < 32; ++offset) {
      std::vector<uint8_t> bytes(offset + len + 32);
      uint32_t state = 0x12345678u ^ uint32_t(len * 17 + offset);
      for (uint8_t& b : bytes) b = uint8_t(Next(&state));
      const uint8_t* data = bytes.data() + offset;
      const uint32_t expected = ReferenceCrc32(data, len);
      const uint32_t direct = Crc32().update_final(data, len);
      if (direct != expected) {
        std::fprintf(stderr, "CRC direct mismatch len=%zu offset=%zu expected=%08x got=%08x\n",
                     len, offset, expected, direct);
        return 1;
      }
      Crc32 incremental;
      size_t consumed = 0;
      while (len - consumed >= 8) {
        const size_t request = ((consumed / 8) % 5 + 1) * 8;
        const size_t block = request < len - consumed ? request : (len - consumed) & ~size_t(7);
        if (block == 0) break;
        const size_t used = incremental.update(data + consumed, block);
        if (used != block) {
          std::fprintf(stderr, "CRC update consumption mismatch len=%zu offset=%zu\n", len, offset);
          return 1;
        }
        consumed += used;
      }
      const uint32_t split = incremental.update_final(data + consumed, len - consumed);
      if (split != expected) {
        std::fprintf(stderr, "CRC incremental mismatch len=%zu offset=%zu expected=%08x got=%08x\n",
                     len, offset, expected, split);
        return 1;
      }
      ++cases;
    }
  }
  static constexpr uint8_t kCheck[] = {'1','2','3','4','5','6','7','8','9'};
  if (Crc32().update_final(kCheck, sizeof(kCheck)) != 0xcbf43926u) {
    std::fprintf(stderr, "CRC known-vector mismatch\n");
    return 1;
  }
  std::printf("portable CRC32: %zu randomized length/alignment cases passed\n", cases);
  return 0;
}
