/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"
#include "block_decode_repeat.h"

#include <immintrin.h>

void minlz_copy_match_avx512(uint8_t *out, size_t offset, size_t length) {
    size_t i = 0;
    if (offset < 64) {
        for (; i < length; i++) {
            out[i] = (out - offset)[i];
        }
        return;
    }
    if (length >= 2048u && offset <= length / 8u) {
        minlz_copy_match_repeated(out, offset, length);
        return;
    }
    while (length - i >= 64) {
        __m512i a = _mm512_loadu_si512((const void *)(out + i - offset));
        _mm512_storeu_si512((void *)(out + i), a);
        i += 64;
    }
    if (offset >= 192u && length - i >= 32u) {
        const __mmask64 valid = (UINT64_C(1) << (length - i)) - UINT64_C(1);
        const __m512i v = _mm512_maskz_loadu_epi8(valid, out + i - offset);
        _mm512_mask_storeu_epi8(out + i, valid, v);
        return;
    }
    for (; i < length; i++) {
        out[i] = (out - offset)[i];
    }
}
