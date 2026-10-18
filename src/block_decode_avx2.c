/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <immintrin.h>

void minlz_copy_match_avx2(uint8_t *out, size_t offset, size_t length) {
    size_t i = 0;
    if (offset < 32) {
        for (; i < length; i++) {
            out[i] = out[i - offset];
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
    if (length - i >= 16) {
        __m128i a = _mm_loadu_si128((const __m128i *)(const void *)(out + i - offset));
        _mm_storeu_si128((__m128i *)(void *)(out + i), a);
        i += 16;
    }
    for (; i < length; i++) {
        out[i] = out[i - offset];
    }
    _mm256_zeroupper();
}
