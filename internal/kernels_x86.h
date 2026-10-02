// Copyright 2026
#ifndef FPNGE_INTERNAL_KERNELS_X86_H_
#define FPNGE_INTERNAL_KERNELS_X86_H_

// This header is included from fpnge.cc after the shared HuffmanTable and
// BitWriter types are defined. Keep x86 intrinsic mechanics in this namespace;
// shared PNG control flow should call these codec-level kernels instead.
namespace x86 {

static uint32_t hadd(MIVEC v) {
  auto sum =
#ifdef __AVX2__
      _mm_add_epi32(_mm256_castsi256_si128(v), _mm256_extracti128_si256(v, 1));
#else
      v;
#endif
  sum = _mm_hadd_epi32(sum, sum);
  sum = _mm_hadd_epi32(sum, sum);
  return _mm_cvtsi128_si32(sum);
}

template <size_t predictor>
static FORCE_INLINE MIVEC PredictVec(const unsigned char *current_buf,
                                     const unsigned char *top_buf,
                                     const unsigned char *left_buf,
                                     const unsigned char *topleft_buf) {
  auto data = MMSI(load)((MIVEC *)(current_buf));
  if (predictor == 0) {
    return data;
  } else if (predictor == 1) {
    auto pred = MMSI(loadu)((MIVEC *)(left_buf));
    return MM(sub_epi8)(data, pred);
  } else if (predictor == 2) {
    auto pred = MMSI(load)((MIVEC *)(top_buf));
    return MM(sub_epi8)(data, pred);
  } else if (predictor == 3) {
    auto left = MMSI(loadu)((MIVEC *)(left_buf));
    auto top = MMSI(load)((MIVEC *)(top_buf));
    auto pred = MM(sub_epi8)(MM(add_epi8)(top, left), MM(avg_epu8)(top, left));
    return MM(sub_epi8)(data, pred);
  } else {
    auto left = MMSI(loadu)((MIVEC *)(left_buf));
    auto top = MMSI(load)((MIVEC *)(top_buf));
    auto c = MMSI(loadu)((MIVEC *)(topleft_buf));

    auto a = MM(min_epu8)(left, top);
    auto b = MM(max_epu8)(left, top);

    auto pa = MM(subs_epu8)(b, c);
    auto pb = MM(subs_epu8)(c, a);

    auto min_pab = MM(min_epu8)(pa, pb);
    auto pc = MM(sub_epi8)(MM(max_epu8)(pa, pb), min_pab);

    auto min_pabc = MM(min_epu8)(min_pab, pc);
    auto use_a = MM(cmpeq_epi8)(min_pabc, pa);
    auto use_b = MM(cmpeq_epi8)(min_pabc, pb);

    auto pred = MM(blendv_epi8)(MM(blendv_epi8)(c, b, use_b), a, use_a);
    return MM(sub_epi8)(data, pred);
  }
}

alignas(SIMD_WIDTH) constexpr int32_t _kMaskVec[] = {0,  0,  0,  0,
#if SIMD_WIDTH == 32
                                                     0,  0,  0,  0,
                                                     -1, -1, -1, -1,
#endif
                                                     -1, -1, -1, -1};
static const uint8_t *kMaskVec =
    reinterpret_cast<const uint8_t *>(_kMaskVec) + SIMD_WIDTH;

template <size_t predictor, typename CB, typename CB_ADL, typename CB_RLE>
static void
ProcessRow(size_t bytes_per_line, const unsigned char *current_row_buf,
           const unsigned char *top_buf, const unsigned char *left_buf,
           const unsigned char *topleft_buf, CB &&cb, CB_ADL &&cb_adl,
           CB_RLE &&cb_rle) {
  size_t run = 0;
  size_t i = 0;
  for (; i + SIMD_WIDTH <= bytes_per_line; i += SIMD_WIDTH) {
    auto pdata = PredictVec<predictor>(current_row_buf + i, top_buf + i,
                                       left_buf + i, topleft_buf + i);
    unsigned pdatais0 =
        MM(movemask_epi8)(MM(cmpeq_epi8)(pdata, MMSI(setzero)()));
    if (pdatais0 == SIMD_MASK) {
      run += SIMD_WIDTH;
    } else {
      if (run != 0) {
        cb_rle(run);
      }
      run = 0;
      cb(pdata, SIMD_WIDTH);
    }
    cb_adl(pdata, SIMD_WIDTH, i);
  }
  size_t bytes_remaining =
      bytes_per_line ^ i; // equivalent to `bytes_per_line - i`
  if (bytes_remaining) {
    auto pdata = PredictVec<predictor>(current_row_buf + i, top_buf + i,
                                       left_buf + i, topleft_buf + i);
    unsigned pdatais0 =
        MM(movemask_epi8)(MM(cmpeq_epi8)(pdata, MMSI(setzero)()));
    auto mask = (1UL << bytes_remaining) - 1;

    if ((pdatais0 & mask) == mask && run + bytes_remaining >= 16) {
      run += bytes_remaining;
    } else {
      if (run != 0) {
        cb_rle(run);
      }
      run = 0;
      cb(pdata, bytes_remaining);
    }
    cb_adl(pdata, bytes_remaining, i);
  }
  if (run != 0) {
    cb_rle(run);
  }
}

template <typename CB, typename CB_ADL, typename CB_RLE>
static void
ProcessRow(uint8_t predictor, size_t bytes_per_line,
           const unsigned char *current_row_buf, const unsigned char *top_buf,
           const unsigned char *left_buf, const unsigned char *topleft_buf,
           CB &&cb, CB_ADL &&cb_adl, CB_RLE &&cb_rle) {
  if (predictor == 1) {
    ProcessRow<1>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else if (predictor == 2) {
    ProcessRow<2>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else if (predictor == 3) {
    ProcessRow<3>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else if (predictor == 4) {
    ProcessRow<4>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  } else {
    assert(predictor == 0);
    ProcessRow<0>(bytes_per_line, current_row_buf, top_buf, left_buf,
                  topleft_buf, cb, cb_adl, cb_rle);
  }
}

template <typename CB> static void ForAllRLESymbols(size_t length, CB &&cb) {
  assert(length >= 4);
  length -= 1;

  if (length < 258) {
    // fast path if long sequences are rare in the image
    cb(length, 1);
  } else {
    auto runs = length / 258;
    auto remain = length % 258;
    if (remain == 1 || remain == 2) {
      remain += 258 - 3;
      runs--;
      cb(3, 1);
    }
    if (runs) {
      cb(258, runs);
    }
    if (remain) {
      cb(remain, 1);
    }
  }
}

template <size_t pred, bool store_pred>
static void
TryPredictor(size_t bytes_per_line, const unsigned char *current_row_buf,
             const unsigned char *top_buf, const unsigned char *left_buf,
             const unsigned char *topleft_buf, unsigned char *predicted_data,
             const HuffmanTable &table, size_t &best_cost, uint8_t &predictor) {
  size_t cost_rle = 0;
  MIVEC cost_direct = MMSI(setzero)();
  auto cost_chunk_cb = [&](const MIVEC bytes,
                           const size_t bytes_in_vec) FORCE_INLINE_LAMBDA {
    auto data_for_lut = MMSI(and)(MM(set1_epi8)(0xF), bytes);
    // get a mask of `bytes` that are between -16 and 15 inclusive
    // (`-16 <= bytes <= 15` is equivalent to `bytes + 112 > 95`)
    auto use_lowhi = MM(cmpgt_epi8)(MM(add_epi8)(bytes, MM(set1_epi8)(112)),
                                    MM(set1_epi8)(95));

    auto nbits_low16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.first16_nbits)), data_for_lut);
    auto nbits_hi16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.last16_nbits)), data_for_lut);

    auto nbits = MM(blendv_epi8)(nbits_low16, nbits_hi16, bytes);
    nbits = MM(blendv_epi8)(MM(set1_epi8)(table.mid_nbits), nbits, use_lowhi);

    auto nbits_discard =
        MMSI(and)(nbits, MMSI(loadu)((MIVEC *)(kMaskVec - bytes_in_vec)));

    cost_direct =
        MM(add_epi32)(cost_direct, MM(sad_epu8)(nbits, nbits_discard));
  };
  auto rle_cost_cb = [&](size_t run) {
    cost_rle += table.first16_nbits[0];
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      cost_rle += (table.dist_nbits + table.lz77_length_nbits[len]) * count;
    });
  };
  if (store_pred) {
    ProcessRow<pred>(
        bytes_per_line, current_row_buf, top_buf, left_buf, topleft_buf,
        cost_chunk_cb,
        [=](const MIVEC pdata, size_t, size_t i) {
          MMSI(store)((MIVEC *)(predicted_data + i), pdata);
        },
        rle_cost_cb);
  } else {
    ProcessRow<pred>(
        bytes_per_line, current_row_buf, top_buf, left_buf, topleft_buf,
        cost_chunk_cb, [](const MIVEC, size_t, size_t) {}, rle_cost_cb);
  }
  size_t cost = cost_rle + hadd(cost_direct);
  if (cost < best_cost) {
    best_cost = cost;
    predictor = pred;
  }
}

static FORCE_INLINE void WriteBitsLong(MIVEC nbits, MIVEC bits_lo,
                                       MIVEC bits_hi, size_t mid_lo_nbits,
                                       BitWriter *__restrict writer) {

  // Merge bits_lo and bits_hi in 16-bit "bits".
#if FPNGE_USE_PEXT
  auto bits0 = MM(unpacklo_epi8)(bits_lo, bits_hi);
  auto bits1 = MM(unpackhi_epi8)(bits_lo, bits_hi);

  // convert nbits into a mask
  auto nbits_hi = MM(sub_epi8)(nbits, MM(set1_epi8)(mid_lo_nbits));
  auto nbits0 = MM(unpacklo_epi8)(nbits, nbits_hi);
  auto nbits1 = MM(unpackhi_epi8)(nbits, nbits_hi);
  const auto nbits_to_mask =
      BCAST128(_mm_set_epi32(0xffffffff, 0xffffffff, 0x7f3f1f0f, 0x07030100));
  auto bitmask0 = MM(shuffle_epi8)(nbits_to_mask, nbits0);
  auto bitmask1 = MM(shuffle_epi8)(nbits_to_mask, nbits1);

  // aggregate nbits
  alignas(16) uint16_t nbits_a[SIMD_WIDTH / 4];
  auto bit_count = MM(maddubs_epi16)(nbits, MM(set1_epi8)(1));
#ifdef __AVX2__
  auto bit_count2 = _mm_hadd_epi16(_mm256_castsi256_si128(bit_count),
                                   _mm256_extracti128_si256(bit_count, 1));
  _mm_store_si128((__m128i *)nbits_a, bit_count2);
#else
  bit_count = _mm_hadd_epi16(bit_count, bit_count);
  _mm_storel_epi64((__m128i *)nbits_a, bit_count);
#endif

  alignas(SIMD_WIDTH) uint64_t bitmask_a[SIMD_WIDTH / 4];
  MMSI(store)((MIVEC *)bitmask_a, bitmask0);
  MMSI(store)((MIVEC *)bitmask_a + 1, bitmask1);

#else

  auto nbits0 = MM(unpacklo_epi8)(nbits, MMSI(setzero)());
  auto nbits1 = MM(unpackhi_epi8)(nbits, MMSI(setzero)());
  MIVEC bits0, bits1;
  if (mid_lo_nbits == 8) {
    bits0 = MM(unpacklo_epi8)(bits_lo, bits_hi);
    bits1 = MM(unpackhi_epi8)(bits_lo, bits_hi);
  } else {
    auto nbits_shift = _mm_cvtsi32_si128(8 - mid_lo_nbits);
    auto bits_lo_shifted = MM(sll_epi16)(bits_lo, nbits_shift);
    bits0 = MM(unpacklo_epi8)(bits_lo_shifted, bits_hi);
    bits1 = MM(unpackhi_epi8)(bits_lo_shifted, bits_hi);

    bits0 = MM(srl_epi16)(bits0, nbits_shift);
    bits1 = MM(srl_epi16)(bits1, nbits_shift);
  }

  // 16 -> 32
  auto nbits0_32_lo = MMSI(and)(nbits0, MM(set1_epi32)(0xFFFF));
  auto nbits1_32_lo = MMSI(and)(nbits1, MM(set1_epi32)(0xFFFF));

  auto bits0_32_lo = MMSI(and)(bits0, MM(set1_epi32)(0xFFFF));
  auto bits1_32_lo = MMSI(and)(bits1, MM(set1_epi32)(0xFFFF));
#ifdef __AVX2__
  auto bits0_32_hi = MM(sllv_epi32)(MM(srli_epi32)(bits0, 16), nbits0_32_lo);
  auto bits1_32_hi = MM(sllv_epi32)(MM(srli_epi32)(bits1, 16), nbits1_32_lo);
#else
  // emulate variable shift by abusing float exponents
  // this works because Huffman symbols are not allowed to exceed 15 bits, so
  // will fit within a float's mantissa and (number << 15) won't overflow when
  // converted back to a signed int
  auto bits0_32_hi =
      _mm_castps_si128(MM(cvtepi32_ps)(MM(srli_epi32)(bits0, 16)));
  auto bits1_32_hi =
      _mm_castps_si128(MM(cvtepi32_ps)(MM(srli_epi32)(bits1, 16)));

  // add shift amount to the exponent
  bits0_32_hi = MM(add_epi32)(bits0_32_hi, MM(slli_epi32)(nbits0_32_lo, 23));
  bits1_32_hi = MM(add_epi32)(bits1_32_hi, MM(slli_epi32)(nbits1_32_lo, 23));

  bits0_32_hi = MM(cvtps_epi32)(_mm_castsi128_ps(bits0_32_hi));
  bits1_32_hi = MM(cvtps_epi32)(_mm_castsi128_ps(bits1_32_hi));
#endif

  nbits0 = MM(madd_epi16)(nbits0, MM(set1_epi16)(1));
  nbits1 = MM(madd_epi16)(nbits1, MM(set1_epi16)(1));
  auto bits0_32 = MMSI(or)(bits0_32_lo, bits0_32_hi);
  auto bits1_32 = MMSI(or)(bits1_32_lo, bits1_32_hi);

  // 32 -> 64
#ifdef __AVX2__
  auto nbits_inv0_64_lo = MM(subs_epu8)(MM(set1_epi64x)(32), nbits0);
  auto nbits_inv1_64_lo = MM(subs_epu8)(MM(set1_epi64x)(32), nbits1);
  bits0 = MM(sllv_epi32)(bits0_32, nbits_inv0_64_lo);
  bits1 = MM(sllv_epi32)(bits1_32, nbits_inv1_64_lo);
  bits0 = MM(srlv_epi64)(bits0, nbits_inv0_64_lo);
  bits1 = MM(srlv_epi64)(bits1, nbits_inv1_64_lo);
#else
  auto nbits0_64_lo = MMSI(and)(nbits0, MM(set1_epi64x)(0xFFFFFFFF));
  auto nbits1_64_lo = MMSI(and)(nbits1, MM(set1_epi64x)(0xFFFFFFFF));
  // just do two shifts for SSE variant
  auto bits0_64_lo = MMSI(and)(bits0_32, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits1_64_lo = MMSI(and)(bits1_32, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits0_64_hi = MM(srli_epi64)(bits0_32, 32);
  auto bits1_64_hi = MM(srli_epi64)(bits1_32, 32);

  bits0_64_hi = _mm_blend_epi16(
      _mm_sll_epi64(bits0_64_hi, nbits0_64_lo),
      _mm_sll_epi64(bits0_64_hi,
                    _mm_unpackhi_epi64(nbits0_64_lo, nbits0_64_lo)),
      0xf0);
  bits1_64_hi = _mm_blend_epi16(
      _mm_sll_epi64(bits1_64_hi, nbits1_64_lo),
      _mm_sll_epi64(bits1_64_hi,
                    _mm_unpackhi_epi64(nbits1_64_lo, nbits1_64_lo)),
      0xf0);

  bits0 = MMSI(or)(bits0_64_lo, bits0_64_hi);
  bits1 = MMSI(or)(bits1_64_lo, bits1_64_hi);
#endif

  auto nbits01 = MM(hadd_epi32)(nbits0, nbits1);

  // nbits_a <= 40 as we have at most 10 bits per symbol, so the call to the
  // writer is safe.
  alignas(SIMD_WIDTH) uint32_t nbits_a[SIMD_WIDTH / 4];
  MMSI(store)((MIVEC *)nbits_a, nbits01);

#endif

  alignas(SIMD_WIDTH) uint64_t bits_a[SIMD_WIDTH / 4];
  MMSI(store)((MIVEC *)bits_a, bits0);
  MMSI(store)((MIVEC *)bits_a + 1, bits1);

#ifdef __AVX2__
  constexpr uint8_t kPerm[] = {0, 1, 4, 5, 2, 3, 6, 7};
#else
  constexpr uint8_t kPerm[] = {0, 1, 2, 3};
#endif

  for (size_t ii = 0; ii < SIMD_WIDTH / 4; ii++) {
    uint64_t bits = bits_a[kPerm[ii]];
#if FPNGE_USE_PEXT
    bits = _pext_u64(bits, bitmask_a[kPerm[ii]]);
#endif
    auto count = nbits_a[ii];
    writer->Write(count, bits);
  }
}

// as above, but where nbits <= 8, so we can ignore bits_hi
static FORCE_INLINE void WriteBitsShort(MIVEC nbits, MIVEC bits,
                                        BitWriter *__restrict writer) {

#if FPNGE_USE_PEXT
  // convert nbits into a mask
  auto bitmask = MM(shuffle_epi8)(
      BCAST128(_mm_set_epi32(0xffffffff, 0xffffffff, 0x7f3f1f0f, 0x07030100)),
      nbits);
  auto bit_count = MM(sad_epu8)(nbits, MMSI(setzero)());

  alignas(SIMD_WIDTH) uint64_t nbits_a[SIMD_WIDTH / 8];
  MMSI(store)((MIVEC *)nbits_a, bit_count);
  alignas(SIMD_WIDTH) uint64_t bits_a[SIMD_WIDTH / 8];
  MMSI(store)((MIVEC *)bits_a, bits);
  alignas(SIMD_WIDTH) uint64_t bitmask_a[SIMD_WIDTH / 8];
  MMSI(store)((MIVEC *)bitmask_a, bitmask);
#else
  // 8 -> 16
  auto prod = MM(slli_epi16)(
      MM(shuffle_epi8)(BCAST128(_mm_set_epi32(
                           //  since we can't handle 8 bits, we'll under-shift
                           //  it and do an extra shift later on
                           -1, 0xffffff80, 0x40201008, 0x040201ff)),
                       nbits),
      8);
  auto bits_hi =
      MM(mulhi_epu16)(MMSI(andnot)(MM(set1_epi16)(0xff), bits), prod);
  bits_hi = MM(add_epi16)(bits_hi, bits_hi); // fix under-shifting
  bits = MMSI(or)(MMSI(and)(bits, MM(set1_epi16)(0xff)), bits_hi);
  nbits = MM(maddubs_epi16)(nbits, MM(set1_epi8)(1));

  // 16 -> 32
  auto nbits_32_lo = MMSI(and)(nbits, MM(set1_epi32)(0xFFFF));
  auto bits_32_lo = MMSI(and)(bits, MM(set1_epi32)(0xFFFF));
  auto bits_32_hi = MM(srli_epi32)(bits, 16);
#ifdef __AVX2__
  bits_32_hi = MM(sllv_epi32)(bits_32_hi, nbits_32_lo);
#else
  // need to avoid overflow when converting float -> int, because it converts to
  // a signed int; do this by offsetting the shift by 1
  nbits_32_lo = MM(add_epi16)(nbits_32_lo, MM(set1_epi32)(0xFFFF));
  bits_32_hi = _mm_castps_si128(MM(cvtepi32_ps)(bits_32_hi));
  bits_32_hi = MM(add_epi32)(bits_32_hi, MM(slli_epi32)(nbits_32_lo, 23));
  bits_32_hi = MM(cvtps_epi32)(_mm_castsi128_ps(bits_32_hi));
  bits_32_hi = MM(add_epi32)(bits_32_hi, bits_32_hi); // fix under-shifting
#endif
  nbits = MM(madd_epi16)(nbits, MM(set1_epi16)(1));
  bits = MMSI(or)(bits_32_lo, bits_32_hi);

  // 32 -> 64
#ifdef __AVX2__
  auto nbits_inv_64_lo = MM(subs_epu8)(MM(set1_epi64x)(32), nbits);
  bits = MM(sllv_epi32)(bits, nbits_inv_64_lo);
  bits = MM(srlv_epi64)(bits, nbits_inv_64_lo);
#else
  auto nbits_64_lo = MMSI(and)(nbits, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits_64_lo = MMSI(and)(bits, MM(set1_epi64x)(0xFFFFFFFF));
  auto bits_64_hi = MM(srli_epi64)(bits, 32);
  bits_64_hi = _mm_blend_epi16(
      _mm_sll_epi64(bits_64_hi, nbits_64_lo),
      _mm_sll_epi64(bits_64_hi, _mm_unpackhi_epi64(nbits_64_lo, nbits_64_lo)),
      0xf0);
  bits = MMSI(or)(bits_64_lo, bits_64_hi);
#endif

  auto nbits2 = _mm_hadd_epi32(
#ifdef __AVX2__
      _mm256_castsi256_si128(nbits), _mm256_extracti128_si256(nbits, 1)
#else
      nbits, nbits
#endif
  );

  alignas(16) uint32_t nbits_a[4];
  alignas(SIMD_WIDTH) uint64_t bits_a[SIMD_WIDTH / 8];

  _mm_store_si128((__m128i *)nbits_a, nbits2);
  MMSI(store)((MIVEC *)bits_a, bits);

#endif

  for (size_t ii = 0; ii < SIMD_WIDTH / 8; ii++) {
    uint64_t bits64 = bits_a[ii];
#if FPNGE_USE_PEXT
    bits64 = _pext_u64(bits64, bitmask_a[ii]);
#endif
    if (nbits_a[ii] + writer->bits_in_buffer > 63) {
      // hope this case rarely occurs
      writer->Write(16, bits64 & 0xffff);
      bits64 >>= 16;
      nbits_a[ii] -= 16;
    }
    writer->Write(nbits_a[ii], bits64);
  }
}

static FORCE_INLINE void AddApproxCost(MIVEC &total, MIVEC pdata,
                                       MIVEC bit_costs) {
  auto approx_sym = MM(min_epu8)(MM(abs_epi8)(pdata), MM(set1_epi8)(15));
  auto cost = MM(shuffle_epi8)(bit_costs, approx_sym);
  total = MM(add_epi64)(total, MM(sad_epu8)(cost, MMSI(setzero)()));
}
static FORCE_INLINE void AddApproxCost(MIVEC &total, MIVEC pdata,
                                       MIVEC bit_costs, MIVEC maskv) {
  auto approx_sym = MM(min_epu8)(MM(abs_epi8)(pdata), MM(set1_epi8)(15));
  auto cost = MM(shuffle_epi8)(bit_costs, approx_sym);
  auto cost_mask = MMSI(and)(maskv, cost);
  total = MM(add_epi64)(total, MM(sad_epu8)(cost, cost_mask));
}

static uint8_t
SelectPredictor(size_t bytes_per_line, const unsigned char *current_row_buf,
                const unsigned char *top_buf, const unsigned char *left_buf,
                const unsigned char *topleft_buf, unsigned char *paeth_data,
                const HuffmanTable &table, const struct FPNGEOptions *options) {
  if (options->predictor <= 4) {
    return options->predictor;
  }
  if (options->predictor == FPNGE_PREDICTOR_APPROX) {
    auto bit_costs = BCAST128(_mm_load_si128((__m128i *)(table.approx_nbits)));
    size_t i = 0;
    auto cost1 = MMSI(setzero)();
    auto cost2 = MMSI(setzero)();
    auto cost3 = MMSI(setzero)();
    auto cost4 = MMSI(setzero)();
    MIVEC pdata;

    for (; i + SIMD_WIDTH <= bytes_per_line; i += SIMD_WIDTH) {
      pdata = PredictVec<1>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost1, pdata, bit_costs);

      pdata = PredictVec<2>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost2, pdata, bit_costs);

      pdata = PredictVec<3>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost3, pdata, bit_costs);

      pdata = PredictVec<4>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost4, pdata, bit_costs);
      MMSI(store)((MIVEC *)(paeth_data + i), pdata);
    }

    size_t bytes_remaining =
        bytes_per_line ^ i; // equivalent to `bytes_per_line - i`
    if (bytes_remaining) {
      auto maskv = MMSI(loadu)((MIVEC *)(kMaskVec - bytes_remaining));

      pdata = PredictVec<1>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost1, pdata, bit_costs, maskv);

      pdata = PredictVec<2>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost2, pdata, bit_costs, maskv);

      pdata = PredictVec<3>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost3, pdata, bit_costs, maskv);

      pdata = PredictVec<4>(current_row_buf + i, top_buf + i, left_buf + i,
                            topleft_buf + i);
      AddApproxCost(cost4, pdata, bit_costs, maskv);
      MMSI(store)((MIVEC *)(paeth_data + i), pdata);
    }

    uint8_t predictor = 1;
    size_t best_cost = hadd(cost1);
    auto test_cost = [&](MIVEC costv, uint8_t pred) {
      size_t cost = hadd(costv);
      if (cost < best_cost) {
        best_cost = cost;
        predictor = pred;
      }
    };
    test_cost(cost2, 2);
    test_cost(cost3, 3);
    test_cost(cost4, 4);
    return predictor;
  }

  assert(options->predictor == FPNGE_PREDICTOR_BEST);
  uint8_t predictor;
  size_t best_cost = ~0U;
  TryPredictor<1, /*store_pred=*/false>(bytes_per_line, current_row_buf,
                                        top_buf, left_buf, topleft_buf, nullptr,
                                        table, best_cost, predictor);
  TryPredictor<2, /*store_pred=*/false>(bytes_per_line, current_row_buf,
                                        top_buf, left_buf, topleft_buf, nullptr,
                                        table, best_cost, predictor);
  TryPredictor<3, /*store_pred=*/false>(bytes_per_line, current_row_buf,
                                        top_buf, left_buf, topleft_buf, nullptr,
                                        table, best_cost, predictor);
  TryPredictor<4, /*store_pred=*/true>(bytes_per_line, current_row_buf, top_buf,
                                       left_buf, topleft_buf, paeth_data, table,
                                       best_cost, predictor);
  return predictor;
}

static void
EncodeOneRow(size_t bytes_per_line, const unsigned char *current_row_buf,
             const unsigned char *top_buf, const unsigned char *left_buf,
             const unsigned char *topleft_buf, unsigned char *paeth_data,
             const HuffmanTable &table, uint32_t &s1, uint32_t &s2,
             BitWriter *__restrict writer, const struct FPNGEOptions *options) {
  uint8_t predictor =
      SelectPredictor(bytes_per_line, current_row_buf, top_buf, left_buf,
                      topleft_buf, paeth_data, table, options);

  writer->Write(table.first16_nbits[predictor], table.first16_bits[predictor]);
  UpdateAdler32(s1, s2, predictor);

  auto adler_accum_s1 = INT2VEC(s1);
  auto adler_accum_s2 = INT2VEC(s2);
  auto adler_s1_sum = MMSI(setzero)();

  uint16_t bytes_since_flush = 1;

  auto flush_adler = [&]() {
    adler_accum_s2 = MM(add_epi32)(
        adler_accum_s2, MM(slli_epi32)(adler_s1_sum, SIMD_WIDTH == 32 ? 5 : 4));
    adler_s1_sum = MMSI(setzero)();

    uint32_t ls1 = hadd(adler_accum_s1);
    uint32_t ls2 = hadd(adler_accum_s2);
    ls1 %= kAdler32Mod;
    ls2 %= kAdler32Mod;
    s1 = ls1;
    s2 = ls2;
    adler_accum_s1 = INT2VEC(s1);
    adler_accum_s2 = INT2VEC(s2);
    bytes_since_flush = 0;
  };

  auto encode_chunk_cb = [&](const MIVEC bytes, const size_t bytes_in_vec) {
    auto maskv = MMSI(loadu)((MIVEC *)(kMaskVec - bytes_in_vec));

    auto data_for_lut = MMSI(and)(MM(set1_epi8)(0xF), bytes);
    data_for_lut = MMSI(or)(data_for_lut, maskv);
    // get a mask of `bytes` that are between -16 and 15 inclusive
    // (`-16 <= bytes <= 15` is equivalent to `bytes + 112 > 95`)
    auto use_lowhi = MM(cmpgt_epi8)(MM(add_epi8)(bytes, MM(set1_epi8)(112)),
                                    MM(set1_epi8)(95));

    auto nbits_low16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.first16_nbits)), data_for_lut);
    auto nbits_hi16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.last16_nbits)), data_for_lut);
    auto nbits = MM(blendv_epi8)(nbits_low16, nbits_hi16, bytes);

    auto bits_low16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.first16_bits)), data_for_lut);
    auto bits_hi16 = MM(shuffle_epi8)(
        BCAST128(_mm_load_si128((__m128i *)table.last16_bits)), data_for_lut);
    auto bits_lo = MM(blendv_epi8)(bits_low16, bits_hi16, bytes);

    if (MM(movemask_epi8)(use_lowhi) ^ SIMD_MASK) {
      auto data_for_midlut =
          MMSI(and)(MM(set1_epi8)(0xF), MM(srai_epi16)(bytes, 4));

      auto bits_mid_lo = MM(shuffle_epi8)(
          BCAST128(_mm_load_si128((__m128i *)table.mid_lowbits)),
          data_for_midlut);

      auto bits_hi = MM(shuffle_epi8)(
          BCAST128(_mm_load_si128((__m128i *)kBitReverseNibbleLookup)),
          data_for_lut);

      use_lowhi = MMSI(or)(use_lowhi, maskv);
      nbits = MM(blendv_epi8)(MM(set1_epi8)(table.mid_nbits), nbits, use_lowhi);
      bits_lo = MM(blendv_epi8)(bits_mid_lo, bits_lo, use_lowhi);

#if !FPNGE_USE_PEXT
      bits_hi = MMSI(andnot)(use_lowhi, bits_hi);
#endif

      WriteBitsLong(nbits, bits_lo, bits_hi, table.mid_nbits - 4, writer);
    } else {
      // since mid (symbols 16-239) is not present, we can take some shortcuts
      // this is expected to occur frequently if compression is effective
      WriteBitsShort(nbits, bits_lo, writer);
    }
  };

  auto adler_chunk_cb = [&](const MIVEC pdata, size_t bytes_in_vec, size_t) {
    bytes_since_flush += bytes_in_vec;
    auto bytes = pdata;

    auto muls = MM(set_epi8)(
        1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
#if SIMD_WIDTH == 32
        ,
        17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32
#endif
    );

    if (bytes_in_vec < SIMD_WIDTH) {
      adler_accum_s2 = MM(add_epi32)(
          MM(mul_epu32)(MM(set1_epi32)(bytes_in_vec), adler_accum_s1),
          adler_accum_s2);
      bytes =
          MMSI(andnot)(MMSI(loadu)((MIVEC *)(kMaskVec - bytes_in_vec)), bytes);
      muls = MM(add_epi8)(muls, MM(set1_epi8)(bytes_in_vec - SIMD_WIDTH));
    } else {
      adler_s1_sum = MM(add_epi32)(adler_s1_sum, adler_accum_s1);
    }

    adler_accum_s1 =
        MM(add_epi32)(adler_accum_s1, MM(sad_epu8)(bytes, MMSI(setzero)()));

    auto bytesmuls = MM(maddubs_epi16)(bytes, muls);
    adler_accum_s2 = MM(add_epi32)(
        adler_accum_s2, MM(madd_epi16)(bytesmuls, MM(set1_epi16)(1)));

    if (bytes_since_flush >= 5500) {
      flush_adler();
    }
  };

  auto encode_rle_cb = [&](size_t run) {
    writer->Write(table.first16_nbits[0], table.first16_bits[0]);
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      uint32_t bits = (table.dist_bits << table.lz77_length_nbits[len]) |
                      table.lz77_length_bits[len];
      auto nbits = table.lz77_length_nbits[len] + table.dist_nbits;
      while (count--) {
        writer->Write(nbits, bits);
      }
    });
  };

  if (options->predictor > 4 && predictor == 4) {
    // re-use Paeth data
    ProcessRow<0>(bytes_per_line, paeth_data, nullptr, nullptr, nullptr,
                  encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  } else {
    ProcessRow(predictor, bytes_per_line, current_row_buf, top_buf, left_buf,
               topleft_buf, encode_chunk_cb, adler_chunk_cb, encode_rle_cb);
  }

  flush_adler();
}

static void
CollectSymbolCounts(size_t bytes_per_line, const unsigned char *current_row_buf,
                    const unsigned char *top_buf, const unsigned char *left_buf,
                    const unsigned char *topleft_buf, unsigned char *paeth_data,
                    uint64_t *__restrict symbol_counts,
                    const struct FPNGEOptions *options) {

  auto encode_chunk_cb = [&](const MIVEC pdata, const size_t bytes_in_vec) {
    alignas(SIMD_WIDTH) uint8_t predicted_data[SIMD_WIDTH];
    MMSI(store)((MIVEC *)predicted_data, pdata);
    for (size_t i = 0; i < bytes_in_vec; i++) {
      symbol_counts[predicted_data[i]] += 1;
    }
  };

  auto adler_chunk_cb = [&](const MIVEC, size_t, size_t) {};

  auto encode_rle_cb = [&](size_t run) {
    symbol_counts[0] += 1;
    constexpr size_t kLZ77Sym[] = {
        0,   0,   0,   257, 258, 259, 260, 261, 262, 263, 264, 265, 265, 266,
        266, 267, 267, 268, 268, 269, 269, 269, 269, 270, 270, 270, 270, 271,
        271, 271, 271, 272, 272, 272, 272, 273, 273, 273, 273, 273, 273, 273,
        273, 274, 274, 274, 274, 274, 274, 274, 274, 275, 275, 275, 275, 275,
        275, 275, 275, 276, 276, 276, 276, 276, 276, 276, 276, 277, 277, 277,
        277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 277, 278,
        278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278, 278,
        278, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279, 279,
        279, 279, 279, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280, 280,
        280, 280, 280, 280, 280, 281, 281, 281, 281, 281, 281, 281, 281, 281,
        281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281, 281,
        281, 281, 281, 281, 281, 281, 281, 281, 281, 282, 282, 282, 282, 282,
        282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282,
        282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 282, 283,
        283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283,
        283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283, 283,
        283, 283, 283, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284,
        284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284, 284,
        284, 284, 284, 284, 284, 284, 285,
    };
    ForAllRLESymbols(run, [&](size_t len, size_t count) {
      symbol_counts[kLZ77Sym[len]] += count;
    });
  };

  if (options->predictor == FPNGE_PREDICTOR_APPROX) {
    // filter selection here seems to be slightly more effective when using the
    // approximate selector; more investigation is probably warranted
    HuffmanTable dummy_table;
    uint8_t predictor =
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
    uint8_t predictor = options->predictor > 4 ? 4 : options->predictor;
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
    for (; x + SIMD_WIDTH / 4 <= width; x += SIMD_WIDTH / 4) {
      auto vec = MMSI(loadu)((MIVEC *)(src + x * 4));
      auto shuf = MM(shuffle_epi8)(
          vec, BCAST128(_mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14,
                                      13, 12, 15)));
      MMSI(storeu)((MIVEC *)(dst + x * 4), shuf);
    }
  } else if (nb_channels == 4 && bytes_per_channel == 2) {
    for (; x + SIMD_WIDTH / 8 <= width; x += SIMD_WIDTH / 8) {
      auto vec = MMSI(loadu)((MIVEC *)(src + x * 8));
      auto shuf = MM(shuffle_epi8)(
          vec, BCAST128(_mm_setr_epi8(4, 5, 2, 3, 0, 1, 6, 7, 12, 13, 10, 11, 8,
                                      9, 14, 15)));
      MMSI(storeu)((MIVEC *)(dst + x * 8), shuf);
    }
  } else if (nb_channels == 3 && bytes_per_channel == 1) {
    for (; x + 16 <= width; x += 16) {
      auto vec1 = _mm_loadu_si128((__m128i *)(src + x * 3));
      auto vec2 = _mm_loadu_si128((__m128i *)(src + x * 3 + 16));
      auto vec3 = _mm_loadu_si128((__m128i *)(src + x * 3 + 32));
      auto s1a =
          _mm_setr_epi8(2, 1, 0, 5, 4, 3, 8, 7, 6, 11, 10, 9, 14, 13, 12, -1);
      auto s1b = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, 1);
      auto shuf1 = _mm_or_si128(_mm_shuffle_epi8(vec1, s1a),
                                _mm_shuffle_epi8(vec2, s1b));
      auto s2a = _mm_setr_epi8(-1, 15, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s2b =
          _mm_setr_epi8(0, -1, 4, 3, 2, 7, 6, 5, 10, 9, 8, 13, 12, 11, -1, 15);
      auto s2c = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, 0, -1);
      auto shuf2 = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(vec1, s2a),
                                             _mm_shuffle_epi8(vec2, s2b)),
                                _mm_shuffle_epi8(vec3, s2c));
      auto s3b = _mm_setr_epi8(14, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s3c =
          _mm_setr_epi8(-1, 3, 2, 1, 6, 5, 4, 9, 8, 7, 12, 11, 10, 15, 14, 13);
      auto shuf3 = _mm_or_si128(_mm_shuffle_epi8(vec2, s3b),
                                _mm_shuffle_epi8(vec3, s3c));

      _mm_storeu_si128((__m128i *)(dst + x * 3), shuf1);
      _mm_storeu_si128((__m128i *)(dst + x * 3 + 16), shuf2);
      _mm_storeu_si128((__m128i *)(dst + x * 3 + 32), shuf3);
    }
  } else if (nb_channels == 3 && bytes_per_channel == 2) {
    for (; x + 8 <= width; x += 8) {
      auto vec1 = _mm_loadu_si128((__m128i *)(src + x * 6));
      auto vec2 = _mm_loadu_si128((__m128i *)(src + x * 6 + 16));
      auto vec3 = _mm_loadu_si128((__m128i *)(src + x * 6 + 32));
      auto s1a =
          _mm_setr_epi8(4, 5, 2, 3, 0, 1, 10, 11, 8, 9, 6, 7, -1, -1, 14, 15);
      auto s1b = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               0, 1, -1, -1);
      auto shuf1 = _mm_or_si128(_mm_shuffle_epi8(vec1, s1a),
                                _mm_shuffle_epi8(vec2, s1b));
      auto s2a = _mm_setr_epi8(12, 13, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s2b =
          _mm_setr_epi8(-1, -1, 6, 7, 4, 5, 2, 3, 12, 13, 10, 11, 8, 9, -1, -1);
      auto s2c = _mm_setr_epi8(-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, 2, 3);
      auto shuf2 = _mm_or_si128(_mm_or_si128(_mm_shuffle_epi8(vec1, s2a),
                                             _mm_shuffle_epi8(vec2, s2b)),
                                _mm_shuffle_epi8(vec3, s2c));
      auto s3b = _mm_setr_epi8(-1, -1, 14, 15, -1, -1, -1, -1, -1, -1, -1, -1,
                               -1, -1, -1, -1);
      auto s3c =
          _mm_setr_epi8(0, 1, -1, -1, 8, 9, 6, 7, 4, 5, 14, 15, 12, 13, 10, 11);
      auto shuf3 = _mm_or_si128(_mm_shuffle_epi8(vec2, s3b),
                                _mm_shuffle_epi8(vec3, s3c));

      _mm_storeu_si128((__m128i *)(dst + x * 6), shuf1);
      _mm_storeu_si128((__m128i *)(dst + x * 6 + 16), shuf2);
      _mm_storeu_si128((__m128i *)(dst + x * 6 + 32), shuf3);
    }
  }
  for (; x < width; x++) {
    if (nb_channels == 3 && bytes_per_channel == 1) {
      dst[x * 3] = src[x * 3 + 2];
      dst[x * 3 + 1] = src[x * 3 + 1];
      dst[x * 3 + 2] = src[x * 3];
    }
    if (nb_channels == 3 && bytes_per_channel == 2) {
      dst[x * 6] = src[x * 6 + 4];
      dst[x * 6 + 1] = src[x * 6 + 5];
      dst[x * 6 + 2] = src[x * 6 + 2];
      dst[x * 6 + 3] = src[x * 6 + 3];
      dst[x * 6 + 4] = src[x * 6];
      dst[x * 6 + 5] = src[x * 6 + 1];
    }
    if (nb_channels == 4 && bytes_per_channel == 1) {
      dst[x * 4] = src[x * 4 + 2];
      dst[x * 4 + 1] = src[x * 4 + 1];
      dst[x * 4 + 2] = src[x * 4];
      dst[x * 4 + 3] = src[x * 4 + 3];
    }
    if (nb_channels == 4 && bytes_per_channel == 2) {
      dst[x * 8] = src[x * 8 + 4];
      dst[x * 8 + 1] = src[x * 8 + 3];
      dst[x * 8 + 2] = src[x * 8 + 2];
      dst[x * 8 + 3] = src[x * 8 + 3];
      dst[x * 8 + 4] = src[x * 8];
      dst[x * 8 + 5] = src[x * 8 + 1];
      dst[x * 8 + 6] = src[x * 8 + 6];
      dst[x * 8 + 7] = src[x * 8 + 7];
    }
  }
}

}  // namespace x86

#endif  // FPNGE_INTERNAL_KERNELS_X86_H_
