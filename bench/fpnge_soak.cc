// Copyright 2026
// Long-lived codec soak harness for M6 stability validation.

#include "../fpnge.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

struct Config {
  size_t width = 1280;
  size_t height = 720;
  size_t iterations = 1000;
  size_t warmup = 25;
  size_t sample_every = 50;
  size_t max_rss_growth_kb = 8192;
  double max_drift_pct = 25.0;
  std::string selected_case = "ui";
};

[[noreturn]] void Usage(const char *argv0, int code) {
  std::fprintf(code == 0 ? stdout : stderr,
      "Usage: %s [--width N] [--height N] [--iterations N] [--warmup N] "
      "[--sample-every N] [--max-rss-growth-kb N] [--max-drift-pct N] "
      "[--case ui|noise|solid|gradient]\n", argv0);
  std::exit(code);
}

size_t ParseSize(const char *flag, const char *value) {
  char *end = nullptr;
  const unsigned long long parsed = std::strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0) {
    std::fprintf(stderr, "Invalid value for %s: %s\n", flag, value);
    std::exit(2);
  }
  return static_cast<size_t>(parsed);
}

double ParseDouble(const char *flag, const char *value) {
  char *end = nullptr;
  const double parsed = std::strtod(value, &end);
  if (end == value || *end != '\0' || !std::isfinite(parsed) || parsed < 0) {
    std::fprintf(stderr, "Invalid value for %s: %s\n", flag, value);
    std::exit(2);
  }
  return parsed;
}

Config ParseArgs(int argc, char **argv) {
  Config c;
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--help") == 0) Usage(argv[0], 0);
    if (i + 1 >= argc) Usage(argv[0], 2);
    const char *value = argv[++i];
    if (std::strcmp(arg, "--width") == 0) c.width = ParseSize(arg, value);
    else if (std::strcmp(arg, "--height") == 0) c.height = ParseSize(arg, value);
    else if (std::strcmp(arg, "--iterations") == 0) c.iterations = ParseSize(arg, value);
    else if (std::strcmp(arg, "--warmup") == 0) c.warmup = ParseSize(arg, value);
    else if (std::strcmp(arg, "--sample-every") == 0) c.sample_every = ParseSize(arg, value);
    else if (std::strcmp(arg, "--max-rss-growth-kb") == 0) c.max_rss_growth_kb = ParseSize(arg, value);
    else if (std::strcmp(arg, "--max-drift-pct") == 0) c.max_drift_pct = ParseDouble(arg, value);
    else if (std::strcmp(arg, "--case") == 0) c.selected_case = value;
    else Usage(argv[0], 2);
  }
  return c;
}

uint32_t XorShift32(uint32_t &state) {
  state ^= state << 13;
  state ^= state >> 17;
  state ^= state << 5;
  return state;
}

std::vector<uint8_t> MakePixels(const std::string &name, size_t width,
                                size_t height) {
  std::vector<uint8_t> pixels(width * height * 4);
  uint32_t rng = 0xC001D00Du;
  for (size_t y = 0; y < height; ++y) {
    for (size_t x = 0; x < width; ++x) {
      uint8_t *p = &pixels[(y * width + x) * 4];
      if (name == "solid") {
        p[0] = 40; p[1] = 90; p[2] = 170; p[3] = 255;
      } else if (name == "gradient") {
        p[0] = width > 1 ? static_cast<uint8_t>((255 * x) / (width - 1)) : 0;
        p[1] = height > 1 ? static_cast<uint8_t>((255 * y) / (height - 1)) : 0;
        p[2] = static_cast<uint8_t>((x + y) & 0xff); p[3] = 255;
      } else if (name == "ui") {
        const bool panel = ((x / 96) + (y / 64)) % 5 == 0;
        const bool text = (y % 23) < 2 && (x % 13) < 9;
        const bool line = (x % 127) == 0 || (y % 89) == 0;
        p[0] = panel ? 44 : (text ? 230 : 24);
        p[1] = panel ? 48 : (text ? 230 : 27);
        p[2] = panel ? 54 : (line ? 190 : 31);
        p[3] = static_cast<uint8_t>(220 + ((x + y) % 36));
      } else if (name == "noise") {
        const uint32_t v = XorShift32(rng);
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = 255;
      } else {
        std::fprintf(stderr, "Unknown soak case: %s\n", name.c_str());
        std::exit(2);
      }
    }
  }
  return pixels;
}

uint64_t HashBytes(const uint8_t *data, size_t size) {
  uint64_t hash = 1469598103934665603ULL;
  for (size_t i = 0; i < size; ++i) {
    hash ^= data[i];
    hash *= 1099511628211ULL;
  }
  return hash;
}

bool LooksLikePng(const uint8_t *data, size_t size) {
  static constexpr uint8_t kSig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
  static constexpr uint8_t kIend[12] =
      {0, 0, 0, 0, 'I', 'E', 'N', 'D', 0xae, 0x42, 0x60, 0x82};
  return size >= 20 && std::memcmp(data, kSig, sizeof(kSig)) == 0 &&
         std::memcmp(data + size - sizeof(kIend), kIend, sizeof(kIend)) == 0;
}

size_t ReadRssKb() {
  FILE *f = std::fopen("/proc/self/status", "r");
  if (!f) return 0;
  char line[256];
  size_t rss = 0;
  while (std::fgets(line, sizeof(line), f)) {
    if (std::sscanf(line, "VmRSS: %zu kB", &rss) == 1) break;
  }
  std::fclose(f);
  return rss;
}

long ReadLongFile(const char *path) {
  FILE *f = std::fopen(path, "r");
  if (!f) return -1;
  long value = -1;
  if (std::fscanf(f, "%ld", &value) != 1) value = -1;
  std::fclose(f);
  return value;
}

double Percentile(std::vector<double> values, double q) {
  if (values.empty()) return 0.0;
  std::sort(values.begin(), values.end());
  const double pos = q * static_cast<double>(values.size() - 1);
  const size_t lo = static_cast<size_t>(pos);
  const size_t hi = std::min(lo + 1, values.size() - 1);
  const double frac = pos - static_cast<double>(lo);
  return values[lo] * (1.0 - frac) + values[hi] * frac;
}

int Run(const Config &c) {
  const auto pixels = MakePixels(c.selected_case, c.width, c.height);
  std::vector<uint8_t> output(
      FPNGEOutputAllocSize(1, 4, c.width, c.height));

  FPNGEOptions options;
  FPNGEFillOptions(&options, FPNGE_COMPRESS_LEVEL_DEFAULT, FPNGE_CICP_NONE);

  size_t encoded_size = 0;
  for (size_t i = 0; i < c.warmup; ++i) {
    encoded_size = FPNGEEncode(1, 4, pixels.data(), c.width, c.width * 4,
                               c.height, output.data(), &options);
  }
  if (!LooksLikePng(output.data(), encoded_size)) {
    std::fprintf(stderr, "soak: warmup output is not a complete PNG\n");
    return 1;
  }

  const uint64_t expected_hash = HashBytes(output.data(), encoded_size);
  const size_t expected_size = encoded_size;
  const size_t rss_start = ReadRssKb();
  size_t rss_peak = rss_start;
  double temp_max_c = -1.0;
  long freq_min_khz = -1;
  long freq_max_khz = -1;

  std::vector<double> samples_ms;
  samples_ms.reserve(c.iterations);

  for (size_t i = 0; i < c.iterations; ++i) {
    const auto start = std::chrono::steady_clock::now();
    encoded_size = FPNGEEncode(1, 4, pixels.data(), c.width, c.width * 4,
                               c.height, output.data(), &options);
    const auto end = std::chrono::steady_clock::now();
    samples_ms.push_back(
        std::chrono::duration<double, std::milli>(end - start).count());

    if (encoded_size != expected_size) {
      std::fprintf(stderr, "soak: encoded size changed at iteration %zu\n", i);
      return 1;
    }

    const bool sample = (i % c.sample_every) == 0 || i + 1 == c.iterations;
    if (sample) {
      if (!LooksLikePng(output.data(), encoded_size) ||
          HashBytes(output.data(), encoded_size) != expected_hash) {
        std::fprintf(stderr, "soak: deterministic output changed at iteration %zu\n", i);
        return 1;
      }
      rss_peak = std::max(rss_peak, ReadRssKb());
      const long temp_millic = ReadLongFile("/sys/class/thermal/thermal_zone0/temp");
      if (temp_millic >= 0) {
        temp_max_c = std::max(temp_max_c, temp_millic / 1000.0);
      }
      const long freq =
          ReadLongFile("/sys/devices/system/cpu/cpu0/cpufreq/scaling_cur_freq");
      if (freq >= 0) {
        if (freq_min_khz < 0 || freq < freq_min_khz) freq_min_khz = freq;
        if (freq > freq_max_khz) freq_max_khz = freq;
      }
    }
  }

  const size_t rss_end = ReadRssKb();
  rss_peak = std::max(rss_peak, rss_end);
  const size_t rss_growth = rss_end > rss_start ? rss_end - rss_start : 0;

  const double median = Percentile(samples_ms, 0.50);
  const double p95 = Percentile(samples_ms, 0.95);
  const double p99 = Percentile(samples_ms, 0.99);
  const size_t quartile = std::max<size_t>(1, c.iterations / 4);
  const std::vector<double> first(samples_ms.begin(),
                                  samples_ms.begin() + quartile);
  const std::vector<double> last(samples_ms.end() - quartile,
                                 samples_ms.end());
  const double first_median = Percentile(first, 0.50);
  const double last_median = Percentile(last, 0.50);
  const double drift_pct =
      first_median == 0.0 ? 0.0 : ((last_median / first_median) - 1.0) * 100.0;
  const double megapixels =
      static_cast<double>(c.width) * static_cast<double>(c.height) / 1'000'000.0;
  const double mp_s = megapixels / (median / 1000.0);

  std::puts("case,width,height,iterations,median_ms,p95_ms,p99_ms,mp_s,"
            "encoded_bytes,first_q_median_ms,last_q_median_ms,drift_pct,"
            "rss_start_kb,rss_peak_kb,rss_end_kb,rss_growth_kb,"
            "temp_max_c,freq_min_khz,freq_max_khz,output_hash");
  std::printf("%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.3f,%zu,"
              "%.6f,%.6f,%.3f,%zu,%zu,%zu,%zu,%.1f,%ld,%ld,%016llx\n",
              c.selected_case.c_str(), c.width, c.height, c.iterations,
              median, p95, p99, mp_s, encoded_size, first_median, last_median,
              drift_pct, rss_start, rss_peak, rss_end, rss_growth, temp_max_c,
              freq_min_khz, freq_max_khz,
              static_cast<unsigned long long>(expected_hash));

  if (rss_growth > c.max_rss_growth_kb) {
    std::fprintf(stderr,
                 "soak: RSS growth %zu kB exceeds limit %zu kB\n",
                 rss_growth, c.max_rss_growth_kb);
    return 1;
  }
  if (drift_pct > c.max_drift_pct) {
    std::fprintf(stderr,
                 "soak: latency drift %.2f%% exceeds limit %.2f%%\n",
                 drift_pct, c.max_drift_pct);
    return 1;
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  return Run(ParseArgs(argc, argv));
}
