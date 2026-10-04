// Copyright 2026
#ifndef FPNGE_INTERNAL_KERNELS_NEON_H_
#define FPNGE_INTERNAL_KERNELS_NEON_H_

#if !FPNGE_ARCH_AARCH64
#error "The native NEON kernel backend is only valid on AArch64"
#endif

namespace neon {

constexpr size_t kSimdWidth = 16;

static FORCE_INLINE uint8x16_t PrefixMask(size_t count) {
  alignas(16) uint8_t mask[16] = {};
  memset(mask, 0xff, count);
  return vld1q_u8(mask);
}

static FORCE_INLINE bool AllZeroPrefix(uint8x16_t value, size_t count) {
  if (count == 16) {
    return vmaxvq_u8(value) == 0;
  }
  alignas(16) uint8_t bytes[16];
  vst1q_u8(bytes, value);
  for (size_t i = 0; i < count; ++i) {
    if (bytes[i] != 0) return false;
  }
  return true;
}

static FORCE_INLINE bool AllMaskPrefix(uint8x16_t value, size_t count) {
  if (count == 16) {
    return vminvq_u8(value) == 0xff;
  }
  alignas(16) uint8_t bytes[16];
  vst1q_u8(bytes, value);
  for (size_t i = 0; i < count; ++i) {
    if (bytes[i] != 0xff) return false;
  }
  return true;
}

template <size_t predictor>
static FORCE_INLINE uint8x16_t
PredictVec(const unsigned char *current_buf, const unsigned char *top_buf,
           const unsigned char *left_buf, const unsigned char *topleft_buf) {
  const uint8x16_t data = vld1q_u8(current_buf);
  if constexpr (predictor == 0) {
    return data;
  } else if constexpr (predictor == 1) {
    return vsubq_u8(data, vld1q_u8(left_buf));
  } else if constexpr (predictor == 2) {
    return vsubq_u8(data, vld1q_u8(top_buf));
  } else if constexpr (predictor == 3) {
    // PNG Average is floor((left + top) / 2).
    return vsubq_u8(data, vhaddq_u8(vld1q_u8(left_buf), vld1q_u8(top_buf)));
  } else {
    static_assert(predictor == 4, "invalid PNG predictor");
    const uint8x16_t left = vld1q_u8(left_buf);
    const uint8x16_t top = vld1q_u8(top_buf);
    const uint8x16_t c = vld1q_u8(topleft_buf);

    // Equivalent to the upstream Paeth distance formulation, with tie order
    // left (a), top (b), upper-left (c).
    const uint8x16_t a = vminq_u8(left, top);
    const uint8x16_t b = vmaxq_u8(left, top);
    const uint8x16_t pa = vqsubq_u8(b, c);
    const uint8x16_t pb = vqsubq_u8(c, a);
    const uint8x16_t min_pab = vminq_u8(pa, pb);
    const uint8x16_t pc = vsubq_u8(vmaxq_u8(pa, pb), min_pab);
    const uint8x16_t min_pabc = vminq_u8(min_pab, pc);
    const uint8x16_t use_a = vceqq_u8(min_pabc, pa);
    const uint8x16_t use_b = vceqq_u8(min_pabc, pb);
    const uint8x16_t pred =
        vbslq_u8(use_a, a, vbslq_u8(use_b, b, c));
    return vsubq_u8(data, pred);
  }
}

template <size_t predictor, typename CB, typename CB_ADL, typename CB_RLE>
static void ProcessRow(size_t bytes_per_line,
                       const unsigned char *current_row_buf,
                       const unsigned char *top_buf,
                       const unsigned char *left_buf,
                       const unsigned char *topleft_buf, CB &&cb,
                       CB_ADL &&cb_adl, CB_RLE &&cb_rle) {
  size_t run = 0;
  size_t i = 0;
  for (; i + kSimdWidth <= bytes_per_line; i += kSimdWidth) {
    const uint8x16_t pdata = [&]() {
      if constexpr (predictor == 0) {
        return PredictVec<0>(current_row_buf + i, nullptr, nullptr, nullptr);
      } else {
        return PredictVec<predictor>(current_row_buf + i, top_buf + i,
                                     left_buf + i, topleft_buf + i);
      }
    }();
    if (AllZeroPrefix(pdata, kSimdWidth)) {
      run += kSimdWidth;
    } else {
      if (run != 0) cb_rle(run);
      run = 0;
      cb(pdata, kSimdWidth);
    }
    cb_adl(pdata, kSimdWidth, i);
  }

  const size_t remaining = bytes_per_line - i;
  if (remaining != 0) {
    const uint8x16_t pdata = [&]() {
      if constexpr (predictor == 0) {
        return PredictVec<0>(current_row_buf + i, nullptr, nullptr, nullptr);
      } else {
        return PredictVec<predictor>(current_row_buf + i, top_buf + i,
                                     left_buf + i, topleft_buf + i);
      }
    }();
    if (AllZeroPrefix(pdata, remaining) && run + remaining >= 16) {
      run += remaining;
    } else {
      if (run != 0) cb_rle(run);
      run = 0;
      cb(pdata, remaining);
    }
    cb_adl(pdata, remaining, i);
  }

  if (run != 0) cb_rle(run);
}

template <typename CB, typename CB_ADL, typename CB_RLE>
static void ProcessRow(uint8_t predictor, size_t bytes_per_line,
                       const unsigned char *current_row_buf,
                       const unsigned char *top_buf,
                       const unsigned char *left_buf,
                       const unsigned char *topleft_buf, CB &&cb,
                       CB_ADL &&cb_adl, CB_RLE &&cb_rle) {
  switch (predictor) {
    case 1:
      ProcessRow<1>(bytes_per_line, current_row_buf, top_buf, left_buf,
                    topleft_buf, cb, cb_adl, cb_rle);
      return;
    case 2:
      ProcessRow<2>(bytes_per_line, current_row_buf, top_buf, left_buf,
                    topleft_buf, cb, cb_adl, cb_rle);
      return;
    case 3:
      ProcessRow<3>(bytes_per_line, current_row_buf, top_buf, left_buf,
                    topleft_buf, cb, cb_adl, cb_rle);
      return;
    case 4:
      ProcessRow<4>(bytes_per_line, current_row_buf, top_buf, left_buf,
                    topleft_buf, cb, cb_adl, cb_rle);
      return;
    default:
      assert(predictor == 0);
      ProcessRow<0>(bytes_per_line, current_row_buf, top_buf, left_buf,
                    topleft_buf, cb, cb_adl, cb_rle);
      return;
  }
}

template <typename CB>
static void ForAllRLESymbols(size_t length, CB &&cb) {
  assert(length >= 4);
  length -= 1;
  if (length < 258) {
    cb(length, 1);
    return;
  }

  auto runs = length / 258;
  auto remain = length % 258;
  if (remain == 1 || remain == 2) {
    remain += 258 - 3;
    --runs;
    cb(3, 1);
  }
  if (runs) cb(258, runs);
  if (remain) cb(remain, 1);
}

static FORCE_INLINE uint8x16_t LiteralRangeMask(uint8x16_t bytes) {
  const int8x16_t signed_bytes = vreinterpretq_s8_u8(bytes);
  return vandq_u8(vcgeq_s8(signed_bytes, vdupq_n_s8(-16)),
                  vcleq_s8(signed_bytes, vdupq_n_s8(15)));
}

static FORCE_INLINE uint8x16_t LiteralNBits(uint8x16_t bytes,
                                            const HuffmanTable &table) {
  const uint8x16_t index = vandq_u8(bytes, vdupq_n_u8(0x0f));
  const uint8x16_t low =
      vqtbl1q_u8(vld1q_u8(table.first16_nbits), index);
  const uint8x16_t high =
      vqtbl1q_u8(vld1q_u8(table.last16_nbits), index);
  const uint8x16_t negative =
      vcltq_s8(vreinterpretq_s8_u8(bytes), vdupq_n_s8(0));
  const uint8x16_t low_high = vbslq_u8(negative, high, low);
  return vbslq_u8(LiteralRangeMask(bytes), low_high,
                  vdupq_n_u8(table.mid_nbits));
}

static FORCE_INLINE size_t SumPrefixU8(uint8x16_t value, size_t count) {
  if (count != 16) value = vandq_u8(value, PrefixMask(count));
  return vaddlvq_u8(value);
}

template <size_t pred, bool store_pred>
static void TryPredictor(size_t bytes_per_line,
                         const unsigned char *current_row_buf,
                         const unsigned char *top_buf,
                         const unsigned char *left_buf,
                         const unsigned char *topleft_buf,
                         unsigned char *predicted_data,
                         const HuffmanTable &table, size_t &best_cost,
                         uint8_t &predictor) {
  size_t direct_cost = 0;
  size_t rle_cost = 0;

  auto cost_chunk_cb = [&](uint8x16_t bytes, size_t bytes_in_vec) {
    direct_cost += SumPrefixU8(LiteralNBits(bytes, table), bytes_in_vec);
  };
  auto adler_cb = [=](uint8x16_t pdata, size_t bytes_in_vec, size_t i) {
    if constexpr (store_pred) {
      alignas(16) uint8_t tmp[16];
      vst1q_u8(tmp, pdata);
      memcpy(predicted_data + i, tmp, bytes_in_vec);
    }
  };
  auto rle_cb = [&](size_t run) {
    rle_cost += table.first16_nbits[0];
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      rle_cost +=
          (table.dist_nbits + table.lz77_length_nbits[len]) * count;
    });
  };

  ProcessRow<pred>(bytes_per_line, current_row_buf, top_buf, left_buf,
                   topleft_buf, cost_chunk_cb, adler_cb, rle_cb);
  const size_t cost = direct_cost + rle_cost;
  if (cost < best_cost) {
    best_cost = cost;
    predictor = pred;
  }
}

static FORCE_INLINE size_t ApproxCost(uint8x16_t pdata,
                                      uint8x16_t bit_costs,
                                      size_t count) {
  const int8x16_t signed_data = vreinterpretq_s8_u8(pdata);
  const uint8x16_t abs_data =
      vreinterpretq_u8_s8(vabsq_s8(signed_data));
  const uint8x16_t index = vminq_u8(abs_data, vdupq_n_u8(15));
  return SumPrefixU8(vqtbl1q_u8(bit_costs, index), count);
}

static uint8_t SelectPredictor(size_t bytes_per_line,
                               const unsigned char *current_row_buf,
                               const unsigned char *top_buf,
                               const unsigned char *left_buf,
                               const unsigned char *topleft_buf,
                               unsigned char *paeth_data,
                               const HuffmanTable &table,
                               const struct FPNGEOptions *options) {
  if (options->predictor <= 4) return options->predictor;

  if (options->predictor == FPNGE_PREDICTOR_APPROX) {
    const uint8x16_t bit_costs = vld1q_u8(table.approx_nbits);
    size_t costs[4] = {};
    size_t i = 0;
    for (; i + kSimdWidth <= bytes_per_line; i += kSimdWidth) {
      costs[0] += ApproxCost(PredictVec<1>(current_row_buf + i, top_buf + i,
                                          left_buf + i, topleft_buf + i),
                             bit_costs, kSimdWidth);
      costs[1] += ApproxCost(PredictVec<2>(current_row_buf + i, top_buf + i,
                                          left_buf + i, topleft_buf + i),
                             bit_costs, kSimdWidth);
      costs[2] += ApproxCost(PredictVec<3>(current_row_buf + i, top_buf + i,
                                          left_buf + i, topleft_buf + i),
                             bit_costs, kSimdWidth);
      const uint8x16_t paeth =
          PredictVec<4>(current_row_buf + i, top_buf + i, left_buf + i,
                        topleft_buf + i);
      costs[3] += ApproxCost(paeth, bit_costs, kSimdWidth);
      vst1q_u8(paeth_data + i, paeth);
    }

    const size_t remaining = bytes_per_line - i;
    if (remaining != 0) {
      costs[0] += ApproxCost(PredictVec<1>(current_row_buf + i, top_buf + i,
                                          left_buf + i, topleft_buf + i),
                             bit_costs, remaining);
      costs[1] += ApproxCost(PredictVec<2>(current_row_buf + i, top_buf + i,
                                          left_buf + i, topleft_buf + i),
                             bit_costs, remaining);
      costs[2] += ApproxCost(PredictVec<3>(current_row_buf + i, top_buf + i,
                                          left_buf + i, topleft_buf + i),
                             bit_costs, remaining);
      const uint8x16_t paeth =
          PredictVec<4>(current_row_buf + i, top_buf + i, left_buf + i,
                        topleft_buf + i);
      costs[3] += ApproxCost(paeth, bit_costs, remaining);
      alignas(16) uint8_t tmp[16];
      vst1q_u8(tmp, paeth);
      memcpy(paeth_data + i, tmp, remaining);
    }

    uint8_t predictor = 1;
    size_t best_cost = costs[0];
    for (uint8_t p = 2; p <= 4; ++p) {
      if (costs[p - 1] < best_cost) {
        best_cost = costs[p - 1];
        predictor = p;
      }
    }
    return predictor;
  }

  assert(options->predictor == FPNGE_PREDICTOR_BEST);
  uint8_t predictor = 1;
  size_t best_cost = static_cast<size_t>(-1);
  TryPredictor<1, false>(bytes_per_line, current_row_buf, top_buf, left_buf,
                         topleft_buf, nullptr, table, best_cost, predictor);
  TryPredictor<2, false>(bytes_per_line, current_row_buf, top_buf, left_buf,
                         topleft_buf, nullptr, table, best_cost, predictor);
  TryPredictor<3, false>(bytes_per_line, current_row_buf, top_buf, left_buf,
                         topleft_buf, nullptr, table, best_cost, predictor);
  TryPredictor<4, true>(bytes_per_line, current_row_buf, top_buf, left_buf,
                        topleft_buf, paeth_data, table, best_cost, predictor);
  return predictor;
}

static FORCE_INLINE void WriteFourLiteralCodes(
    const uint8_t *nbits, const uint16_t *bits,
    BitWriter *__restrict writer) {
  uint32_t count;
  const uint64_t packed =
      fpnge_internal::PackFourCodes(nbits, bits, &count);
  writer->Write(count, packed);
}

static FORCE_INLINE void WriteLiteralChunk(uint8x16_t bytes, size_t count,
                                           const HuffmanTable &table,
                                           BitWriter *__restrict writer) {
  const uint8x16_t low_nibble = vandq_u8(bytes, vdupq_n_u8(0x0f));
  const uint8x16_t high_nibble = vshrq_n_u8(bytes, 4);
  const int8x16_t signed_bytes = vreinterpretq_s8_u8(bytes);
  const uint8x16_t negative = vcltq_s8(signed_bytes, vdupq_n_s8(0));
  const uint8x16_t range = LiteralRangeMask(bytes);

  const uint8x16_t low_nbits =
      vqtbl1q_u8(vld1q_u8(table.first16_nbits), low_nibble);
  const uint8x16_t high_nbits =
      vqtbl1q_u8(vld1q_u8(table.last16_nbits), low_nibble);
  const uint8x16_t low_high_nbits =
      vbslq_u8(negative, high_nbits, low_nbits);
  const uint8x16_t nbits_vec =
      vbslq_u8(range, low_high_nbits, vdupq_n_u8(table.mid_nbits));

  const uint8x16_t low_bits =
      vqtbl1q_u8(vld1q_u8(table.first16_bits), low_nibble);
  const uint8x16_t high_bits =
      vqtbl1q_u8(vld1q_u8(table.last16_bits), low_nibble);
  const uint8x16_t low_high_bits =
      vbslq_u8(negative, high_bits, low_bits);
  const uint8x16_t mid_low_bits =
      vqtbl1q_u8(vld1q_u8(table.mid_lowbits), high_nibble);
  const uint8x16_t bits_low8 =
      vbslq_u8(range, low_high_bits, mid_low_bits);

  const uint8x16_t reversed_nibble =
      vqtbl1q_u8(vld1q_u8(kBitReverseNibbleLookup), low_nibble);
  const uint8x16_t mid_high4 =
      vbslq_u8(range, vdupq_n_u8(0), reversed_nibble);

  const int16x8_t mid_shift =
      vdupq_n_s16(static_cast<int16_t>(table.mid_nbits - 4));
  uint16x8_t bits0 = vmovl_u8(vget_low_u8(bits_low8));
  uint16x8_t bits1 = vmovl_high_u8(bits_low8);
  bits0 = vorrq_u16(
      bits0, vshlq_u16(vmovl_u8(vget_low_u8(mid_high4)), mid_shift));
  bits1 = vorrq_u16(
      bits1, vshlq_u16(vmovl_high_u8(mid_high4), mid_shift));

  alignas(16) uint8_t nbits[16];
  alignas(16) uint16_t bits[16];
  vst1q_u8(nbits, nbits_vec);
  vst1q_u16(bits, bits0);
  vst1q_u16(bits + 8, bits1);

  if (count == 16) {
    WriteFourLiteralCodes(nbits + 0, bits + 0, writer);
    WriteFourLiteralCodes(nbits + 4, bits + 4, writer);
    WriteFourLiteralCodes(nbits + 8, bits + 8, writer);
    WriteFourLiteralCodes(nbits + 12, bits + 12, writer);
    return;
  }

  // Scalar grouping is retained only for the final partial vector of a row.
  fpnge_internal::PackCodeSequence(
      nbits, bits, count,
      [&](uint32_t n, uint64_t packed) { writer->Write(n, packed); });
}

static FORCE_INLINE void UpdateAdlerChunk(uint32_t &s1, uint32_t &s2,
                                          uint8x16_t bytes, size_t count,
                                          uint16_t &bytes_since_flush) {
  static constexpr uint8_t kWeights[16] = {
      16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,
  };

  if (count != 16) bytes = vandq_u8(bytes, PrefixMask(count));

  uint8x16_t weights = vld1q_u8(kWeights);
  if (count != 16) {
    weights = vsubq_u8(weights, vdupq_n_u8(static_cast<uint8_t>(16 - count)));
  }

  const uint32_t byte_sum = vaddlvq_u8(bytes);
  const uint16x8_t prod_lo =
      vmull_u8(vget_low_u8(bytes), vget_low_u8(weights));
  const uint16x8_t prod_hi =
      vmull_high_u8(bytes, weights);
  const uint32_t weighted_sum =
      vaddlvq_u16(prod_lo) + vaddlvq_u16(prod_hi);

  s2 += static_cast<uint32_t>(count) * s1 + weighted_sum;
  s1 += byte_sum;
  bytes_since_flush += static_cast<uint16_t>(count);

  if (bytes_since_flush >= 5500) {
    s1 %= kAdler32Mod;
    s2 %= kAdler32Mod;
    bytes_since_flush = 0;
  }
}

static void EncodeOneRow(size_t bytes_per_line,
                         const unsigned char *current_row_buf,
                         const unsigned char *top_buf,
                         const unsigned char *left_buf,
                         const unsigned char *topleft_buf,
                         unsigned char *paeth_data,
                         const HuffmanTable &table, uint32_t &s1,
                         uint32_t &s2, BitWriter *__restrict writer,
                         const struct FPNGEOptions *options) {
  const uint8_t predictor =
      SelectPredictor(bytes_per_line, current_row_buf, top_buf, left_buf,
                      topleft_buf, paeth_data, table, options);

  writer->Write(table.first16_nbits[predictor],
                table.first16_bits[predictor]);
  UpdateAdler32(s1, s2, predictor);

  uint16_t bytes_since_flush = 1;
  auto encode_chunk_cb = [&](uint8x16_t bytes, size_t bytes_in_vec) {
    WriteLiteralChunk(bytes, bytes_in_vec, table, writer);
  };
  auto adler_chunk_cb = [&](uint8x16_t bytes, size_t bytes_in_vec, size_t) {
    UpdateAdlerChunk(s1, s2, bytes, bytes_in_vec, bytes_since_flush);
  };
  auto encode_rle_cb = [&](size_t run) {
    writer->Write(table.first16_nbits[0], table.first16_bits[0]);
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      const uint32_t bits =
          (table.dist_bits << table.lz77_length_nbits[len]) |
          table.lz77_length_bits[len];
      const uint32_t nbits =
          table.lz77_length_nbits[len] + table.dist_nbits;
      while (count--) writer->Write(nbits, bits);
    });
  };

  if (options->predictor > 4 && predictor == 4) {
    ProcessRow<0>(bytes_per_line, paeth_data, nullptr, nullptr, nullptr,
                  encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  } else {
    ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
               topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  }

  s1 %= kAdler32Mod;
  s2 %= kAdler32Mod;
}

static FORCE_INLINE size_t Lz77LengthSymbol(size_t length) {
  assert(length >= 3 && length <= 258);
  for (size_t i = 0; i < 29; ++i) {
    if (i == 28 || length < kLZ77Base[i + 1]) return 257 + i;
  }
  assert(false);
  return 285;
}

static void CollectSymbolCounts(size_t bytes_per_line,
                                const unsigned char *current_row_buf,
                                const unsigned char *top_buf,
                                const unsigned char *left_buf,
                                const unsigned char *topleft_buf,
                                unsigned char *paeth_data,
                                uint64_t *__restrict symbol_counts,
                                const struct FPNGEOptions *options) {
  auto encode_chunk_cb = [&](uint8x16_t pdata, size_t bytes_in_vec) {
    alignas(16) uint8_t predicted[16];
    vst1q_u8(predicted, pdata);
    for (size_t i = 0; i < bytes_in_vec; ++i) {
      ++symbol_counts[predicted[i]];
    }
  };
  auto adler_chunk_cb = [](uint8x16_t, size_t, size_t) {};
  auto encode_rle_cb = [&](size_t run) {
    ++symbol_counts[0];
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      symbol_counts[Lz77LengthSymbol(len)] += count;
    });
  };

  if (options->predictor == FPNGE_PREDICTOR_APPROX) {
    HuffmanTable dummy_table;
    const uint8_t predictor =
        SelectPredictor(bytes_per_line, current_row_buf, top_buf, left_buf,
                        topleft_buf, paeth_data, dummy_table, options);
    if (predictor == 4) {
      ProcessRow<0>(bytes_per_line, paeth_data, nullptr, nullptr, nullptr,
                    encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
    } else {
      ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
                 topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
    }
  } else {
    const uint8_t predictor =
        options->predictor > 4 ? 4 : options->predictor;
    ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
               topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  }
}

void CopyRow(unsigned char *dst, const unsigned char *src, size_t nb_channels,
             size_t bytes_per_channel, FPNGEColorChannelOrder order,
             size_t width) {
  if (order == FPNGE_ORDER_RGB || nb_channels <= 2) {
    memcpy(dst, src, nb_channels * bytes_per_channel * width);
    return;
  }

  size_t x = 0;
  if (nb_channels == 4 && bytes_per_channel == 1) {
    for (; x + 16 <= width; x += 16) {
      uint8x16x4_t px = vld4q_u8(src + x * 4);
      const uint8x16_t tmp = px.val[0];
      px.val[0] = px.val[2];
      px.val[2] = tmp;
      vst4q_u8(dst + x * 4, px);
    }
  } else if (nb_channels == 4 && bytes_per_channel == 2) {
    static constexpr uint8_t kSwap4x16[16] = {
        4, 5, 2, 3, 0, 1, 6, 7, 12, 13, 10, 11, 8, 9, 14, 15,
    };
    const uint8x16_t shuffle = vld1q_u8(kSwap4x16);
    for (; x + 2 <= width; x += 2) {
      const uint8x16_t px = vld1q_u8(src + x * 8);
      vst1q_u8(dst + x * 8, vqtbl1q_u8(px, shuffle));
    }
  } else if (nb_channels == 3 && bytes_per_channel == 1) {
    for (; x + 16 <= width; x += 16) {
      uint8x16x3_t px = vld3q_u8(src + x * 3);
      const uint8x16_t tmp = px.val[0];
      px.val[0] = px.val[2];
      px.val[2] = tmp;
      vst3q_u8(dst + x * 3, px);
    }
  } else if (nb_channels == 3 && bytes_per_channel == 2) {
    static constexpr uint8_t kS1A[16] = {
        4, 5, 2, 3, 0, 1, 10, 11, 8, 9, 6, 7, 0xff, 0xff, 14, 15,
    };
    static constexpr uint8_t kS1B[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0, 1, 0xff, 0xff,
    };
    static constexpr uint8_t kS2A[16] = {
        12, 13, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    static constexpr uint8_t kS2B[16] = {
        0xff, 0xff, 6, 7, 4, 5, 2, 3, 12, 13, 10, 11, 8, 9, 0xff, 0xff,
    };
    static constexpr uint8_t kS2C[16] = {
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 2, 3,
    };
    static constexpr uint8_t kS3B[16] = {
        0xff, 0xff, 14, 15, 0xff, 0xff, 0xff, 0xff,
        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
    };
    static constexpr uint8_t kS3C[16] = {
        0, 1, 0xff, 0xff, 8, 9, 6, 7, 4, 5, 14, 15, 12, 13, 10, 11,
    };

    const uint8x16_t s1a = vld1q_u8(kS1A);
    const uint8x16_t s1b = vld1q_u8(kS1B);
    const uint8x16_t s2a = vld1q_u8(kS2A);
    const uint8x16_t s2b = vld1q_u8(kS2B);
    const uint8x16_t s2c = vld1q_u8(kS2C);
    const uint8x16_t s3b = vld1q_u8(kS3B);
    const uint8x16_t s3c = vld1q_u8(kS3C);

    for (; x + 8 <= width; x += 8) {
      const uint8x16_t v1 = vld1q_u8(src + x * 6);
      const uint8x16_t v2 = vld1q_u8(src + x * 6 + 16);
      const uint8x16_t v3 = vld1q_u8(src + x * 6 + 32);

      const uint8x16_t out1 =
          vorrq_u8(vqtbl1q_u8(v1, s1a), vqtbl1q_u8(v2, s1b));
      const uint8x16_t out2 =
          vorrq_u8(vorrq_u8(vqtbl1q_u8(v1, s2a),
                            vqtbl1q_u8(v2, s2b)),
                   vqtbl1q_u8(v3, s2c));
      const uint8x16_t out3 =
          vorrq_u8(vqtbl1q_u8(v2, s3b), vqtbl1q_u8(v3, s3c));

      vst1q_u8(dst + x * 6, out1);
      vst1q_u8(dst + x * 6 + 16, out2);
      vst1q_u8(dst + x * 6 + 32, out3);
    }
  }

  for (; x < width; ++x) {
    if (nb_channels == 3 && bytes_per_channel == 1) {
      dst[x * 3] = src[x * 3 + 2];
      dst[x * 3 + 1] = src[x * 3 + 1];
      dst[x * 3 + 2] = src[x * 3];
    } else if (nb_channels == 3 && bytes_per_channel == 2) {
      dst[x * 6] = src[x * 6 + 4];
      dst[x * 6 + 1] = src[x * 6 + 5];
      dst[x * 6 + 2] = src[x * 6 + 2];
      dst[x * 6 + 3] = src[x * 6 + 3];
      dst[x * 6 + 4] = src[x * 6];
      dst[x * 6 + 5] = src[x * 6 + 1];
    } else if (nb_channels == 4 && bytes_per_channel == 1) {
      dst[x * 4] = src[x * 4 + 2];
      dst[x * 4 + 1] = src[x * 4 + 1];
      dst[x * 4 + 2] = src[x * 4];
      dst[x * 4 + 3] = src[x * 4 + 3];
    } else if (nb_channels == 4 && bytes_per_channel == 2) {
      dst[x * 8] = src[x * 8 + 4];
      dst[x * 8 + 1] = src[x * 8 + 5];
      dst[x * 8 + 2] = src[x * 8 + 2];
      dst[x * 8 + 3] = src[x * 8 + 3];
      dst[x * 8 + 4] = src[x * 8];
      dst[x * 8 + 5] = src[x * 8 + 1];
      dst[x * 8 + 6] = src[x * 8 + 6];
      dst[x * 8 + 7] = src[x * 8 + 7];
    }
  }
}

}  // namespace neon

#endif  // FPNGE_INTERNAL_KERNELS_NEON_H_
