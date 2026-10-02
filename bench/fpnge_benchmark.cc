// Copyright 2026
//
// Standalone synthetic benchmark for FPNGE. This intentionally avoids decoder,
// rendering, readback, and transport work so the measurement is codec-only.

#include "../fpnge.h"

#include <algorithm>
#include <chrono>
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
  size_t iterations = 20;
  size_t warmup = 3;
  std::string selected_case = "all";
};

[[noreturn]] void Usage(const char *argv0, int exit_code) {
  std::fprintf(
      exit_code == 0 ? stdout : stderr,
      "Usage: %s [--width N] [--height N] [--iterations N] [--warmup N] "
      "[--case all|solid|gradient|ui|noise]\n",
      argv0);
  std::exit(exit_code);
}

size_t ParseSize(const char *flag, const char *value) {
  char *end = nullptr;
  unsigned long long parsed = std::strtoull(value, &end, 10);
  if (end == value || *end != '\0' || parsed == 0) {
    std::fprintf(stderr, "Invalid value for %s: %s\n", flag, value);
    std::exit(2);
  }
  return static_cast<size_t>(parsed);
}

Config ParseArgs(int argc, char **argv) {
  Config config;
  for (int i = 1; i < argc; ++i) {
    const char *arg = argv[i];
    if (std::strcmp(arg, "--help") == 0) {
      Usage(argv[0], 0);
    }
    if (i + 1 >= argc) {
      Usage(argv[0], 2);
    }
    const char *value = argv[++i];
    if (std::strcmp(arg, "--width") == 0) {
      config.width = ParseSize(arg, value);
    } else if (std::strcmp(arg, "--height") == 0) {
      config.height = ParseSize(arg, value);
    } else if (std::strcmp(arg, "--iterations") == 0) {
      config.iterations = ParseSize(arg, value);
    } else if (std::strcmp(arg, "--warmup") == 0) {
      config.warmup = ParseSize(arg, value);
    } else if (std::strcmp(arg, "--case") == 0) {
      config.selected_case = value;
    } else {
      std::fprintf(stderr, "Unknown argument: %s\n", arg);
      Usage(argv[0], 2);
    }
  }
  return config;
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
        p[0] = 40;
        p[1] = 90;
        p[2] = 170;
        p[3] = 255;
      } else if (name == "gradient") {
        p[0] = width > 1 ? static_cast<uint8_t>((255 * x) / (width - 1)) : 0;
        p[1] = height > 1 ? static_cast<uint8_t>((255 * y) / (height - 1)) : 0;
        p[2] = static_cast<uint8_t>((x + y) & 0xFF);
        p[3] = 255;
      } else if (name == "ui") {
        const bool panel = ((x / 96) + (y / 64)) % 5 == 0;
        const bool text = (y % 23) < 2 && (x % 13) < 9;
        const bool line = (x % 127) == 0 || (y % 89) == 0;
        p[0] = panel ? 44 : (text ? 230 : 24);
        p[1] = panel ? 48 : (text ? 230 : 27);
        p[2] = panel ? 54 : (line ? 190 : 31);
        p[3] = static_cast<uint8_t>(220 + ((x + y) % 36));
      } else if (name == "noise") {
        uint32_t v = XorShift32(rng);
        p[0] = static_cast<uint8_t>(v);
        p[1] = static_cast<uint8_t>(v >> 8);
        p[2] = static_cast<uint8_t>(v >> 16);
        p[3] = 255;
      } else {
        std::fprintf(stderr, "Unknown benchmark case: %s\n", name.c_str());
        std::exit(2);
      }
    }
  }

  return pixels;
}

double Percentile(const std::vector<double> &sorted, double q) {
  if (sorted.empty()) {
    return 0.0;
  }
  const double pos = q * static_cast<double>(sorted.size() - 1);
  const size_t lo = static_cast<size_t>(pos);
  const size_t hi = std::min(lo + 1, sorted.size() - 1);
  const double frac = pos - static_cast<double>(lo);
  return sorted[lo] * (1.0 - frac) + sorted[hi] * frac;
}

void RunCase(const std::string &name, const Config &config) {
  const std::vector<uint8_t> pixels =
      MakePixels(name, config.width, config.height);
  std::vector<uint8_t> output(
      FPNGEOutputAllocSize(1, 4, config.width, config.height));

  FPNGEOptions options;
  FPNGEFillOptions(&options, FPNGE_COMPRESS_LEVEL_DEFAULT, FPNGE_CICP_NONE);

  size_t encoded_size = 0;
  for (size_t i = 0; i < config.warmup; ++i) {
    encoded_size = FPNGEEncode(1, 4, pixels.data(), config.width,
                               config.width * 4, config.height, output.data(),
                               &options);
  }

  std::vector<double> samples_ms;
  samples_ms.reserve(config.iterations);

  for (size_t i = 0; i < config.iterations; ++i) {
    const auto start = std::chrono::steady_clock::now();
    encoded_size = FPNGEEncode(1, 4, pixels.data(), config.width,
                               config.width * 4, config.height, output.data(),
                               &options);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    samples_ms.push_back(ms);
  }

  std::sort(samples_ms.begin(), samples_ms.end());
  const double median_ms = Percentile(samples_ms, 0.50);
  const double p95_ms = Percentile(samples_ms, 0.95);
  const double p99_ms = Percentile(samples_ms, 0.99);
  const double megapixels =
      static_cast<double>(config.width) * static_cast<double>(config.height) /
      1'000'000.0;
  const double mp_s = megapixels / (median_ms / 1000.0);

  std::printf("%s,%zu,%zu,%zu,%.6f,%.6f,%.6f,%.3f,%zu\n", name.c_str(),
              config.width, config.height, config.iterations, median_ms, p95_ms,
              p99_ms, mp_s, encoded_size);
}

}  // namespace

int main(int argc, char **argv) {
  const Config config = ParseArgs(argc, argv);
  const char *cases[] = {"solid", "gradient", "ui", "noise"};

  std::puts(
      "case,width,height,iterations,median_ms,p95_ms,p99_ms,mp_s,encoded_bytes");

  if (config.selected_case == "all") {
    for (const char *name : cases) {
      RunCase(name, config);
    }
  } else {
    RunCase(config.selected_case, config);
  }
  return 0;
}
