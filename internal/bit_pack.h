// Copyright 2026
#ifndef FPNGE_INTERNAL_BIT_PACK_H_
#define FPNGE_INTERNAL_BIT_PACK_H_

#include <cstddef>
#include <cstdint>

namespace fpnge_internal {

template <typename Bits, typename Write>
inline void PackCodeSequence(const uint8_t *nbits, const Bits *bits,
                             size_t count, Write &&write) {
  uint64_t packed = 0;
  uint32_t packed_nbits = 0;
  for (size_t i = 0; i < count; ++i) {
    const uint32_t n = nbits[i];
    const uint64_t code =
        n == 0 ? 0 : (static_cast<uint64_t>(bits[i]) &
                      ((uint64_t{1} << n) - 1));
    if (packed_nbits + n > 48) {
      write(packed_nbits, packed);
      packed = 0;
      packed_nbits = 0;
    }
    packed |= code << packed_nbits;
    packed_nbits += n;
  }
  if (packed_nbits != 0) write(packed_nbits, packed);
}

}  // namespace fpnge_internal

#endif  // FPNGE_INTERNAL_BIT_PACK_H_
