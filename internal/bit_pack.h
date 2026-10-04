// Copyright 2026
#ifndef FPNGE_INTERNAL_BIT_PACK_H_
#define FPNGE_INTERNAL_BIT_PACK_H_

#include <cstddef>
#include <cstdint>

namespace fpnge_internal {

// Packs exactly four LSB-first codes without a per-symbol loop. The native
// NEON literal path uses this for full vectors, where FPNGE's literal code
// lengths are at most 12 bits and therefore each group is at most 48 bits.
template <typename Bits>
inline uint64_t PackFourCodes(const uint8_t *nbits, const Bits *bits,
                              uint32_t *packed_nbits) {
  const uint32_t n0 = nbits[0];
  const uint32_t n1 = nbits[1];
  const uint32_t n2 = nbits[2];
  const uint32_t n3 = nbits[3];

  const uint32_t s1 = n0;
  const uint32_t s2 = n0 + n1;
  const uint32_t s3 = n0 + n1 + n2;
  *packed_nbits = s3 + n3;

  const auto mask = [](uint32_t n) -> uint64_t {
    return n == 0 ? 0 : ((uint64_t{1} << n) - 1);
  };
  return (static_cast<uint64_t>(bits[0]) & mask(n0)) |
         ((static_cast<uint64_t>(bits[1]) & mask(n1)) << s1) |
         ((static_cast<uint64_t>(bits[2]) & mask(n2)) << s2) |
         ((static_cast<uint64_t>(bits[3]) & mask(n3)) << s3);
}

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
