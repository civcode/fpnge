#include "fpnge.h"
#include "lodepng.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

constexpr uint8_t kGuard = 0xA5;
constexpr size_t kGuardSize = 64;

struct TestStats {
  size_t cases = 0;
  size_t failures = 0;
};

struct CaseSpec {
  size_t width;
  size_t height;
  size_t bytes_per_channel;
  size_t channels;
  FPNGEColorChannelOrder order;
  int predictor;
  size_t input_offset;
  size_t stride_padding;
  const char* family;
  bool cicp;
  bool additional_chunk;
  int fill_level = FPNGE_COMPRESS_LEVEL_DEFAULT;
};

uint32_t Crc32Reference(const uint8_t* data, size_t size) {
  uint32_t crc = 0xffffffffu;
  for (size_t i = 0; i < size; ++i) {
    crc ^= data[i];
    for (int bit = 0; bit < 8; ++bit) {
      crc = (crc >> 1) ^ ((crc & 1u) ? 0xedb88320u : 0u);
    }
  }
  return ~crc;
}

uint32_t ReadBE32(const uint8_t* p) {
  return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) |
         (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

LodePNGColorType ColorTypeForChannels(size_t channels) {
  switch (channels) {
    case 1: return LCT_GREY;
    case 2: return LCT_GREY_ALPHA;
    case 3: return LCT_RGB;
    case 4: return LCT_RGBA;
    default: std::abort();
  }
}

uint16_t SampleValue(const char* family, size_t x, size_t y, size_t c,
                     size_t width, size_t height, uint32_t* rng) {
  const uint16_t maxv = 0xffffu;
  if (std::strcmp(family, "solid") == 0) {
    static constexpr uint16_t k[] = {0x1234, 0x5678, 0x9abc, 0xffff};
    return k[c & 3];
  }
  if (std::strcmp(family, "gradient") == 0) {
    const uint32_t xv = width > 1 ? uint32_t((uint64_t(x) * maxv) / (width - 1)) : 0;
    const uint32_t yv = height > 1 ? uint32_t((uint64_t(y) * maxv) / (height - 1)) : 0;
    if (c == 0) return uint16_t(xv);
    if (c == 1) return uint16_t(yv);
    if (c == 2) return uint16_t((xv + yv) / 2);
    return uint16_t(maxv - ((xv + yv) / 2));
  }
  if (std::strcmp(family, "checker") == 0) {
    return (((x / 3) + (y / 2) + c) & 1) ? 0xffff : 0x0101;
  }
  if (std::strcmp(family, "sparse") == 0) {
    const bool changed = y == 0 || ((x + c * 7 + y * 13) % 29 == 0);
    return changed ? uint16_t((x * 1103 + y * 3511 + c * 7717) & 0xffff) :
                     uint16_t((c * 9001) & 0xffff);
  }
  if (std::strcmp(family, "alpha") == 0) {
    if (c == 3 || c == 1) {
      return width > 1 ? uint16_t((uint64_t(x) * maxv) / (width - 1)) : 0xffff;
    }
    return uint16_t((0x2222u * (c + 1) + y * 257u) & 0xffff);
  }
  if (std::strcmp(family, "ui") == 0) {
    const bool panel = ((x / 11) + (y / 7)) % 5 == 0;
    const bool text = (y % 13) < 2 && (x % 9) < 6;
    const bool line = (x % 31) == 0 || (y % 23) == 0;
    const uint16_t base = panel ? 0x3333 : (text ? 0xeaea : (line ? 0xaaaa : 0x1818));
    return c == 3 ? uint16_t(0xd000u + ((x + y) & 0x2fffu)) : uint16_t(base + c * 0x0303u);
  }
  // deterministic noise
  uint32_t v = *rng;
  v ^= v << 13;
  v ^= v >> 17;
  v ^= v << 5;
  *rng = v;
  return uint16_t(v >> 8);
}

void StoreSample(uint8_t* dst, size_t bytes_per_channel, uint16_t value) {
  if (bytes_per_channel == 1) {
    dst[0] = uint8_t(value >> 8);
  } else {
    dst[0] = uint8_t(value >> 8);
    dst[1] = uint8_t(value);
  }
}

void BuildInput(const CaseSpec& spec, std::vector<uint8_t>* storage,
                std::vector<uint8_t>* expected, size_t* stride) {
  const size_t pixel_bytes = spec.channels * spec.bytes_per_channel;
  const size_t row_bytes = spec.width * pixel_bytes;
  *stride = row_bytes + spec.stride_padding;
  storage->assign(spec.input_offset + (*stride) * spec.height + 32, 0x6d);
  expected->assign(row_bytes * spec.height, 0);
  uint8_t* input = storage->data() + spec.input_offset;
  uint32_t rng = 0xc001d00du ^ uint32_t(spec.width * 131 + spec.height * 17 + spec.channels);

  for (size_t y = 0; y < spec.height; ++y) {
    std::fill(input + y * (*stride) + row_bytes,
              input + y * (*stride) + (*stride), uint8_t(0x80u + ((y * 17u) & 0x7fu)));
    for (size_t x = 0; x < spec.width; ++x) {
      std::array<uint16_t, 4> logical{};
      for (size_t c = 0; c < spec.channels; ++c) {
        logical[c] = SampleValue(spec.family, x, y, c, spec.width, spec.height, &rng);
      }
      for (size_t c = 0; c < spec.channels; ++c) {
        const size_t source_c =
            (spec.order == FPNGE_ORDER_BGR && spec.channels >= 3 && c < 3) ? 2 - c : c;
        StoreSample(input + y * (*stride) + x * pixel_bytes + c * spec.bytes_per_channel,
                    spec.bytes_per_channel, logical[source_c]);
        StoreSample(expected->data() + (y * spec.width + x) * pixel_bytes + c * spec.bytes_per_channel,
                    spec.bytes_per_channel, logical[c]);
      }
    }
  }
}

bool ValidateChunks(const uint8_t* png, size_t size, const CaseSpec& spec,
                    std::string* error) {
  static constexpr uint8_t kSignature[] = {137,80,78,71,13,10,26,10};
  if (size < sizeof(kSignature) || std::memcmp(png, kSignature, sizeof(kSignature)) != 0) {
    *error = "bad PNG signature";
    return false;
  }
  bool saw_ihdr = false;
  bool saw_idat = false;
  bool saw_iend = false;
  bool saw_cicp = false;
  bool saw_extra = false;
  size_t pos = 8;
  while (pos + 12 <= size) {
    const uint32_t len = ReadBE32(png + pos);
    if (uint64_t(pos) + 12u + len > size) {
      *error = "chunk exceeds encoded size";
      return false;
    }
    const uint8_t* type = png + pos + 4;
    const uint8_t* data = png + pos + 8;
    const uint32_t stored_crc = ReadBE32(data + len);
    const uint32_t actual_crc = Crc32Reference(type, size_t(len) + 4);
    if (stored_crc != actual_crc) {
      char message[128];
      std::snprintf(message, sizeof(message), "CRC mismatch in %.4s", type);
      *error = message;
      return false;
    }
    if (std::memcmp(type, "IHDR", 4) == 0) {
      if (len != 13 || ReadBE32(data) != spec.width || ReadBE32(data + 4) != spec.height ||
          data[8] != spec.bytes_per_channel * 8) {
        *error = "IHDR semantic mismatch";
        return false;
      }
      static constexpr uint8_t kColorType[] = {0,0,4,2,6};
      if (data[9] != kColorType[spec.channels]) {
        *error = "IHDR color type mismatch";
        return false;
      }
      saw_ihdr = true;
    } else if (std::memcmp(type, "IDAT", 4) == 0) {
      saw_idat = true;
    } else if (std::memcmp(type, "IEND", 4) == 0) {
      saw_iend = true;
    } else if (std::memcmp(type, "cICP", 4) == 0) {
      static constexpr uint8_t kExpected[] = {9,16,0,1};
      saw_cicp = len == 4 && std::memcmp(data, kExpected, 4) == 0;
    } else if (std::memcmp(type, "vpAg", 4) == 0) {
      static constexpr char kPayload[] = "m1-test";
      saw_extra = len == sizeof(kPayload) - 1 &&
                  std::memcmp(data, kPayload, sizeof(kPayload) - 1) == 0;
    }
    pos += 12 + len;
    if (saw_iend) break;
  }
  if (!saw_ihdr || !saw_idat || !saw_iend) {
    *error = "required PNG chunk missing";
    return false;
  }
  if (spec.cicp != saw_cicp) {
    *error = "cICP presence/payload mismatch";
    return false;
  }
  if (spec.additional_chunk != saw_extra) {
    *error = "additional chunk presence/payload mismatch";
    return false;
  }
  return true;
}

bool RunCase(const CaseSpec& spec, TestStats* stats) {
  ++stats->cases;
  std::vector<uint8_t> input_storage;
  std::vector<uint8_t> expected;
  size_t stride = 0;
  BuildInput(spec, &input_storage, &expected, &stride);

  FPNGEOptions options;
  FPNGEFillOptions(&options, spec.fill_level,
                   spec.cicp ? FPNGE_CICP_PQ : FPNGE_CICP_NONE);
  options.predictor = char(spec.predictor);
  options.channel_order = char(spec.order);
  static constexpr char kPayload[] = "m1-test";
  FPNGEAdditionalChunk chunk{{'v','p','A','g'}, kPayload, int(sizeof(kPayload) - 1)};
  if (spec.additional_chunk) {
    options.num_additional_chunks = 1;
    options.additional_chunks = &chunk;
  }

  const size_t alloc = FPNGEOutputAllocSize(spec.bytes_per_channel, spec.channels,
                                             spec.width, spec.height);
  std::vector<uint8_t> guarded(kGuardSize + alloc + kGuardSize, kGuard);
  uint8_t* output = guarded.data() + kGuardSize;
  const size_t encoded = FPNGEEncode(spec.bytes_per_channel, spec.channels,
      input_storage.data() + spec.input_offset, spec.width, stride, spec.height,
      output, &options);

  auto fail = [&](const std::string& message) {
    ++stats->failures;
    std::fprintf(stderr,
        "FAIL %s %zux%zu bpc=%zu ch=%zu order=%d pred=%d off=%zu pad=%zu: %s\n",
        spec.family, spec.width, spec.height, spec.bytes_per_channel, spec.channels,
        int(spec.order), spec.predictor, spec.input_offset, spec.stride_padding,
        message.c_str());
    return false;
  };

  if (encoded > alloc) return fail("FPNGEEncode returned size beyond FPNGEOutputAllocSize");
  if (!std::all_of(guarded.begin(), guarded.begin() + kGuardSize,
                   [](uint8_t v) { return v == kGuard; }) ||
      !std::all_of(guarded.begin() + kGuardSize + alloc, guarded.end(),
                   [](uint8_t v) { return v == kGuard; })) {
    return fail("output guard region modified");
  }

  std::string chunk_error;
  if (!ValidateChunks(output, encoded, spec, &chunk_error)) return fail(chunk_error);

  unsigned char* decoded = nullptr;
  unsigned width = 0;
  unsigned height = 0;
  const unsigned decode_error = lodepng_decode_memory(
      &decoded, &width, &height, output, encoded, ColorTypeForChannels(spec.channels),
      unsigned(spec.bytes_per_channel * 8));
  if (decode_error != 0) {
    return fail(std::string("lodepng decode failed: ") + lodepng_error_text(decode_error));
  }
  const size_t expected_bytes = expected.size();
  const bool pixels_match = width == spec.width && height == spec.height &&
                            std::memcmp(decoded, expected.data(), expected_bytes) == 0;
  std::free(decoded);
  if (!pixels_match) return fail("decoded pixels differ from logical source");
  return true;
}

void RunFullApiMatrix(TestStats* stats) {
  const char* families[] = {"gradient", "ui", "noise"};
  size_t family_index = 0;
  for (size_t bpc : {size_t(1), size_t(2)}) {
    for (size_t channels = 1; channels <= 4; ++channels) {
      const FPNGEColorChannelOrder orders[] = {FPNGE_ORDER_RGB, FPNGE_ORDER_BGR};
      const size_t order_count = channels >= 3 ? 2 : 1;
      for (size_t oi = 0; oi < order_count; ++oi) {
        for (int predictor = 0; predictor <= 6; ++predictor) {
          CaseSpec spec{17, 5, bpc, channels, orders[oi], predictor,
                        size_t((predictor * 5 + channels) & 31),
                        size_t((predictor * 11 + bpc * 7) & 31),
                        families[family_index++ % 3], false, false};
          RunCase(spec, stats);
        }
      }
    }
  }
}

void RunFillOptionsLevels(TestStats* stats) {
  for (int level = 1; level <= 5; ++level) {
    FPNGEOptions filled;
    FPNGEFillOptions(&filled, level, FPNGE_CICP_NONE);
    CaseSpec spec{33, 4, size_t(level & 1 ? 1 : 2), 4, FPNGE_ORDER_RGB,
                  int(filled.predictor), size_t(level), size_t(level * 3),
                  "checker", false, false, level};
    RunCase(spec, stats);
  }
}

void RunBoundaryMatrix(TestStats* stats) {
  static constexpr size_t widths[] = {
      1,2,3,4,7,8,9,15,16,17,31,32,33,47,48,49,63,64,65,127,128,129,255,256,257};
  for (size_t width : widths) {
    for (size_t offset = 0; offset < 32; ++offset) {
      const size_t padding = (offset * 7 + width * 3) & 31;
      CaseSpec spec{width, 3, (offset & 1) ? 1u : 2u,
                    (offset % 3 == 0) ? 3u : 4u,
                    (offset % 4 == 0) ? FPNGE_ORDER_BGR : FPNGE_ORDER_RGB,
                    int(offset % 7), offset, padding,
                    (offset & 2) ? "sparse" : "gradient", false, false};
      RunCase(spec, stats);
    }
  }
}

void RunImageFamilies(TestStats* stats) {
  static constexpr const char* families[] = {
      "solid", "gradient", "checker", "sparse", "alpha", "ui", "noise"};
  for (size_t i = 0; i < std::size(families); ++i) {
    CaseSpec spec{49 + i, 11, i & 1 ? 2u : 1u, 4,
                  i & 1 ? FPNGE_ORDER_BGR : FPNGE_ORDER_RGB,
                  FPNGE_PREDICTOR_BEST, i, (i * 5) & 31,
                  families[i], false, false};
    RunCase(spec, stats);
  }
}

void RunMetadataCases(TestStats* stats) {
  RunCase(CaseSpec{19, 7, 1, 4, FPNGE_ORDER_RGB, FPNGE_PREDICTOR_BEST,
                   3, 5, "ui", true, false}, stats);
  RunCase(CaseSpec{21, 6, 2, 3, FPNGE_ORDER_BGR, FPNGE_PREDICTOR_APPROX,
                   7, 11, "gradient", false, true}, stats);
  RunCase(CaseSpec{23, 5, 1, 4, FPNGE_ORDER_RGB, FPNGE_PREDICTOR_BEST,
                   9, 13, "noise", true, true}, stats);
}

void RunBgr16TailRegression(TestStats* stats) {
  // Width 1 forces the scalar 3-channel 16-bit BGR tail path. Distinct high and
  // low bytes make a one-byte channel source error immediately visible.
  RunCase(CaseSpec{1, 1, 2, 3, FPNGE_ORDER_BGR, FPNGE_PREDICTOR_FIXED_NOOP,
                   0, 0, "solid", false, false}, stats);
  RunCase(CaseSpec{9, 2, 2, 3, FPNGE_ORDER_BGR, FPNGE_PREDICTOR_FIXED_NOOP,
                   5, 7, "gradient", false, false}, stats);
}

}  // namespace

int main() {
  TestStats stats;
  RunBgr16TailRegression(&stats);
  RunFullApiMatrix(&stats);
  RunFillOptionsLevels(&stats);
  RunBoundaryMatrix(&stats);
  RunImageFamilies(&stats);
  RunMetadataCases(&stats);
  std::printf("FPNGE API correctness: %zu cases, %zu failures\n", stats.cases, stats.failures);
  return stats.failures == 0 ? 0 : 1;
}
