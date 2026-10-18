/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#if MINLZ_ENABLE_INTRINSICS && \
    (defined(__x86_64__) || defined(__i386__) || defined(_M_X64) || defined(_M_IX86))
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <nmmintrin.h>
#endif
#define MINLZ_X86_CRC32C 1
#else
#define MINLZ_X86_CRC32C 0
#endif

static uint32_t crc_table[256];
static int crc_table_ready;

static void minlz_crc32c_init(void) {
    uint32_t i;
    if (crc_table_ready) {
        return;
    }
    for (i = 0; i < 256; i++) {
        uint32_t c = i;
        int k;
        for (k = 0; k < 8; k++) {
            c = (c & 1u) ? (0x82f63b78u ^ (c >> 1)) : (c >> 1);
        }
        crc_table[i] = c;
    }
    crc_table_ready = 1;
}

static uint32_t minlz_crc32c_table(const uint8_t *src, size_t src_len) {
    uint32_t c = 0xffffffffu;
    size_t i;
    minlz_crc32c_init();
    for (i = 0; i < src_len; i++) {
        c = crc_table[(c ^ src[i]) & 0xffu] ^ (c >> 8);
    }
    return c ^ 0xffffffffu;
}

#if MINLZ_X86_CRC32C
#if defined(__GNUC__) || defined(__clang__)
__attribute__((target("sse4.2")))
#endif
static uint32_t minlz_crc32c_hw(const uint8_t *src, size_t src_len) {
    size_t i = 0;
#if defined(__x86_64__) || defined(_M_X64)
    uint64_t c = 0xffffffffu;
    while (((uintptr_t)(src + i) & 7u) != 0 && i < src_len) {
        c = _mm_crc32_u8((uint32_t)c, src[i]);
        i++;
    }
    while (src_len - i >= 8) {
        c = _mm_crc32_u64(c, minlz_load64(src + i));
        i += 8;
    }
    while (i < src_len) {
        c = _mm_crc32_u8((uint32_t)c, src[i]);
        i++;
    }
    return (uint32_t)c ^ 0xffffffffu;
#else
    uint32_t c = 0xffffffffu;
    while (((uintptr_t)(src + i) & 3u) != 0 && i < src_len) {
        c = _mm_crc32_u8(c, src[i]);
        i++;
    }
    while (src_len - i >= 4) {
        c = _mm_crc32_u32(c, minlz_load32(src + i));
        i += 4;
    }
    while (i < src_len) {
        c = _mm_crc32_u8(c, src[i]);
        i++;
    }
    return c ^ 0xffffffffu;
#endif
}

#endif

uint32_t minlz_crc32c(const uint8_t *src, size_t src_len) {
#if MINLZ_X86_CRC32C
    static int use_hw = -1;
    if (use_hw < 0) {
        use_hw = minlz_backend_has_sse42();
    }
    if (use_hw) {
        return minlz_crc32c_hw(src, src_len);
    }
#endif
    return minlz_crc32c_table(src, src_len);
}

uint32_t minlz_mask_crc32c(uint32_t crc) {
    return ((crc >> 15) | (crc << 17)) + 0xa282ead8u;
}

uint32_t minlz_crc(const uint8_t *src, size_t src_len) {
    return minlz_mask_crc32c(minlz_crc32c(src, src_len));
}
