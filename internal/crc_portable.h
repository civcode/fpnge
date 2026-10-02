// Copyright 2026
#ifndef FPNGE_INTERNAL_CRC_PORTABLE_H_
#define FPNGE_INTERNAL_CRC_PORTABLE_H_

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace fpnge_internal {
namespace crc_portable_detail {

template <std::size_t Length, typename Generator, std::size_t... Indexes>
constexpr auto LutImpl(Generator&& f, std::index_sequence<Indexes...>) {
  using content_type = decltype(f(std::size_t{0}));
  return std::array<content_type, Length>{{f(Indexes)...}};
}

template <std::size_t Length, typename Generator>
constexpr auto Lut(Generator&& f) {
  return LutImpl<Length>(std::forward<Generator>(f),
                         std::make_index_sequence<Length>{});
}

constexpr uint32_t Crc32Slice8Gen(unsigned n) {
  uint32_t crc = n & 0xff;
  for (int i = n >> 8; i >= 0; i--) {
    for (int j = 0; j < 8; j++) {
      crc = (crc >> 1) ^ ((crc & 1) * 0xEDB88320);
    }
  }
  return crc;
}

static constexpr auto kCrcSlice8LUT = Lut<256 * 8>(Crc32Slice8Gen);

inline uint32_t ProcessIter(uint32_t crc, const uint32_t* current) {
  uint32_t one = *current++ ^ crc;
  uint32_t two = *current;
  return kCrcSlice8LUT[(two >> 24) & 0xFF] ^
         kCrcSlice8LUT[0x100 + ((two >> 16) & 0xFF)] ^
         kCrcSlice8LUT[0x200 + ((two >> 8) & 0xFF)] ^
         kCrcSlice8LUT[0x300 + (two & 0xFF)] ^
         kCrcSlice8LUT[0x400 + ((one >> 24) & 0xFF)] ^
         kCrcSlice8LUT[0x500 + ((one >> 16) & 0xFF)] ^
         kCrcSlice8LUT[0x600 + ((one >> 8) & 0xFF)] ^
         kCrcSlice8LUT[0x700 + (one & 0xFF)];
}

}  // namespace crc_portable_detail

class Crc32Portable {
 public:
  Crc32Portable() : state_(0xffffffff) {}

  size_t update(const unsigned char* data, size_t len) {
    const size_t amount = len & ~size_t{7};
    for (size_t i = 0; i < amount; i += 8) {
      state_ = crc_portable_detail::ProcessIter(
          state_, reinterpret_cast<const uint32_t*>(data + i));
    }
    return amount;
  }

  uint32_t update_final(const unsigned char* data, size_t len) {
    size_t i = update(data, len);
    for (; i < len; i++) {
      state_ = (state_ >> 8) ^
               crc_portable_detail::kCrcSlice8LUT[(state_ & 0xFF) ^ data[i]];
    }
    return ~state_;
  }

 private:
  uint32_t state_;
};

}  // namespace fpnge_internal

#endif  // FPNGE_INTERNAL_CRC_PORTABLE_H_
