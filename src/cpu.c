/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <stdlib.h>

#if defined(_MSC_VER) && (defined(_M_X64) || defined(_M_IX86))
#include <intrin.h>
#define MINLZ_CAN_CPUID 1
#elif (defined(__GNUC__) || defined(__clang__)) && \
    (defined(__x86_64__) || defined(__i386__))
#include <cpuid.h>
#define MINLZ_CAN_CPUID 1
#else
#define MINLZ_CAN_CPUID 0
#endif

#if defined(__x86_64__) || defined(_M_X64)
#define MINLZ_CPU_X86_64 1
#else
#define MINLZ_CPU_X86_64 0
#endif

static minlz_cpu_features g_features;
static int g_features_ready;
static int g_backend_ready;
static minlz_backend g_backend;
#if MINLZ_HAVE_INLINE_ASM_AMD64
static int g_inline_asm_ready;
static int g_inline_asm_enabled;
#endif

#if MINLZ_CAN_CPUID
static void minlz_cpuid(int leaf, int subleaf, int regs[4]) {
#if defined(_MSC_VER)
    __cpuidex(regs, leaf, subleaf);
#else
    unsigned a = 0;
    unsigned b = 0;
    unsigned c = 0;
    unsigned d = 0;
    __cpuid_count((unsigned)leaf, (unsigned)subleaf, a, b, c, d);
    regs[0] = (int)a;
    regs[1] = (int)b;
    regs[2] = (int)c;
    regs[3] = (int)d;
#endif
}

static uint64_t minlz_xgetbv0(void) {
#if defined(_MSC_VER)
    return _xgetbv(0);
#elif defined(__GNUC__) || defined(__clang__)
    uint32_t eax;
    uint32_t edx;
    __asm__ volatile(".byte 0x0f, 0x01, 0xd0" : "=a"(eax), "=d"(edx) : "c"(0));
    return (uint64_t)eax | ((uint64_t)edx << 32);
#else
    return 0;
#endif
}
#endif

const minlz_cpu_features *minlz_cpu_features_get(void) {
    if (!g_features_ready) {
        minlz_cpu_features f;
        memset(&f, 0, sizeof(f));
#if MINLZ_CAN_CPUID
        {
            int r0[4];
            int r1[4];
            int r7[4];
            int max_leaf;
            int osxsave;
            uint64_t xcr0 = 0;
            minlz_cpuid(0, 0, r0);
            max_leaf = r0[0];
            if (max_leaf >= 1) {
                minlz_cpuid(1, 0, r1);
                f.x86 = 1;
                f.sse42 = (r1[2] & (1 << 20)) != 0;
                f.pclmul = (r1[2] & (1 << 1)) != 0;
                f.avx = (r1[2] & (1 << 28)) != 0;
                osxsave = (r1[2] & (1 << 27)) != 0;
                if (osxsave) {
                    xcr0 = minlz_xgetbv0();
                }
                if ((xcr0 & 0x6u) != 0x6u) {
                    f.avx = 0;
                }
            }
            if (max_leaf >= 7) {
                minlz_cpuid(7, 0, r7);
                f.bmi1 = (r7[1] & (1 << 3)) != 0;
                f.bmi2 = (r7[1] & (1 << 8)) != 0;
                f.erms = (r7[1] & (1 << 9)) != 0;
                f.avx2 = f.avx && ((r7[1] & (1 << 5)) != 0);
                f.avx512f = (r7[1] & (1 << 16)) != 0;
                f.avx512bw = (r7[1] & (1 << 30)) != 0;
                f.avx512vl = ((unsigned)r7[1] & (1u << 31)) != 0;
                if ((xcr0 & 0xe6u) != 0xe6u) {
                    f.avx512f = 0;
                    f.avx512bw = 0;
                    f.avx512vl = 0;
                }
            }
        }
#endif
        g_features = f;
        g_features_ready = 1;
    }
    return &g_features;
}

const char *minlz_backend_name(minlz_backend backend) {
    switch (backend) {
    case MINLZ_BACKEND_SSE42:
        return "sse42";
    case MINLZ_BACKEND_AVX2:
        return "avx2";
    case MINLZ_BACKEND_AVX512:
        return "avx512";
    case MINLZ_BACKEND_ASM_AMD64:
        return "asm_amd64";
    case MINLZ_BACKEND_PORTABLE:
    default:
        return "portable";
    }
}

static int minlz_backend_supported(minlz_backend backend, const minlz_cpu_features *f) {
    switch (backend) {
    case MINLZ_BACKEND_SSE42:
        return f->sse42;
    case MINLZ_BACKEND_AVX2:
#if defined(MINLZ_HAVE_AVX2) && MINLZ_HAVE_AVX2
        return f->avx2 && f->bmi1 && f->bmi2;
#else
        return 0;
#endif
    case MINLZ_BACKEND_AVX512:
#if defined(MINLZ_HAVE_AVX512) && MINLZ_HAVE_AVX512
        return f->avx512f && f->avx512bw && f->avx512vl;
#else
        return 0;
#endif
    case MINLZ_BACKEND_ASM_AMD64:
#if defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
        return MINLZ_CPU_X86_64 && f->x86;
#else
        return 0;
#endif
    case MINLZ_BACKEND_PORTABLE:
    default:
        return 1;
    }
}

static minlz_backend minlz_forced_backend(const char *force) {
    if (!force || force[0] == '\0') {
        return (minlz_backend)-1;
    }
    if (strcmp(force, "portable") == 0) {
        return MINLZ_BACKEND_PORTABLE;
    }
    if (strcmp(force, "sse42") == 0 || strcmp(force, "sse4.2") == 0) {
        return MINLZ_BACKEND_SSE42;
    }
    if (strcmp(force, "avx2") == 0 || strcmp(force, "avx2+bmi2") == 0) {
        return MINLZ_BACKEND_AVX2;
    }
    if (strcmp(force, "avx512") == 0) {
        return MINLZ_BACKEND_AVX512;
    }
    if (strcmp(force, "asm") == 0 || strcmp(force, "asm_amd64") == 0) {
        return MINLZ_BACKEND_ASM_AMD64;
    }
    return (minlz_backend)-1;
}

minlz_backend minlz_select_backend(void) {
    if (!g_backend_ready) {
        const minlz_cpu_features *f = minlz_cpu_features_get();
        const char *force = getenv("MINLZ_FORCE_CPU");
        minlz_backend forced = minlz_forced_backend(force);
        if ((int)forced >= 0) {
            g_backend = minlz_backend_supported(forced, f) ? forced : MINLZ_BACKEND_PORTABLE;
        } else if (minlz_backend_supported(MINLZ_BACKEND_AVX512, f)) {
            g_backend = MINLZ_BACKEND_AVX512;
        } else if (minlz_backend_supported(MINLZ_BACKEND_AVX2, f)) {
            g_backend = MINLZ_BACKEND_AVX2;
        } else if (minlz_backend_supported(MINLZ_BACKEND_ASM_AMD64, f)) {
            g_backend = MINLZ_BACKEND_ASM_AMD64;
        } else if (minlz_backend_supported(MINLZ_BACKEND_SSE42, f)) {
            g_backend = MINLZ_BACKEND_SSE42;
        } else {
            g_backend = MINLZ_BACKEND_PORTABLE;
        }
        g_backend_ready = 1;
    }
    return g_backend;
}

int minlz_backend_has_sse42(void) {
    minlz_backend b = minlz_select_backend();
    const minlz_cpu_features *f = minlz_cpu_features_get();
    return f->sse42 && (b == MINLZ_BACKEND_SSE42 || b == MINLZ_BACKEND_AVX2 ||
                        b == MINLZ_BACKEND_AVX512 || b == MINLZ_BACKEND_ASM_AMD64);
}

int minlz_backend_has_avx2(void) {
    minlz_backend b = minlz_select_backend();
    return b == MINLZ_BACKEND_AVX2 || b == MINLZ_BACKEND_AVX512;
}

int minlz_backend_has_avx512(void) {
    return minlz_select_backend() == MINLZ_BACKEND_AVX512;
}

int minlz_backend_has_asm_amd64(void) {
#if defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
    minlz_backend b = minlz_select_backend();
    return MINLZ_CPU_X86_64 && minlz_cpu_features_get()->x86 &&
           (b == MINLZ_BACKEND_SSE42 || b == MINLZ_BACKEND_ASM_AMD64 ||
            b == MINLZ_BACKEND_AVX2 ||
            b == MINLZ_BACKEND_AVX512);
#else
    return 0;
#endif
}

int minlz_backend_has_inline_asm_amd64(void) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (!g_inline_asm_ready) {
        minlz_backend b = minlz_select_backend();
        g_inline_asm_enabled = MINLZ_CPU_X86_64 && minlz_cpu_features_get()->x86 &&
                               (b == MINLZ_BACKEND_SSE42 ||
                                b == MINLZ_BACKEND_ASM_AMD64 || b == MINLZ_BACKEND_AVX2 ||
                                b == MINLZ_BACKEND_AVX512);
        g_inline_asm_ready = 1;
    }
    return g_inline_asm_enabled;
#else
    return 0;
#endif
}
