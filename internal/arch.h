// Copyright 2026
#ifndef FPNGE_INTERNAL_ARCH_H_
#define FPNGE_INTERNAL_ARCH_H_

#if defined(__x86_64__) || defined(__amd64__) || defined(_M_X64) ||             \
    defined(_M_AMD64) || (defined(_WIN64) && !defined(_M_ARM64))
#define FPNGE_ARCH_X86_64 1
#else
#define FPNGE_ARCH_X86_64 0
#endif

#if defined(__aarch64__) || defined(_M_ARM64)
#define FPNGE_ARCH_AARCH64 1
#else
#define FPNGE_ARCH_AARCH64 0
#endif

#endif  // FPNGE_INTERNAL_ARCH_H_
