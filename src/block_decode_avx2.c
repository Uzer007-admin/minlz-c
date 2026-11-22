/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"
#include "block_decode_repeat.h"

#include <immintrin.h>

void minlz_copy_match_avx2(uint8_t *out, size_t offset, size_t length) {
    size_t i = 0;
    if (offset < 64) {
        for (; i < length; i++) {
            out[i] = (out - offset)[i];
        }
        return;
    }
    if (length >= 4096u && offset <= length / 8u) {
        minlz_copy_match_repeated(out, offset, length);
        return;
    }
    if (offset == 64u && length >= 96u && length < 512u) {
        const uint8_t *seed = out - 64u;
        const __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)seed);
        const __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(seed + 32u));
        do {
            _mm256_storeu_si256((__m256i *)(void *)(out + i), a);
            _mm256_storeu_si256((__m256i *)(void *)(out + i + 32u), b);
            i += 64u;
        } while (length - i >= 64u);
        size_t rest = length - i;
        if (rest >= 32u) {
            _mm256_storeu_si256((__m256i *)(void *)(out + i), a);
            seed += 32u;
            i += 32u;
            rest -= 32u;
        }
        _mm256_zeroupper();
        if (rest <= 4u) {
            for (size_t k = 0; k < rest; k++) {
                out[i + k] = seed[k];
            }
        } else {
            memcpy(out + i, seed, rest);
        }
        return;
    }
    if (offset == 128u && length >= 128u && length < 512u) {
        const uint8_t *seed = out - 128u;
        const __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)seed);
        const __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(seed + 32u));
        const __m256i c = _mm256_loadu_si256((const __m256i *)(const void *)(seed + 64u));
        const __m256i d = _mm256_loadu_si256((const __m256i *)(const void *)(seed + 96u));
        do {
            _mm256_storeu_si256((__m256i *)(void *)(out + i), a);
            _mm256_storeu_si256((__m256i *)(void *)(out + i + 32u), b);
            _mm256_storeu_si256((__m256i *)(void *)(out + i + 64u), c);
            _mm256_storeu_si256((__m256i *)(void *)(out + i + 96u), d);
            i += 128u;
        } while (length - i >= 128u);
        const size_t rest = length - i;
        if (rest >= 32u) {
            _mm256_storeu_si256((__m256i *)(void *)(out + i), a);
        }
        if (rest >= 64u) {
            _mm256_storeu_si256((__m256i *)(void *)(out + i + 32u), b);
        }
        if (rest >= 96u) {
            _mm256_storeu_si256((__m256i *)(void *)(out + i + 64u), c);
        }
        _mm256_zeroupper();
        const size_t bulk = rest & ~(size_t)31u;
        if (bulk != rest) {
            memcpy(out + i + bulk, seed + bulk, rest - bulk);
        }
        return;
    }
    while (length - i >= 64) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(out + i - offset));
        __m256i b = _mm256_loadu_si256((const __m256i *)(const void *)(out + i + 32 - offset));
        _mm256_storeu_si256((__m256i *)(void *)(out + i), a);
        _mm256_storeu_si256((__m256i *)(void *)(out + i + 32), b);
        i += 64;
    }
    while (length - i >= 32) {
        __m256i a = _mm256_loadu_si256((const __m256i *)(const void *)(out + i - offset));
        _mm256_storeu_si256((__m256i *)(void *)(out + i), a);
        i += 32;
    }
    if (offset >= 192u && length >= 32u && length - i >= 16u) {
        /* The end of the loaded vector is already in decoded history. */
        uint8_t *tail = out + length - 32u;
        const __m256i v = _mm256_loadu_si256((const __m256i *)(const void *)(tail - offset));
        _mm256_storeu_si256((__m256i *)(void *)tail, v);
        _mm256_zeroupper();
        return;
    }
    if (length - i >= 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(out + i - offset));
        _mm_storeu_si128((__m128i *)(void *)(out + i), a);
        i += 16;
    }
    for (; i < length; i++) {
        out[i] = (out - offset)[i];
    }
    _mm256_zeroupper();
}
