/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <immintrin.h>

void minlz_copy_match_avx512(uint8_t *out, size_t offset, size_t length) {
    size_t i = 0;
    if (offset < 64) {
        for (; i < length; i++) {
            out[i] = out[i - offset];
        }
        return;
    }
    while (length - i >= 64) {
        __m512i a = _mm512_loadu_si512((const void *)(out + i - offset));
        _mm512_storeu_si512((void *)(out + i), a);
        i += 64;
    }
    for (; i < length; i++) {
        out[i] = out[i - offset];
    }
}
