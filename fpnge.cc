// Copyright 2021 Google LLC
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//    https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "fpnge.h"
#include "internal/arch.h"
#include <algorithm>
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <vector>

#if defined(_MSC_VER) && !defined(__clang__)
#define FORCE_INLINE_LAMBDA
#define FORCE_INLINE __forceinline
#define __SSE4_1__ 1
#define __PCLMUL__ 1
#ifdef __AVX2__
#define __BMI2__ 1
#endif
#else
#define FORCE_INLINE_LAMBDA __attribute__((always_inline))
#define FORCE_INLINE __attribute__((always_inline)) inline
#endif

#if !defined(FPNGE_USE_PEXT)
#if defined(__BMI2__) && FPNGE_ARCH_X86_64 &&                            \
    !defined(__tune_znver1__) && !defined(__tune_znver2__) &&                  \
    !defined(__tune_bdver4__)
#define FPNGE_USE_PEXT 1
#else
#define FPNGE_USE_PEXT 0
#endif
#endif

#include "internal/simd_x86.h"

#ifdef __PCLMUL__
#include "internal/crc_x86.h"
#else
#include "internal/crc_portable.h"
#endif

namespace {

#ifdef __PCLMUL__
using Crc32 = fpnge_internal::Crc32X86;
#else
using Crc32 = fpnge_internal::Crc32Portable;
#endif

alignas(16) constexpr uint8_t kBitReverseNibbleLookup[16] = {
    0b0000, 0b1000, 0b0100, 0b1100, 0b0010, 0b1010, 0b0110, 0b1110,
    0b0001, 0b1001, 0b0101, 0b1101, 0b0011, 0b1011, 0b0111, 0b1111,
};

static constexpr uint8_t kLZ77NBits[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1,
                                           1, 1, 2, 2, 2, 2, 3, 3, 3, 3,
                                           4, 4, 4, 4, 5, 5, 5, 5, 0};

static constexpr uint16_t kLZ77Base[29] = {
    3,  4,  5,  6,  7,  8,  9,  10, 11,  13,  15,  17,  19,  23, 27,
    31, 35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};

static uint16_t BitReverse(size_t nbits, uint16_t bits) {
  uint16_t rev16 = (kBitReverseNibbleLookup[bits & 0xF] << 12) |
                   (kBitReverseNibbleLookup[(bits >> 4) & 0xF] << 8) |
                   (kBitReverseNibbleLookup[(bits >> 8) & 0xF] << 4) |
                   (kBitReverseNibbleLookup[bits >> 12]);
  return rev16 >> (16 - nbits);
}

struct HuffmanTable {
  uint8_t nbits[286];
  uint16_t end_bits;

  alignas(16) uint8_t approx_nbits[16];

  alignas(16) uint8_t first16_nbits[16];
  alignas(16) uint8_t first16_bits[16];

  alignas(16) uint8_t last16_nbits[16];
  alignas(16) uint8_t last16_bits[16];

  alignas(16) uint8_t mid_lowbits[16];
  uint8_t mid_nbits;

  uint32_t lz77_length_nbits[259] = {};
  uint32_t lz77_length_bits[259] = {};
  uint32_t lz77_length_sym[259] = {};

  uint32_t dist_nbits, dist_bits;

  // Computes nbits[i] for i <= n, subject to min_limit[i] <= nbits[i] <=
  // max_limit[i], so to minimize sum(nbits[i] * freqs[i]).
  static void ComputeCodeLengths(const uint64_t *freqs, size_t n,
                                 uint8_t *min_limit, uint8_t *max_limit,
                                 uint8_t *nbits) {
    size_t precision = 0;
    uint64_t freqsum = 0;
    for (size_t i = 0; i < n; i++) {
      assert(freqs[i] != 0);
      freqsum += freqs[i];
      if (min_limit[i] < 1)
        min_limit[i] = 1;
      assert(min_limit[i] <= max_limit[i]);
      precision = std::max<size_t>(max_limit[i], precision);
    }
    uint64_t infty = freqsum * precision;
    std::vector<uint64_t> dynp(((1U << precision) + 1) * (n + 1), infty);
    auto d = [&](size_t sym, size_t off) -> uint64_t & {
      return dynp[sym * ((1 << precision) + 1) + off];
    };
    d(0, 0) = 0;
    for (size_t sym = 0; sym < n; sym++) {
      for (size_t bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
        size_t off_delta = 1U << (precision - bits);
        for (size_t off = 0; off + off_delta <= (1U << precision); off++) {
          d(sym + 1, off + off_delta) = std::min(
              d(sym, off) + freqs[sym] * bits, d(sym + 1, off + off_delta));
        }
      }
    }

    size_t sym = n;
    size_t off = 1U << precision;

    while (sym-- > 0) {
      assert(off > 0);
      for (size_t bits = min_limit[sym]; bits <= max_limit[sym]; bits++) {
        size_t off_delta = 1U << (precision - bits);
        if (off_delta <= off &&
            d(sym + 1, off) == d(sym, off - off_delta) + freqs[sym] * bits) {
          off -= off_delta;
          nbits[sym] = bits;
          break;
        }
      }
    }
  }

  void ComputeNBits(const uint64_t *collected_data) {
    constexpr uint64_t kBaselineData[286] = {
        113, 54, 28, 18, 12, 9, 7, 6, 5, 4, 3, 3, 2, 2, 2, 2, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 2, 2, 2, 2, 2, 3, 3, 4, 5, 6, 7, 9,
        12,  18, 29, 54, 1,  1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
        1,   1,  1,  1,  1,  1, 1, 1, 1, 1, 1, 1, 1,
    };

    uint64_t data[286];

    for (size_t i = 0; i < 286; i++) {
      data[i] = collected_data[i] + kBaselineData[i];
    }

    // Compute Huffman code length ensuring that all the "fake" symbols for [16,
    // 240) and [255, 285) have their maximum length.
    uint64_t collapsed_data[16 + 14 + 16 + 2] = {};
    uint8_t collapsed_min_limit[16 + 14 + 16 + 2] = {};
    uint8_t collapsed_max_limit[16 + 14 + 16 + 2];
    for (size_t i = 0; i < 48; i++) {
      collapsed_max_limit[i] = 8;
    }
    for (size_t i = 0; i < 16; i++) {
      collapsed_data[i] = data[i];
    }
    for (size_t i = 0; i < 14; i++) {
      collapsed_data[16 + i] = 1;
      collapsed_min_limit[16 + i] = 8;
    }
    for (size_t j = 0; j < 16; j++) {
      collapsed_data[16 + 14 + j] += data[240 + j];
    }
    collapsed_data[16 + 14 + 16] = 1;
    collapsed_min_limit[16 + 14 + 16] = 8;
    collapsed_data[16 + 14 + 16 + 1] = data[285];

    uint8_t collapsed_nbits[48] = {};
    ComputeCodeLengths(collapsed_data, 48, collapsed_min_limit,
                       collapsed_max_limit, collapsed_nbits);

    // Compute "extra" code lengths for symbols >= 256, except 285.
    uint8_t tail_nbits[29] = {};
    uint8_t tail_min_limit[29] = {};
    uint8_t tail_max_limit[29] = {};
    for (size_t i = 0; i < 29; i++) {
      tail_min_limit[i] = 4;
      tail_max_limit[i] = 7;
    }
    ComputeCodeLengths(data + 256, 29, tail_min_limit, tail_max_limit,
                       tail_nbits);

    for (size_t i = 0; i < 16; i++) {
      nbits[i] = collapsed_nbits[i];
    }
    for (size_t i = 0; i < 14; i++) {
      for (size_t j = 0; j < 16; j++) {
        nbits[(i + 1) * 16 + j] = collapsed_nbits[16 + i] + 4;
      }
    }
    for (size_t i = 0; i < 16; i++) {
      nbits[240 + i] = collapsed_nbits[30 + i];
    }
    for (size_t i = 0; i < 29; i++) {
      nbits[256 + i] = collapsed_nbits[46] + tail_nbits[i];
    }
    nbits[285] = collapsed_nbits[47];
  }

  void ComputeCanonicalCode(const uint8_t *nbits, uint16_t *bits) {
    uint8_t code_length_counts[16] = {};
    for (size_t i = 0; i < 286; i++) {
      code_length_counts[nbits[i]]++;
    }
    uint16_t next_code[16] = {};
    uint16_t code = 0;
    for (size_t i = 1; i < 16; i++) {
      code = (code + code_length_counts[i - 1]) << 1;
      next_code[i] = code;
    }
    for (size_t i = 0; i < 286; i++) {
      bits[i] = BitReverse(nbits[i], next_code[nbits[i]]++);
    }
  }

  void FillNBits() {
    for (size_t i = 0; i < 16; i++) {
      first16_nbits[i] = nbits[i];
      last16_nbits[i] = nbits[240 + i];
    }
    mid_nbits = nbits[16];
    for (size_t i = 16; i < 240; i++) {
      assert(nbits[i] == mid_nbits);
    }
    // Construct lz77 lookup tables.
    for (size_t i = 0; i < 29; i++) {
      for (size_t j = 0; j < (1U << kLZ77NBits[i]); j++) {
        lz77_length_nbits[kLZ77Base[i] + j] = nbits[257 + i] + kLZ77NBits[i];
        lz77_length_sym[kLZ77Base[i] + j] = 257 + i;
      }
    }

    dist_nbits = 1;

    approx_nbits[0] =
        nbits[0] - 1; // subtract 1 as a fudge for catering for RLE
    for (size_t i = 1; i < 15; i++) {
      approx_nbits[i] = (nbits[i] + nbits[256 - i] + 1) / 2;
    }
    approx_nbits[15] = mid_nbits;
  }

  void FillBits() {
    uint16_t bits[286];
    ComputeCanonicalCode(nbits, bits);
    for (size_t i = 0; i < 16; i++) {
      first16_bits[i] = bits[i];
      last16_bits[i] = bits[240 + i];
    }
    mid_lowbits[0] = mid_lowbits[15] = 0;
    for (size_t i = 16; i < 240; i += 16) {
      mid_lowbits[i / 16] = bits[i] & ((1U << (mid_nbits - 4)) - 1);
    }
    for (size_t i = 16; i < 240; i++) {
      assert((uint32_t(mid_lowbits[i / 16]) |
              (kBitReverseNibbleLookup[i % 16] << (mid_nbits - 4))) == bits[i]);
    }
    end_bits = bits[256];
    // Construct lz77 lookup tables.
    for (size_t i = 0; i < 29; i++) {
      for (size_t j = 0; j < (1U << kLZ77NBits[i]); j++) {
        lz77_length_bits[kLZ77Base[i] + j] =
            bits[257 + i] | (j << nbits[257 + i]);
      }
    }

    dist_bits = 0;
  }

  HuffmanTable(const uint64_t *collected_data) {
    ComputeNBits(collected_data);
    FillNBits();
    FillBits();
  }

  // estimate for CollectSymbolCounts
  // only fills nbits; skips computing actual codes
  HuffmanTable() {
    // the following is similar to ComputeNBits(0, 0, 0 ...), but much faster
    constexpr uint8_t collapsed_nbits[] = {
        2,  3,  4,  5,  5,  6,  6,  6,  7,  7,  7,  7,  8,  8,  8,  8,
        8,  8,  8,  8,  8,  7,  7,  7,  7,  6,  6,  6,  5,  5,  4,  3,

        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 13,
        13, 13, 13, 13, 13, 13, 13, 13, 13, 13, 12, 12, 12, 8};
    for (size_t i = 0; i < 16; i++) {
      nbits[i] = collapsed_nbits[i];
      nbits[240 + i] = collapsed_nbits[16 + i];
    }
    for (size_t i = 16; i < 240; i++) {
      nbits[i] = 12;
    }
    for (size_t i = 0; i < 30; i++) {
      nbits[256 + i] = collapsed_nbits[32 + i];
    }

    FillNBits();
  }
};

struct BitWriter {
  void Write(uint32_t count, uint64_t bits) {
    buffer |= bits << bits_in_buffer;
    bits_in_buffer += count;
    memcpy(data + bytes_written, &buffer, 8);
    size_t bytes_in_buffer = bits_in_buffer / 8;
    bits_in_buffer &= 7;
    buffer >>= bytes_in_buffer * 8;
    bytes_written += bytes_in_buffer;
  }

  void ZeroPadToByte() {
    if (bits_in_buffer != 0) {
      Write(8 - bits_in_buffer, 0);
    }
  }

  void WriteBytes(const char *data, size_t count) {
    memcpy(this->data + bytes_written, data, count);
    bytes_written += count;
  }

  unsigned char *data;
  size_t bytes_written = 0;
  size_t bits_in_buffer = 0;
  uint64_t buffer = 0;
};

static void WriteHuffmanCode(const HuffmanTable &table,
                             BitWriter *__restrict writer) {
  constexpr uint8_t kCodeLengthNbits[] = {
      4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 4, 0, 0, 0,
  };
  constexpr uint8_t kCodeLengthOrder[] = {
      16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15,
  };
  writer->Write(5, 29); // all lit/len codes
  writer->Write(5, 0);  // distance code up to dist, included
  writer->Write(4, 15); // all code length codes
  for (size_t i = 0; i < 19; i++) {
    writer->Write(3, kCodeLengthNbits[kCodeLengthOrder[i]]);
  }

  for (size_t i = 0; i < 286; i++) {
    writer->Write(4, kBitReverseNibbleLookup[table.nbits[i]]);
  }
  writer->Write(4, 0b1000);
}

constexpr unsigned kAdler32Mod = 65521;

static void UpdateAdler32(uint32_t &s1, uint32_t &s2, uint8_t byte) {
  s1 += byte;
  s2 += s1;
  s1 %= kAdler32Mod;
  s2 %= kAdler32Mod;
}

#include "internal/kernels_x86.h"

static void AppendBE32(size_t value, BitWriter *__restrict writer) {
  writer->Write(8, value >> 24);
  writer->Write(8, (value >> 16) & 0xFF);
  writer->Write(8, (value >> 8) & 0xFF);
  writer->Write(8, value & 0xFF);
}

static void WriteHeader(size_t width, size_t height, size_t bytes_per_channel,
                        size_t num_channels, char cicp_colorspace,
                        const FPNGEAdditionalChunk *additional_chunks,
                        int num_additional_chunks,
                        BitWriter *__restrict writer) {
  constexpr uint8_t kPNGHeader[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  for (size_t i = 0; i < 8; i++) {
    writer->Write(8, kPNGHeader[i]);
  }
  // Length
  writer->Write(32, 0x0d000000);
  assert(writer->bits_in_buffer == 0);
  size_t crc_start = writer->bytes_written;
  // IHDR
  writer->Write(32, 0x52444849);
  AppendBE32(width, writer);
  AppendBE32(height, writer);
  // Bit depth
  writer->Write(8, bytes_per_channel * 8);
  // Colour type
  constexpr uint8_t numc_to_colour_type[] = {0, 0, 4, 2, 6};
  writer->Write(8, numc_to_colour_type[num_channels]);
  // Compression, filter and interlace methods.
  writer->Write(24, 0);
  assert(writer->bits_in_buffer == 0);
  size_t crc_end = writer->bytes_written;
  uint32_t crc =
      Crc32().update_final(writer->data + crc_start, crc_end - crc_start);
  AppendBE32(crc, writer);

  if (cicp_colorspace == FPNGE_CICP_PQ) {
    writer->Write(32, 0x04000000);
    writer->Write(32, 0x50434963); // cICP
    writer->Write(32, 0x01001009); // PQ, Rec2020
    writer->Write(32, 0xfe23234d); // CRC
  }
  for (int i = 0; i < num_additional_chunks; i++) {
    AppendBE32(additional_chunks[i].data_size, writer);
    size_t crc_start = writer->bytes_written;
    uint32_t name = 0;
    memcpy(&name, additional_chunks[i].name, 4);
    writer->Write(32, name);
    writer->WriteBytes(additional_chunks[i].data,
                       additional_chunks[i].data_size);
    size_t crc_end = writer->bytes_written;
    uint32_t crc =
        Crc32().update_final(writer->data + crc_start, crc_end - crc_start);
    AppendBE32(crc, writer);
  }
}



} // namespace

extern "C" size_t FPNGEEncode(size_t bytes_per_channel, size_t num_channels,
                              const void *data, size_t width, size_t row_stride,
                              size_t height, void *output,
                              const struct FPNGEOptions *options) {
  assert(bytes_per_channel == 1 || bytes_per_channel == 2);
  assert(num_channels != 0 && num_channels <= 4);
  size_t bytes_per_line = bytes_per_channel * num_channels * width;
  assert(row_stride >= bytes_per_line);

  // allows for padding, and for extra initial space for the "left" pixel for
  // predictors.
  size_t bytes_per_line_buf =
      (bytes_per_line + 4 * bytes_per_channel + SIMD_WIDTH - 1) / SIMD_WIDTH *
      SIMD_WIDTH;

  // Extra space for alignment purposes.
  std::vector<unsigned char> buf(bytes_per_line_buf * 2 + SIMD_WIDTH - 1 +
                                 4 * bytes_per_channel);
  unsigned char *aligned_buf_ptr = buf.data() + 4 * bytes_per_channel;
  aligned_buf_ptr += (intptr_t)aligned_buf_ptr % SIMD_WIDTH
                         ? (SIMD_WIDTH - (intptr_t)aligned_buf_ptr % SIMD_WIDTH)
                         : 0;

  std::vector<unsigned char> pdata_buf(bytes_per_line_buf + SIMD_WIDTH - 1);
  unsigned char *aligned_pdata_ptr = pdata_buf.data();
  aligned_pdata_ptr +=
      (intptr_t)aligned_pdata_ptr % SIMD_WIDTH
          ? (SIMD_WIDTH - (intptr_t)aligned_pdata_ptr % SIMD_WIDTH)
          : 0;

  struct FPNGEOptions default_options;
  if (options == nullptr) {
    FPNGEFillOptions(&default_options, FPNGE_COMPRESS_LEVEL_DEFAULT,
                     FPNGE_CICP_NONE);
    options = &default_options;
  }

  // options sanity check
  assert(options->predictor >= 0 && options->predictor <= FPNGE_PREDICTOR_BEST);
  assert(options->huffman_sample >= 0 && options->huffman_sample <= 127);

  BitWriter writer;
  writer.data = static_cast<unsigned char *>(output);

  WriteHeader(width, height, bytes_per_channel, num_channels,
              options->cicp_colorspace, options->additional_chunks,
              options->num_additional_chunks, &writer);

  assert(writer.bits_in_buffer == 0);
  size_t chunk_length_pos = writer.bytes_written;
  writer.bytes_written += 4; // Skip space for length.
  size_t crc_pos = writer.bytes_written;
  writer.Write(32, 0x54414449); // IDAT
  // Deflate header
  writer.Write(8, 8);  // deflate with smallest window
  writer.Write(8, 29); // cfm+flg check value

  uint64_t symbol_counts[286] = {};

  // Sample rows in the center of the image.
  size_t y0 = height * (127 - options->huffman_sample) / 256;
  size_t y1 = height * (129 + options->huffman_sample) / 256;
  if (y1 == 0 && height > 0) { // for 1 pixel high images
    y1 = 1;
  }

  for (size_t y = y0; y < y1; y++) {
    const unsigned char *current_row_in =
        static_cast<const unsigned char *>(data) + row_stride * y;
    unsigned char *current_row_buf =
        aligned_buf_ptr + (y % 2 ? bytes_per_line_buf : 0);
    const unsigned char *top_buf =
        aligned_buf_ptr + ((y + 1) % 2 ? bytes_per_line_buf : 0);
    const unsigned char *left_buf =
        current_row_buf - bytes_per_channel * num_channels;
    const unsigned char *topleft_buf =
        top_buf - bytes_per_channel * num_channels;

    x86::CopyRow(current_row_buf, current_row_in, num_channels, bytes_per_channel,
            (FPNGEColorChannelOrder)options->channel_order, width);
    if (y == y0 && y != 0) {
      continue;
    }

    x86::CollectSymbolCounts(bytes_per_line, current_row_buf, top_buf, left_buf,
                        topleft_buf, aligned_pdata_ptr, symbol_counts, options);
  }

  memset(buf.data(), 0, buf.size());

  HuffmanTable huffman_table(symbol_counts);

  // Single block, dynamic huffman
  writer.Write(3, 0b101);
  WriteHuffmanCode(huffman_table, &writer);

  Crc32 crc;
  uint32_t s1 = 1;
  uint32_t s2 = 0;
  for (size_t y = 0; y < height; y++) {
    const unsigned char *current_row_in =
        static_cast<const unsigned char *>(data) + row_stride * y;
    unsigned char *current_row_buf =
        aligned_buf_ptr + (y % 2 ? bytes_per_line_buf : 0);
    const unsigned char *top_buf =
        aligned_buf_ptr + ((y + 1) % 2 ? bytes_per_line_buf : 0);
    const unsigned char *left_buf =
        current_row_buf - bytes_per_channel * num_channels;
    const unsigned char *topleft_buf =
        top_buf - bytes_per_channel * num_channels;

    x86::CopyRow(current_row_buf, current_row_in, num_channels, bytes_per_channel,
            (FPNGEColorChannelOrder)options->channel_order, width);

    x86::EncodeOneRow(bytes_per_line, current_row_buf, top_buf, left_buf,
                 topleft_buf, aligned_pdata_ptr, huffman_table, s1, s2, &writer,
                 options);

    crc_pos +=
        crc.update(writer.data + crc_pos, writer.bytes_written - crc_pos);
  }

  // EOB
  writer.Write(huffman_table.nbits[256], huffman_table.end_bits);

  writer.ZeroPadToByte();
  assert(writer.bits_in_buffer == 0);
  s1 %= kAdler32Mod;
  s2 %= kAdler32Mod;
  uint32_t adler32 = (s2 << 16) | s1;
  AppendBE32(adler32, &writer);

  size_t data_len = writer.bytes_written - chunk_length_pos - 8;
  writer.data[chunk_length_pos + 0] = data_len >> 24;
  writer.data[chunk_length_pos + 1] = (data_len >> 16) & 0xFF;
  writer.data[chunk_length_pos + 2] = (data_len >> 8) & 0xFF;
  writer.data[chunk_length_pos + 3] = data_len & 0xFF;

  auto final_crc =
      crc.update_final(writer.data + crc_pos, writer.bytes_written - crc_pos);
  AppendBE32(final_crc, &writer);

  // IEND
  writer.Write(32, 0);
  writer.Write(32, 0x444e4549);
  writer.Write(32, 0x826042ae);

  return writer.bytes_written;
}
