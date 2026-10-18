/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz.h"
#include "minlz_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

typedef struct test_mem_read {
    const uint8_t *data;
    size_t len;
    size_t pos;
    size_t read_at_calls;
    size_t read_at_bytes;
} test_mem_read;

typedef struct test_mem_write {
    uint8_t *data;
    size_t len;
    size_t cap;
} test_mem_write;

static minlz_status test_read_cb(void *ctx, uint8_t *dst, size_t cap, size_t *nread) {
    test_mem_read *r = (test_mem_read *)ctx;
    size_t n = r->len - r->pos;
    if (n > cap) {
        n = cap;
    }
    if (n) {
        memcpy(dst, r->data + r->pos, n);
    }
    r->pos += n;
    *nread = n;
    return MINLZ_OK;
}

static minlz_status test_seek_cb(void *ctx, int64_t offset, int whence, int64_t *new_pos) {
    test_mem_read *r = (test_mem_read *)ctx;
    int64_t base;
    int64_t pos;
    switch (whence) {
    case MINLZ_SEEK_SET:
        base = 0;
        break;
    case MINLZ_SEEK_CUR:
        base = (int64_t)r->pos;
        break;
    case MINLZ_SEEK_END:
        base = (int64_t)r->len;
        break;
    default:
        return MINLZ_UNSUPPORTED;
    }
    pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)r->len) {
        return MINLZ_CORRUPT;
    }
    r->pos = (size_t)pos;
    if (new_pos) {
        *new_pos = pos;
    }
    return MINLZ_OK;
}

static minlz_status test_read_at_cb(void *ctx, int64_t offset,
                                    uint8_t *dst, size_t len,
                                    size_t *nread) {
    test_mem_read *r = (test_mem_read *)ctx;
    size_t pos;
    size_t n;
    if (offset < 0 || (uint64_t)offset > (uint64_t)r->len) {
        return MINLZ_CORRUPT;
    }
    pos = (size_t)offset;
    n = r->len - pos;
    if (n > len) {
        n = len;
    }
    memcpy(dst, r->data + pos, n);
    r->read_at_calls++;
    r->read_at_bytes += n;
    *nread = n;
    return MINLZ_OK;
}

static minlz_status test_write_cb(void *ctx, const uint8_t *src, size_t len) {
    test_mem_write *w = (test_mem_write *)ctx;
    if (w->len + len > w->cap) {
        size_t next = w->cap ? w->cap * 2 : 1024;
        uint8_t *p;
        while (next < w->len + len) {
            next *= 2;
        }
        p = (uint8_t *)realloc(w->data, next);
        if (!p) {
            return MINLZ_IO;
        }
        w->data = p;
        w->cap = next;
    }
    if (len) {
        memcpy(w->data + w->len, src, len);
        w->len += len;
    }
    return MINLZ_OK;
}

static int test_varint_crc(void) {
    uint8_t tmp[16];
    size_t value = 0;
    size_t used = 0;
    size_t n = minlz_put_uvarint(tmp, 300);
    CHECK(n == 2);
    CHECK(minlz_read_uvarint(tmp, n, &value, &used) == MINLZ_OK);
    CHECK(value == 300 && used == 2);
    CHECK(minlz_crc32c((const uint8_t *)"123456789", 9) == 0xe3069283u);
    CHECK(minlz_crc((const uint8_t *)"abc", 3) == minlz_mask_crc32c(minlz_crc32c((const uint8_t *)"abc", 3)));
    return 0;
}

static int test_backend_probe(void) {
    minlz_backend backend = minlz_select_backend();
    const minlz_cpu_features *features = minlz_cpu_features_get();
    const char *name = minlz_backend_name(backend);
    CHECK(name != NULL);
    CHECK(backend >= MINLZ_BACKEND_PORTABLE && backend <= MINLZ_BACKEND_ASM_AMD64);
    if (backend == MINLZ_BACKEND_SSE42) {
        CHECK(features->sse42 != 0);
    } else if (backend == MINLZ_BACKEND_AVX2) {
        CHECK(features->avx2 != 0 && features->bmi1 != 0 && features->bmi2 != 0);
    } else if (backend == MINLZ_BACKEND_AVX512) {
        CHECK(features->avx512f != 0 && features->avx512bw != 0 && features->avx512vl != 0);
    } else if (backend == MINLZ_BACKEND_ASM_AMD64) {
        CHECK(features->x86 != 0);
    }
    return 0;
}

static int roundtrip(const uint8_t *src, size_t src_len, int level) {
    size_t cap = minlz_max_encoded_len(src_len);
    uint8_t *enc = (uint8_t *)malloc(cap ? cap : 1);
    uint8_t *dec = NULL;
    minlz_workspace *ws = NULL;
    size_t enc_len = 0;
    size_t dec_len = 0;
    minlz_status st;
    int ret = 1;
    CHECK(enc != NULL);
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    st = minlz_encode_ws(ws, enc, cap, &enc_len, src, src_len, level);
    CHECK(st == MINLZ_OK);
    st = minlz_decoded_len(enc, enc_len, &dec_len);
    CHECK(st == MINLZ_OK);
    CHECK(dec_len == src_len);
    dec = (uint8_t *)malloc(dec_len ? dec_len : 1);
    CHECK(dec != NULL);
    st = minlz_decode_ws(ws, dec, dec_len, &dec_len, enc, enc_len);
    CHECK(st == MINLZ_OK);
    CHECK(dec_len == src_len);
    CHECK(memcmp(dec, src, src_len) == 0);
    ret = 0;
    free(enc);
    free(dec);
    minlz_workspace_destroy(ws);
    return ret;
}

static int test_roundtrips(void) {
    uint8_t data[32768];
    uint32_t x = 1;
    size_t i;
    int levels[] = {
        MINLZ_LEVEL_SUPERFAST,
        MINLZ_LEVEL_UNCOMPRESSED,
        MINLZ_LEVEL_FASTEST,
        MINLZ_LEVEL_BALANCED,
        MINLZ_LEVEL_SMALLEST
    };
    for (i = 0; i < sizeof(data); i++) {
        x = x * 1664525u + 1013904223u;
        data[i] = (uint8_t)(x >> 24);
    }
    CHECK(roundtrip((const uint8_t *)"", 0, MINLZ_LEVEL_BALANCED) == 0);
    CHECK(roundtrip((const uint8_t *)"hello", 5, MINLZ_LEVEL_BALANCED) == 0);
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        CHECK(roundtrip(data, sizeof(data), levels[i]) == 0);
    }
    memset(data, 'A', sizeof(data));
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        CHECK(roundtrip(data, sizeof(data), levels[i]) == 0);
    }
    return 0;
}

static int test_balanced_16k_hash_loop(void) {
    static const size_t sizes[] = {8192u, 10000u, 16384u};
    uint8_t data[16384];
    uint32_t seed;
    size_t i;
    size_t j;
    for (seed = 1; seed <= 8; seed++) {
        uint32_t x = seed;
        for (i = 0; i < sizeof(data); i++) {
            x = x * 1664525u + 1013904223u;
            data[i] = (i >= 256u && (i & 255u) < 64u) ?
                data[i - 256u] : (uint8_t)(x >> 24);
        }
        for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
            CHECK(roundtrip(data, sizes[j], MINLZ_LEVEL_BALANCED) == 0);
        }
    }
    return 0;
}

static int test_max_block_roundtrip(void) {
    uint8_t *data = (uint8_t *)malloc(MINLZ_MAX_BLOCK_SIZE);
    int ret = 1;
    size_t i;
    CHECK(data != NULL);
    {
        const char pattern[] = "MinLZ-fast-path-regression:";
        for (i = 0; i < MINLZ_MAX_BLOCK_SIZE; i++) {
            data[i] = (uint8_t)(pattern[i % (sizeof(pattern) - 1)] + (uint8_t)(i >> 19));
        }
    }
    CHECK(roundtrip(data, MINLZ_MAX_BLOCK_SIZE, MINLZ_LEVEL_FASTEST) == 0);
    CHECK(roundtrip(data, MINLZ_MAX_BLOCK_SIZE, MINLZ_LEVEL_BALANCED) == 0);
    ret = 0;
    free(data);
    return ret;
}

static int test_long_match_encode(void) {
    static const char pattern[] = "abcdefghij";
    static const size_t sizes[] = {60000u, (512u << 10) + 1u, 1u << 20, 2u << 20};
    static const int levels[] = {
        MINLZ_LEVEL_SUPERFAST, MINLZ_LEVEL_FASTEST,
        MINLZ_LEVEL_BALANCED, MINLZ_LEVEL_SMALLEST
    };
    const size_t max_size = sizes[sizeof(sizes) / sizeof(sizes[0]) - 1u];
    uint8_t *src = (uint8_t *)malloc(max_size);
    uint8_t *encoded = (uint8_t *)malloc(minlz_max_encoded_len(max_size));
    uint8_t *decoded = (uint8_t *)malloc(max_size);
    minlz_workspace *ws = NULL;
    size_t i;
    size_t j;
    CHECK(src && encoded && decoded);
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    for (i = 0; i < max_size; i++) {
        src[i] = (uint8_t)pattern[i % (sizeof(pattern) - 1u)];
    }
    for (i = 0; i < sizeof(sizes) / sizeof(sizes[0]); i++) {
        for (j = 0; j < sizeof(levels) / sizeof(levels[0]); j++) {
            size_t encoded_len = 0;
            size_t decoded_len = 0;
            CHECK(minlz_encode(encoded, minlz_max_encoded_len(sizes[i]),
                               &encoded_len, src, sizes[i], levels[j]) == MINLZ_OK);
            if (encoded_len > 1024u) {
                fprintf(stderr, "long match: level=%d size=%zu encoded=%zu\n",
                        levels[j], sizes[i], encoded_len);
            }
            CHECK(encoded_len <= 1024u);
            CHECK(minlz_decode(decoded, sizes[i], &decoded_len,
                               encoded, encoded_len) == MINLZ_OK);
            CHECK(decoded_len == sizes[i]);
            CHECK(memcmp(decoded, src, sizes[i]) == 0);
            CHECK(minlz_encode_ws(ws, encoded, minlz_max_encoded_len(sizes[i]),
                                  &encoded_len, src, sizes[i], levels[j]) == MINLZ_OK);
            CHECK(encoded_len <= 1024u);
            CHECK(minlz_try_encode_ws(ws, encoded, minlz_max_encoded_len(sizes[i]),
                                      &encoded_len, src, sizes[i], levels[j]) == MINLZ_OK);
            CHECK(encoded_len <= 1024u);
            CHECK(minlz_decode_ws(ws, decoded, sizes[i], &decoded_len,
                                  encoded, encoded_len) == MINLZ_OK);
            CHECK(decoded_len == sizes[i]);
            CHECK(memcmp(decoded, src, sizes[i]) == 0);
        }
    }
    minlz_workspace_destroy(ws);
    free(decoded);
    free(encoded);
    free(src);
    return 0;
}

static int test_short_encode_buffer(void) {
    static const char pattern[] = "abcdefghij";
    static const int levels[] = {
        MINLZ_LEVEL_SUPERFAST, MINLZ_LEVEL_FASTEST,
        MINLZ_LEVEL_BALANCED, MINLZ_LEVEL_SMALLEST
    };
    uint8_t *src = (uint8_t *)malloc(60000u);
    uint8_t *decoded = (uint8_t *)malloc(60000u);
    uint8_t encoded[64];
    size_t i;
    size_t j;
    CHECK(src && decoded);
    for (i = 0; i < 60000u; i++) {
        src[i] = (uint8_t)pattern[i % (sizeof(pattern) - 1u)];
    }
    for (i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
        size_t encoded_len = 0;
        size_t decoded_len = 0;
        memset(encoded, 0xa5, sizeof(encoded));
        CHECK(minlz_encode(encoded, 12u, &encoded_len,
                           src, 60000u, levels[i]) == MINLZ_DST_TOO_SMALL);
        CHECK(encoded_len > 12u && encoded_len <= sizeof(encoded));
        for (j = 12u; j < sizeof(encoded); j++) {
            CHECK(encoded[j] == 0xa5);
        }
        memset(encoded, 0xa5, sizeof(encoded));
        CHECK(minlz_try_encode(encoded, 12u, &encoded_len,
                               src, 60000u, levels[i]) == MINLZ_DST_TOO_SMALL);
        CHECK(encoded_len > 12u && encoded_len <= sizeof(encoded));
        for (j = 12u; j < sizeof(encoded); j++) {
            CHECK(encoded[j] == 0xa5);
        }
        CHECK(minlz_encode(encoded, sizeof(encoded), &encoded_len,
                           src, 60000u, levels[i]) == MINLZ_OK);
        CHECK(minlz_decode(decoded, 60000u, &decoded_len,
                           encoded, encoded_len) == MINLZ_OK);
        CHECK(decoded_len == 60000u);
        CHECK(memcmp(decoded, src, 60000u) == 0);
    }
    free(decoded);
    free(src);
    return 0;
}

static int test_balanced_mixed_runs(void) {
    static const size_t run_lengths[] = {64u << 10, 256u << 10};
    static const char text[] = "lorem ipsum dolor sit amet consectetur ";
    const size_t input_len = 4u << 20;
    uint8_t *src = (uint8_t *)malloc(input_len);
    uint8_t *encoded = (uint8_t *)malloc(minlz_max_encoded_len(input_len));
    uint8_t *decoded = (uint8_t *)malloc(input_len);
    size_t case_idx;
    CHECK(src && encoded && decoded);
    for (case_idx = 0; case_idx < sizeof(run_lengths) / sizeof(run_lengths[0]); case_idx++) {
        uint32_t state = 1u;
        size_t pos;
        size_t encoded_len = 0;
        size_t decoded_len = 0;
        for (pos = 0; pos < input_len; pos++) {
            if ((pos / run_lengths[case_idx]) & 1u) {
                src[pos] = (uint8_t)text[pos % (sizeof(text) - 1u)];
            } else {
                state = state * 1664525u + 1013904223u;
                src[pos] = (uint8_t)(state >> 24);
            }
        }
        CHECK(minlz_encode(encoded, minlz_max_encoded_len(input_len),
                           &encoded_len, src, input_len,
                           MINLZ_LEVEL_BALANCED) == MINLZ_OK);
        if (encoded_len >= (3u << 20)) {
            fprintf(stderr, "mixed runs: run=%zu encoded=%zu\n",
                    run_lengths[case_idx], encoded_len);
        }
        CHECK(encoded_len < (3u << 20));
        CHECK(minlz_decode(decoded, input_len, &decoded_len,
                           encoded, encoded_len) == MINLZ_OK);
        CHECK(decoded_len == input_len);
        CHECK(memcmp(decoded, src, input_len) == 0);
    }
    free(decoded);
    free(encoded);
    free(src);
    return 0;
}

static int test_balanced_2mb_global_asm(void) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    extern size_t minlz_encode_better_block_2mb_goasm(
        uint8_t *dst, size_t dst_cap, const uint8_t *src, size_t src_len,
        uint8_t *tmp);
    const size_t input_len = 1000000u;
    const size_t cap = minlz_max_encoded_len(input_len);
    minlz_workspace *ws = NULL, *reference_ws = NULL;
    uint8_t *src;
    uint8_t *block;
    uint8_t *reference;
    uint8_t *decoded;
    uint8_t *scratch = NULL;
    uint8_t *seed;
    FILE *seed_file;
    long seed_len;
    size_t pos = 0;
    size_t block_len;
    size_t reference_len;
    size_t decoded_len = 0;
    unsigned line = 0;
    size_t h;

    if (!minlz_backend_has_inline_asm_amd64()) {
        return 0;
    }
    src = (uint8_t *)malloc(input_len);
    block = (uint8_t *)malloc(cap);
    reference = (uint8_t *)malloc(cap);
    decoded = (uint8_t *)malloc(input_len);
    CHECK(src && block && reference && decoded);
    while (pos < input_len) {
        char row[192];
        int n = snprintf(row, sizeof(row),
                         "{\"ts\":\"2026-05-07T12:%02u:%02uZ\",\"level\":\"info\","
                         "\"svc\":\"minlz\",\"msg\":\"block processed\","
                         "\"block\":%u,\"bytes\":%u,\"crc\":\"%08x\"}\n",
                         line % 60u, (line / 60u) % 60u, line,
                         4096u + (line & 8191u), line * 2654435761u);
        CHECK(n > 0 && (size_t)n < sizeof(row));
        if ((size_t)n > input_len - pos) {
            n = (int)(input_len - pos);
        }
        memcpy(src + pos, row, (size_t)n);
        pos += (size_t)n;
        line++;
    }
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_workspace_create(&reference_ws) == MINLZ_OK);
    CHECK(minlz_workspace_prepare_buffer(reference_ws, 2, 589824u, &scratch) == MINLZ_OK);
    block[0] = 0;
    h = minlz_put_uvarint(block + 1u, input_len);
    block_len = minlz_encode_better_block_2mb_goasm(
        block + 1u + h, cap - 1u - h, src, input_len, scratch);
    CHECK(block_len != 0);
    block_len += 1u + h;
    CHECK(block_len == 180160u);
    CHECK(minlz_encode_ws(ws, reference, cap, &reference_len,
                          src, input_len, MINLZ_LEVEL_BALANCED) == MINLZ_OK);
    CHECK(reference_len <= block_len);
    if (reference_len == block_len) {
        CHECK(memcmp(block, reference, block_len) == 0);
    }
    CHECK(minlz_decode_ws(ws, decoded, input_len, &decoded_len,
                          block, block_len) == MINLZ_OK);
    CHECK(decoded_len == input_len && memcmp(decoded, src, input_len) == 0);

    seed_file = fopen("testdata/Mark.Twain-Tom.Sawyer.txt", "rb");
    if (!seed_file) {
        seed_file = fopen("../testdata/Mark.Twain-Tom.Sawyer.txt", "rb");
    }
    CHECK(seed_file != NULL);
    CHECK(fseek(seed_file, 0, SEEK_END) == 0);
    seed_len = ftell(seed_file);
    CHECK(seed_len > 0 && (size_t)seed_len <= input_len);
    CHECK(fseek(seed_file, 0, SEEK_SET) == 0);
    seed = (uint8_t *)malloc((size_t)seed_len);
    CHECK(seed != NULL);
    CHECK(fread(seed, 1, (size_t)seed_len, seed_file) == (size_t)seed_len);
    CHECK(fclose(seed_file) == 0);
    for (pos = 0; pos < input_len; pos++) {
        src[pos] = (uint8_t)(seed[pos % (size_t)seed_len] ^
                             (uint8_t)(pos / (size_t)seed_len));
    }
    h = minlz_put_uvarint(block + 1u, input_len);
    block_len = minlz_encode_better_block_2mb_goasm(
        block + 1u + h, cap - 1u - h, src, input_len, scratch);
    CHECK(block_len != 0);
    block_len += 1u + h;
    CHECK(block_len == 652201u);
    CHECK(minlz_encode_ws(ws, reference, cap, &reference_len,
                          src, input_len, MINLZ_LEVEL_BALANCED) == MINLZ_OK);
    CHECK(reference_len <= block_len);
    if (reference_len == block_len) {
        CHECK(memcmp(block, reference, block_len) == 0);
    }
    decoded_len = 0;
    CHECK(minlz_decode_ws(ws, decoded, input_len, &decoded_len,
                          block, block_len) == MINLZ_OK);
    CHECK(decoded_len == input_len && memcmp(decoded, src, input_len) == 0);
    free(seed);
    minlz_workspace_destroy(reference_ws);
    minlz_workspace_destroy(ws);
    free(decoded);
    free(reference);
    free(block);
    free(src);
#endif
    return 0;
}

static int test_balanced_2mb_reference_boundaries(void) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    extern size_t minlz_encode_better_block_2mb_goasm(
        uint8_t *dst, size_t dst_cap, const uint8_t *src, size_t src_len,
        uint8_t *tmp);
#endif
    static const size_t sizes[] = {
        (512u << 10) + 1u, (512u << 10) + 32u, 1u << 20, 2u << 20
    };
    const size_t max_len = sizes[3];
    const size_t cap = minlz_max_encoded_len(max_len);
    minlz_workspace *ws = NULL, *reference_ws = NULL;
    uint8_t *src, *reference = NULL, *block, *decoded, *scratch = NULL;
    uint32_t seed;
    size_t i, j;
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    int compare_reference = minlz_backend_has_inline_asm_amd64();
#else
    int compare_reference = 0;
#endif
    src = (uint8_t *)malloc(max_len);
    block = (uint8_t *)malloc(cap);
    decoded = (uint8_t *)malloc(max_len);
    CHECK(src && block && decoded);
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    if (compare_reference) {
        reference = (uint8_t *)malloc(cap);
        CHECK(reference != NULL);
        CHECK(minlz_workspace_create(&reference_ws) == MINLZ_OK);
        CHECK(minlz_workspace_prepare_buffer(reference_ws, 2, 589824u, &scratch) == MINLZ_OK);
    }
    for (seed = 1; seed <= 3; seed++) {
        uint32_t x = seed;
        /* High-bit literals and short matches exercise both hash widths. */
        for (i = 0; i < max_len; i++) {
            x = x * 1664525u + 1013904223u;
            src[i] = (i >= 8192u && (i & 255u) >= 96u) ?
                src[i - ((i & 8u) ? 4099u : 257u)] : (uint8_t)(x >> 24);
        }
        for (j = 0; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
            size_t block_len = 0, decoded_len = 0;
            CHECK(minlz_encode_ws(ws, block, cap, &block_len, src, sizes[j],
                                  MINLZ_LEVEL_BALANCED) == MINLZ_OK);
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
            if (compare_reference) {
                size_t h, ref_len;
                reference[0] = 0;
                h = 1u + minlz_put_uvarint(reference + 1u, sizes[j]);
                ref_len = minlz_encode_better_block_2mb_goasm(
                    reference + h, cap - h, src, sizes[j], scratch);
                CHECK(ref_len != 0);
                ref_len += h;
                CHECK(block_len == ref_len && memcmp(block, reference, ref_len) == 0);
            }
#endif
            CHECK(minlz_decode_ws(ws, decoded, max_len, &decoded_len,
                                  block, block_len) == MINLZ_OK);
            CHECK(decoded_len == sizes[j] && memcmp(decoded, src, sizes[j]) == 0);
        }
    }
    minlz_workspace_destroy(reference_ws);
    minlz_workspace_destroy(ws);
    free(decoded);
    free(block);
    free(reference);
    free(src);
    return 0;
}

static int test_workspace_scratch_reuse(void) {
    const size_t cap = 589824u;
    minlz_workspace *ws = NULL;
    uint8_t *scratch = NULL;
    uintptr_t first;
    unsigned i;
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, cap, &scratch) == MINLZ_OK);
    CHECK(scratch != NULL && ((uintptr_t)scratch & 63u) == 0u);
    first = (uintptr_t)scratch;
    scratch[0] = 17u;
    scratch[cap - 1u] = 29u;
    for (i = 0; i < 2u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, cap, &scratch) == MINLZ_OK);
        CHECK((uintptr_t)scratch == first);
        CHECK(scratch[0] == 17u && scratch[cap - 1u] == 29u);
        CHECK(ws->encode_scratch_hint == 0u);
    }
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, cap, &scratch) == MINLZ_OK);
    if (ws->encode_scratch_hint == 2u) {
        CHECK(((uintptr_t)scratch & ((2u << 20) - 1u)) == 0u);
        CHECK(scratch[0] == 0u && scratch[cap - 1u] == 0u);
    } else {
        CHECK((uintptr_t)scratch == first);
        CHECK(scratch[0] == 17u && scratch[cap - 1u] == 29u);
    }
    first = (uintptr_t)scratch;
    for (i = 0; i < 8u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, cap, &scratch) == MINLZ_OK);
        CHECK((uintptr_t)scratch == first);
    }
    minlz_workspace_reset(ws);
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, cap, &scratch) == MINLZ_OK);
    CHECK((uintptr_t)scratch == first);
    CHECK(minlz_workspace_prepare_buffer(ws, 2u, cap + 1u, &scratch) == MINLZ_OK);
    CHECK(ws->encode_scratch_uses == 0u && ws->encode_scratch_hint == 0u);
    CHECK(((uintptr_t)scratch & 63u) == 0u);
    CHECK(scratch[0] == 0u && scratch[cap] == 0u);
    CHECK(minlz_workspace_prepare_buffer(ws, 0u, SIZE_MAX, &scratch) == MINLZ_IO);
    CHECK(ws->buffers[0] == NULL && ws->buffer_caps[0] == 0u);
    minlz_workspace_destroy(ws);
    return 0;
}

static int decode_expect(const uint8_t *block, size_t block_len,
                         const uint8_t *want, size_t want_len) {
    uint8_t *got = (uint8_t *)malloc(want_len ? want_len : 1);
    size_t got_len = 0;
    minlz_status st = minlz_decode(got, want_len, &got_len, block, block_len);
    CHECK(st == MINLZ_OK);
    CHECK(got_len == want_len);
    CHECK(memcmp(got, want, want_len) == 0);
    free(got);
    return 0;
}

static int test_superfast_small_repeat_size(void) {
    static const size_t sizes[] = {64u, 128u, 256u, 512u, 1024u};
    static const size_t go_sizes[] = {22u, 23u, 23u, 25u, 25u};
    uint8_t input[1024], encoded[1100], decoded[1024];
    size_t i, j, encoded_len, decoded_len;
    for (i = 0u; i < sizeof(input); i++) input[i] = (uint8_t)"0123456789ABCDEF"[i % 16u];
    for (j = 0u; j < sizeof(sizes) / sizeof(sizes[0]); j++) {
        CHECK(minlz_encode(encoded, sizeof(encoded), &encoded_len, input, sizes[j], MINLZ_LEVEL_SUPERFAST) == MINLZ_OK);
        CHECK(encoded_len <= go_sizes[j]);
        CHECK(minlz_decode(decoded, sizeof(decoded), &decoded_len, encoded, encoded_len) == MINLZ_OK);
        CHECK(decoded_len == sizes[j] && memcmp(decoded, input, sizes[j]) == 0);
    }
    return 0;
}

static int test_exact_tiny_matches(void) {
    const size_t offsets[] = {8u, 16u, 31u, 64u};
    size_t length, a, repeat, tag_kind;
    for (length = 4u; length <= 8u; length++) {
        for (a = 0; a < sizeof(offsets) / sizeof(offsets[0]); a++) {
            for (repeat = 0u; repeat <= 1u; repeat++) {
                for (tag_kind = 1u; tag_kind <= 2u; tag_kind++) {
                    /* A four-byte Copy2 without the repeat is larger than raw input. */
                    if (tag_kind == 2u && (offsets[a] < 64u || (length == 4u && repeat == 0u))) continue;
                    /* Repeat the short tag to enter the 256..8192-byte tiny decoder. */
                    const size_t copies = 80u;
                    uint8_t block[512], want[1400], guarded[1464];
                    const size_t offset = offsets[a];
                    const size_t want_len = 64u + copies * length * (repeat + 1u);
                    size_t p = 0u, i, copy, got_len = 0u;
                    uint16_t copy_tag;
                    block[p++] = 0;
                    p += minlz_put_uvarint(block + p, want_len);
                    block[p++] = (uint8_t)(29u << 3);
                    block[p++] = 34u;
                    for (i = 0u; i < 64u; i++) {
                        want[i] = block[p++] = (uint8_t)(i * 173u + 11u);
                    }
                    for (copy = 0u; copy < copies; copy++) {
                        if (tag_kind == 1u) {
                            copy_tag = (uint16_t)(((offset - 1u) << 6) |
                                                 ((length - 4u) << 2) | MINLZ_TAG_COPY1);
                            block[p++] = (uint8_t)copy_tag;
                            block[p++] = (uint8_t)(copy_tag >> 8);
                        } else {
                            block[p++] = (uint8_t)(((length - 4u) << 2) | MINLZ_TAG_COPY2);
                            copy_tag = (uint16_t)(offset - 64u);
                            block[p++] = (uint8_t)copy_tag;
                            block[p++] = (uint8_t)(copy_tag >> 8);
                        }
                        if (repeat) {
                            block[p++] = (uint8_t)(((length - 1u) << 3) | MINLZ_TAG_REPEAT);
                        }
                    }
                    for (i = 64u; i < want_len; i++) {
                        want[i] = want[i - offset];
                    }
                    memset(guarded, 0xa5, sizeof(guarded));
                    CHECK(minlz_decode(guarded + 32u, want_len, &got_len, block, p) == MINLZ_OK);
                    CHECK(got_len == want_len && memcmp(guarded + 32u, want, want_len) == 0);
                    for (i = 0u; i < 32u; i++) {
                        CHECK(guarded[i] == 0xa5 && guarded[32u + want_len + i] == 0xa5);
                    }
                    CHECK(minlz_decode(guarded + 32u, want_len - 1u, &got_len, block, p) != MINLZ_OK);
                    CHECK(minlz_decode(guarded + 32u, want_len, &got_len, block, p - 1u) != MINLZ_OK);
                }
            }
        }
    }
    return 0;
}

static int test_manual_tags(void) {
    uint8_t copy1[] = {0, 8, 24, 'a', 'b', 'c', 'd', 0xc1, 0x00};
    uint8_t want1[] = {'a', 'b', 'c', 'd', 'a', 'b', 'c', 'd'};
    uint8_t repeat[] = {0, 6, 0, 'x', 0x24};
    uint8_t want_rep[] = {'x', 'x', 'x', 'x', 'x', 'x'};
    uint8_t copy2[1 + 1 + 2 + 64 + 3];
    uint8_t want2[80];
    uint8_t fused[1 + 1 + 2 + 64 + 4];
    uint8_t want_fused[73];
    uint8_t *copy3;
    uint8_t *want3;
    size_t p;
    size_t i;

    CHECK(decode_expect(copy1, sizeof(copy1), want1, sizeof(want1)) == 0);
    CHECK(decode_expect(repeat, sizeof(repeat), want_rep, sizeof(want_rep)) == 0);

    p = 0;
    copy2[p++] = 0;
    p += minlz_put_uvarint(copy2 + p, sizeof(want2));
    copy2[p++] = (uint8_t)(29u << 3);
    copy2[p++] = 34;
    for (i = 0; i < 64; i++) {
        copy2[p++] = (uint8_t)i;
        want2[i] = (uint8_t)i;
    }
    copy2[p++] = (uint8_t)((12u << 2) | MINLZ_TAG_COPY2);
    copy2[p++] = 0;
    copy2[p++] = 0;
    memcpy(want2 + 64, want2, 16);
    CHECK(decode_expect(copy2, p, want2, sizeof(want2)) == 0);

    p = 0;
    fused[p++] = 0;
    p += minlz_put_uvarint(fused + p, sizeof(want_fused));
    fused[p++] = (uint8_t)(29u << 3);
    fused[p++] = 34;
    for (i = 0; i < 64; i++) {
        fused[p++] = (uint8_t)i;
        want_fused[i] = (uint8_t)i;
    }
    fused[p++] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (4u << 5));
    fused[p++] = 0;
    fused[p++] = 0;
    fused[p++] = 'Z';
    want_fused[64] = 'Z';
    memcpy(want_fused + 65, want_fused + 1, 8);
    CHECK(decode_expect(fused, p, want_fused, sizeof(want_fused)) == 0);

    want3 = (uint8_t *)malloc(65552);
    copy3 = (uint8_t *)malloc(1 + 4 + 3 + 65536 + 4);
    CHECK(want3 && copy3);
    p = 0;
    copy3[p++] = 0;
    p += minlz_put_uvarint(copy3 + p, 65552);
    copy3[p++] = (uint8_t)(30u << 3);
    copy3[p++] = 0xe2;
    copy3[p++] = 0xff;
    for (i = 0; i < 65536; i++) {
        copy3[p++] = (uint8_t)i;
        want3[i] = (uint8_t)i;
    }
    copy3[p++] = 0x87;
    copy3[p++] = 0x01;
    copy3[p++] = 0x00;
    copy3[p++] = 0x00;
    memcpy(want3 + 65536, want3, 16);
    CHECK(decode_expect(copy3, p, want3, 65552) == 0);
    free(copy3);
    free(want3);
    return 0;
}

static int test_copy3_fast_margin(void) {
    static const size_t lengths[] = {16u, 17u, 59u, 64u, 65u, 69u, 80u};
    static const size_t literal_lengths[] = {0u, 1u, 3u};
    uint8_t *block = (uint8_t *)malloc(65536u + 256u);
    uint8_t *want = (uint8_t *)malloc(65536u + 256u);
    size_t i, j, k;
    CHECK(block && want);

    for (i = 0; i < sizeof(lengths) / sizeof(lengths[0]); i++) {
        for (j = 0; j < sizeof(literal_lengths) / sizeof(literal_lengths[0]); j++) {
            const size_t length = lengths[i];
            const size_t lit_len = literal_lengths[j];
            const size_t want_len = 65536u + lit_len + length + 128u;
            const uint8_t len_code = (uint8_t)(length <= 64u ? length - 4u : 61u);
            const uint32_t tag = 7u | ((uint32_t)lit_len << 3) |
                                 ((uint32_t)len_code << 5);
            size_t p = 0;

            block[p++] = 0;
            p += minlz_put_uvarint(block + p, want_len);
            block[p++] = (uint8_t)(30u << 3);
            block[p++] = 0xe2;
            block[p++] = 0xff;
            for (k = 0; k < 65536u; k++) {
                block[p++] = (uint8_t)k;
                want[k] = (uint8_t)k;
            }
            minlz_store32(block + p, tag);
            p += 4u;
            if (length > 64u) {
                block[p++] = (uint8_t)(length - 64u);
            }
            for (k = 0; k < lit_len; k++) {
                block[p++] = (uint8_t)('a' + k);
                want[65536u + k] = (uint8_t)('a' + k);
            }
            memcpy(want + 65536u + lit_len, want + lit_len, length);
            block[p++] = (uint8_t)(29u << 3);
            block[p++] = 98u;
            for (k = 0; k < 128u; k++) {
                block[p++] = (uint8_t)(255u - k);
                want[65536u + lit_len + length + k] = (uint8_t)(255u - k);
            }
            CHECK(decode_expect(block, p, want, want_len) == 0);
        }
    }
    free(want);
    free(block);
    return 0;
}

static int test_fastest_2mb_natural(void) {
    static const char phrase[] =
        "The river curled past the town while the morning light crossed "
        "the water. A quiet traveler followed the road and watched the "
        "boats return to the landing before the market opened. ";
    enum { seed_len = 8192, raw_len = 1000000 };
    uint8_t seed[seed_len];
    uint8_t *src = (uint8_t *)malloc(raw_len);
    uint8_t *enc = (uint8_t *)malloc(minlz_max_encoded_len(raw_len));
    uint8_t *enc_again = (uint8_t *)malloc(minlz_max_encoded_len(raw_len));
    uint8_t *dec = (uint8_t *)malloc(raw_len);
    minlz_workspace *ws = NULL;
    size_t pos = 0, i, enc_len = 0, enc_again_len = 0, dec_len = 0;
    uint8_t generation = 0;
    CHECK(src && enc && enc_again && dec);

    for (i = 0; i < seed_len; i++) {
        seed[i] = (uint8_t)phrase[i % (sizeof(phrase) - 1u)];
    }
    while (pos < raw_len) {
        const size_t n = raw_len - pos < seed_len ? raw_len - pos : seed_len;
        for (i = 0; i < n; i++) {
            src[pos + i] = seed[i] ^ generation;
        }
        pos += n;
        generation++;
    }
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_encode_ws(ws, enc, minlz_max_encoded_len(raw_len), &enc_len,
                          src, raw_len, MINLZ_LEVEL_FASTEST) == MINLZ_OK);
    CHECK(enc_len < raw_len);
    CHECK(minlz_decode_ws(ws, dec, raw_len, &dec_len, enc, enc_len) == MINLZ_OK);
    CHECK(dec_len == raw_len);
    CHECK(memcmp(dec, src, raw_len) == 0);
    CHECK(minlz_encode_ws(ws, enc, minlz_max_encoded_len(raw_len), &enc_len,
                          src, raw_len, MINLZ_LEVEL_BALANCED) == MINLZ_OK);
    CHECK(enc_len < raw_len);
    CHECK(minlz_encode_ws(ws, enc_again, minlz_max_encoded_len(raw_len),
                          &enc_again_len, src, raw_len, MINLZ_LEVEL_BALANCED) == MINLZ_OK);
    CHECK(enc_again_len == enc_len && memcmp(enc_again, enc, enc_len) == 0);
    CHECK(minlz_decode_ws(ws, dec, raw_len, &dec_len, enc, enc_len) == MINLZ_OK);
    CHECK(dec_len == raw_len && memcmp(dec, src, raw_len) == 0);

    minlz_workspace_destroy(ws);
    free(dec);
    free(enc_again);
    free(enc);
    free(src);
    return 0;
}

static int test_fastest_2mb_structured(void) {
    enum { raw_len = 1000000 };
    uint8_t *src = (uint8_t *)malloc(raw_len);
    uint8_t *enc = (uint8_t *)malloc(minlz_max_encoded_len(raw_len));
    uint8_t *enc_again = (uint8_t *)malloc(minlz_max_encoded_len(raw_len));
    uint8_t *dec = (uint8_t *)malloc(raw_len);
    minlz_workspace *ws = NULL;
    size_t pos = 0, enc_len = 0, enc_again_len = 0, dec_len = 0;
    unsigned line = 0;
    CHECK(src && enc && enc_again && dec);

    while (pos < raw_len) {
        char text[192];
        int len = snprintf(text, sizeof(text),
                           "{\"ts\":\"2026-05-07T12:%02u:%02uZ\",\"level\":\"info\","
                           "\"svc\":\"minlz\",\"msg\":\"block processed\","
                           "\"block\":%u,\"bytes\":%u,\"crc\":\"%08x\"}\n",
                           line % 60u, (line / 60u) % 60u, line,
                           4096u + (line & 8191u), line * 2654435761u);
        size_t n;
        CHECK(len > 0 && (size_t)len < sizeof(text));
        n = (size_t)len < raw_len - pos ? (size_t)len : raw_len - pos;
        memcpy(src + pos, text, n);
        pos += n;
        line++;
    }
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_encode_ws(ws, enc, minlz_max_encoded_len(raw_len), &enc_len,
                          src, raw_len, MINLZ_LEVEL_FASTEST) == MINLZ_OK);
    CHECK(enc_len < raw_len);
    CHECK(minlz_decode_ws(ws, dec, raw_len, &dec_len, enc, enc_len) == MINLZ_OK);
    CHECK(dec_len == raw_len && memcmp(dec, src, raw_len) == 0);
    CHECK(minlz_encode_ws(ws, enc, minlz_max_encoded_len(raw_len), &enc_len,
                          src, raw_len, MINLZ_LEVEL_BALANCED) == MINLZ_OK);
    CHECK(enc_len < raw_len);
    CHECK(minlz_encode_ws(ws, enc_again, minlz_max_encoded_len(raw_len),
                          &enc_again_len, src, raw_len, MINLZ_LEVEL_BALANCED) == MINLZ_OK);
    CHECK(enc_again_len == enc_len && memcmp(enc_again, enc, enc_len) == 0);
    CHECK(minlz_decode_ws(ws, dec, raw_len, &dec_len, enc, enc_len) == MINLZ_OK);
    CHECK(dec_len == raw_len && memcmp(dec, src, raw_len) == 0);

    minlz_workspace_destroy(ws);
    free(dec);
    free(enc_again);
    free(enc);
    free(src);
    return 0;
}

static int test_corrupt(void) {
    uint8_t bad_offset[] = {0, 4, 0x01, 0x00};
    uint8_t out[16];
    size_t out_len = 0;
    CHECK(minlz_decode(out, sizeof(out), &out_len, bad_offset, sizeof(bad_offset)) == MINLZ_CORRUPT);
    CHECK(minlz_decoded_len((const uint8_t *)"", 0, &out_len) == MINLZ_CORRUPT);
    return 0;
}

static int test_index_api(void) {
    static const char src[] =
        "MinLZ indexed stream test data. MinLZ indexed stream test data. "
        "MinLZ indexed stream test data. MinLZ indexed stream test data.";
    minlz_stream_options opts;
    test_mem_read r;
    test_mem_write encoded = {0};
    test_mem_write decoded = {0};
    uint8_t *idx_bytes = NULL;
    size_t idx_len = 0;
    minlz_index *idx = NULL;
    minlz_index_info info;
    minlz_index_pair pair;
    char *json = NULL;
    size_t json_len = 0;
    const uint8_t *trimmed = NULL;
    size_t trimmed_len = 0;
    uint8_t *restored = NULL;
    size_t restored_len = 0;

    minlz_stream_options_init(&opts);
    opts.block_size = MINLZ_MIN_BLOCK_SIZE;
    opts.level = MINLZ_LEVEL_BALANCED;
    opts.add_index = 1;
    r.data = (const uint8_t *)src;
    r.len = sizeof(src) - 1;
    r.pos = 0;
    CHECK(minlz_stream_encode(test_read_cb, &r, test_write_cb, &encoded, &opts) == MINLZ_OK);
    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    CHECK(minlz_stream_decode(test_read_cb, &r, test_write_cb, &decoded, &opts) == MINLZ_OK);
    CHECK(decoded.len == sizeof(src) - 1);
    CHECK(memcmp(decoded.data, src, decoded.len) == 0);

    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    CHECK(minlz_index_stream(test_read_cb, &r, &idx_bytes, &idx_len) == MINLZ_OK);
    CHECK(idx_len > 0);
    CHECK(minlz_index_create(&idx) == MINLZ_OK);
    CHECK(minlz_index_load(idx, idx_bytes, idx_len, NULL) == MINLZ_OK);
    CHECK(minlz_index_info_get(idx, &info) == MINLZ_OK);
    CHECK(info.total_uncompressed == (int64_t)(sizeof(src) - 1));
    CHECK(info.est_block_uncompressed == (int64_t)(sizeof(src) - 1));
    CHECK(info.entries >= 1);
    CHECK(minlz_index_find(idx, 0, &pair) == MINLZ_OK);
    CHECK(pair.compressed_offset == 10 && pair.uncompressed_offset == 0);
    CHECK(minlz_index_json(idx, &json, &json_len) == MINLZ_OK);
    CHECK(json_len > 0 && strstr(json, "total_uncompressed") != NULL);
    CHECK(minlz_index_remove_headers(idx_bytes, idx_len, &trimmed, &trimmed_len) == MINLZ_OK);
    CHECK(trimmed_len > 0);
    CHECK(minlz_index_restore_headers(trimmed, trimmed_len, &restored, &restored_len) == MINLZ_OK);
    CHECK(restored_len == idx_len);
    CHECK(memcmp(restored, idx_bytes, idx_len) == 0);

    minlz_index_destroy(idx);
    minlz_free(idx_bytes);
    minlz_free(json);
    minlz_free(restored);
    free(encoded.data);
    free(decoded.data);
    return 0;
}

static int test_reader_writer_api(void) {
    uint8_t src[9000];
    uint8_t out[64];
    size_t i;
    size_t n = 0;
    uint64_t total = 0;
    int64_t pos = 0;
    minlz_stream_options opts;
    test_mem_write encoded = {0};
    test_mem_write decoded = {0};
    test_mem_read r;
    minlz_writer *w = NULL;
    minlz_reader *reader = NULL;
    minlz_readseeker *rs = NULL;

    for (i = 0; i < sizeof(src); i++) {
        src[i] = (uint8_t)("reader-writer-api:"[i % 18] + (uint8_t)(i >> 11));
    }
    minlz_stream_options_init(&opts);
    opts.block_size = MINLZ_MIN_BLOCK_SIZE;
    opts.level = MINLZ_LEVEL_FASTEST;
    opts.add_index = 1;
    CHECK(minlz_writer_create(&w, test_write_cb, &encoded, &opts) == MINLZ_OK);
    CHECK(minlz_writer_write(w, src, 123, &n) == MINLZ_OK && n == 123);
    CHECK(minlz_writer_write(w, src + 123, sizeof(src) - 123, &n) == MINLZ_OK &&
          n == sizeof(src) - 123);
    CHECK(minlz_writer_close(w) == MINLZ_OK);
    minlz_writer_destroy(w);

    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    CHECK(minlz_reader_create(&reader, test_read_cb, &r, &opts) == MINLZ_OK);
    CHECK(minlz_reader_decode_concurrent(reader, test_write_cb, &decoded, 4, &total) ==
          MINLZ_OK);
    CHECK(total == sizeof(src));
    CHECK(decoded.len == sizeof(src));
    CHECK(memcmp(decoded.data, src, sizeof(src)) == 0);
    minlz_reader_destroy(reader);
    reader = NULL;
    free(decoded.data);
    decoded.data = NULL;
    decoded.len = 0;
    decoded.cap = 0;
    total = 0;

    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    CHECK(minlz_reader_create(&reader, test_read_cb, &r, &opts) == MINLZ_OK);
    CHECK(minlz_reader_read(reader, out, 17, &n) == MINLZ_OK && n == 17);
    CHECK(memcmp(out, src, 17) == 0);
    CHECK(minlz_reader_skip(reader, 100) == MINLZ_OK);
    CHECK(minlz_reader_read_byte(reader, out) == MINLZ_OK);
    CHECK(out[0] == src[117]);
    CHECK(minlz_reader_readseeker(reader, NULL, 0, &rs) == MINLZ_OK);
    CHECK(minlz_readseeker_seek(rs, 4096, MINLZ_SEEK_SET, &pos) == MINLZ_OK && pos == 4096);
    CHECK(minlz_readseeker_read(rs, out, sizeof(out), &n) == MINLZ_OK && n == sizeof(out));
    CHECK(memcmp(out, src + 4096, sizeof(out)) == 0);
    CHECK(minlz_readseeker_read_at(rs, out, sizeof(out), 2048, &n) == MINLZ_OK &&
          n == sizeof(out));
    CHECK(memcmp(out, src + 2048, sizeof(out)) == 0);
    CHECK(minlz_reader_write_to(reader, test_write_cb, &decoded, &total) == MINLZ_OK);
    CHECK(total == sizeof(src) - 2048 - sizeof(out));

    minlz_readseeker_destroy(rs);
    minlz_reader_destroy(reader);
    rs = NULL;
    reader = NULL;
    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    CHECK(minlz_index_stream(test_read_cb, &r, NULL, NULL) == MINLZ_CORRUPT);
    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    {
        uint8_t *idx_bytes = NULL;
        size_t idx_len = 0;
        CHECK(minlz_index_stream(test_read_cb, &r, &idx_bytes, &idx_len) == MINLZ_OK);
        CHECK(idx_len > 0);
        r.data = encoded.data;
        r.len = encoded.len;
        r.pos = 0;
        CHECK(minlz_reader_create_seekable(&reader, test_read_cb, test_seek_cb, &r,
                                           &opts) == MINLZ_OK);
        CHECK(minlz_reader_readseeker(reader, idx_bytes, idx_len, &rs) == MINLZ_OK);
        CHECK(minlz_readseeker_read_at(rs, out, sizeof(out), 3072, &n) == MINLZ_OK &&
              n == sizeof(out));
        CHECK(memcmp(out, src + 3072, sizeof(out)) == 0);
        {
            const size_t lengths[] = {0u, 1u, 64u, 255u, 256u, 257u};
            const size_t offsets[] = {1u, 3840u, 3841u, 8744u, 8999u, 9000u};
            uint8_t guarded[259];
            size_t a, b, pass;
            for (a = 0; a < sizeof(offsets) / sizeof(offsets[0]); a++) {
                for (b = 0; b < sizeof(lengths) / sizeof(lengths[0]); b++) {
                    size_t want = sizeof(src) - offsets[a];
                    if (want > lengths[b]) {
                        want = lengths[b];
                    }
                    /* Repeat each request to exercise both misses and cache hits. */
                    for (pass = 0; pass < 2u; pass++) {
                        memset(guarded, 0xa5, sizeof(guarded));
                        n = SIZE_MAX;
                        CHECK(minlz_readseeker_read_at(rs, guarded + 1u, lengths[b],
                                  (int64_t)offsets[a], &n) == MINLZ_OK && n == want);
                        CHECK(memcmp(guarded + 1u, src + offsets[a], want) == 0);
                        CHECK(guarded[0] == 0xa5 && guarded[want + 1u] == 0xa5);
                    }
                }
            }
            CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 256u, -1, &n) ==
                  MINLZ_CORRUPT && n == 0u);
            CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 256u,
                      (int64_t)sizeof(src) + 1, &n) == MINLZ_CORRUPT && n == 0u);
        }
        CHECK(minlz_readseeker_seek(rs, 8192, MINLZ_SEEK_SET, &pos) == MINLZ_OK &&
              pos == 8192);
        CHECK(minlz_readseeker_read(rs, out, sizeof(out), &n) == MINLZ_OK &&
              n == sizeof(out));
        CHECK(memcmp(out, src + 8192, sizeof(out)) == 0);
        minlz_readseeker_destroy(rs);
        minlz_reader_destroy(reader);
        minlz_free(idx_bytes);
    }
    free(encoded.data);
    free(decoded.data);
    return 0;
}

static int test_readseeker_large_cache(void) {
    const size_t src_len = (2u << 20) + 257u;
    const int levels[] = {MINLZ_LEVEL_UNCOMPRESSED, MINLZ_LEVEL_FASTEST};
    const size_t lengths[] = {0u, 1u, 255u, 256u, 257u};
    size_t offsets[] = {0u, 1u, src_len / 2u, src_len - 257u,
                        src_len - 256u, src_len - 1u, src_len};
    uint8_t *src = (uint8_t *)malloc(src_len);
    uint8_t guarded[259];
    size_t i, level, a, b, pass, n;
    CHECK(src != NULL);
    for (i = 0; i < src_len; i++) {
        src[i] = (uint8_t)("large-indexed-cache:"[i % 20u] + (uint8_t)(i >> 11));
    }
    for (level = 0; level < sizeof(levels) / sizeof(levels[0]); level++) {
        minlz_stream_options opts;
        minlz_reader *reader = NULL;
        minlz_readseeker *rs = NULL;
        test_mem_write encoded = {0};
        test_mem_read r;
        uint8_t *idx = NULL;
        size_t idx_len = 0;
        minlz_stream_options_init(&opts);
        opts.block_size = MINLZ_MAX_BLOCK_SIZE;
        opts.level = levels[level];
        r.data = src;
        r.len = src_len;
        r.pos = 0;
        CHECK(minlz_stream_encode(test_read_cb, &r, test_write_cb, &encoded, &opts) ==
              MINLZ_OK);
        r.data = encoded.data;
        r.len = encoded.len;
        r.pos = 0;
        CHECK(minlz_index_stream(test_read_cb, &r, &idx, &idx_len) == MINLZ_OK);
        r.pos = 0;
        CHECK(minlz_reader_create_seekable(&reader, test_read_cb, test_seek_cb, &r,
                                          &opts) == MINLZ_OK);
        CHECK(minlz_reader_readseeker(reader, idx, idx_len, &rs) == MINLZ_OK);
        for (a = 0; a < sizeof(offsets) / sizeof(offsets[0]); a++) {
            for (b = 0; b < sizeof(lengths) / sizeof(lengths[0]); b++) {
                size_t want = src_len - offsets[a];
                if (want > lengths[b]) {
                    want = lengths[b];
                }
                for (pass = 0; pass < 2u; pass++) {
                    memset(guarded, 0xa5, sizeof(guarded));
                    n = SIZE_MAX;
                    CHECK(minlz_readseeker_read_at(rs, guarded + 1u, lengths[b],
                              (int64_t)offsets[a], &n) == MINLZ_OK && n == want);
                    CHECK(memcmp(guarded + 1u, src + offsets[a], want) == 0);
                    CHECK(guarded[0] == 0xa5 && guarded[want + 1u] == 0xa5);
                }
            }
        }
        CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 256u, -1, &n) ==
              MINLZ_CORRUPT && n == 0u);
        CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 256u,
                  (int64_t)src_len + 1, &n) == MINLZ_CORRUPT && n == 0u);
        minlz_readseeker_destroy(rs);
        minlz_reader_destroy(reader);
        minlz_free(idx);
        free(encoded.data);
    }
    free(src);
    return 0;
}

static int test_readseeker_mixed_cache(void) {
    const size_t block_size = MINLZ_MIN_BLOCK_SIZE;
    const size_t src_len = 12u * MINLZ_MIN_BLOCK_SIZE + 257u;
    uint8_t *src = (uint8_t *)malloc(src_len);
    uint8_t guarded[259];
    uint8_t *indexes[2] = {NULL, NULL};
    size_t index_lens[2] = {0, 0};
    size_t chunks[13] = {0};
    size_t i, p, mode, pass, n, raw_count = 0, comp_count = 0, data_count = 0;
    uint32_t random = 123456789u;
    minlz_stream_options opts;
    minlz_index *sparse = NULL;
    test_mem_write encoded = {0};
    test_mem_read r = {0};
    CHECK(src != NULL);
    for (i = 0; i < src_len; i++) {
        random ^= random << 13;
        random ^= random >> 17;
        random ^= random << 5;
        src[i] = (i / block_size) & 1u ? (uint8_t)('a' + i % 7u) : (uint8_t)random;
    }
    minlz_stream_options_init(&opts);
    opts.block_size = block_size;
    opts.level = MINLZ_LEVEL_FASTEST;
    r.data = src;
    r.len = src_len;
    CHECK(minlz_stream_encode(test_read_cb, &r, test_write_cb, &encoded, &opts) == MINLZ_OK);
    for (p = 0; p + 4u <= encoded.len;) {
        size_t len = (size_t)encoded.data[p + 1u] |
                     ((size_t)encoded.data[p + 2u] << 8) |
                     ((size_t)encoded.data[p + 3u] << 16);
        uint8_t type = encoded.data[p];
        CHECK(len <= encoded.len - p - 4u);
        if (type == MINLZ_CHUNK_UNCOMPRESSED || type == MINLZ_CHUNK_MINLZ ||
            type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            CHECK(data_count < 13u);
            chunks[data_count++] = p;
            if (type == MINLZ_CHUNK_UNCOMPRESSED) {
                raw_count++;
            } else {
                comp_count++;
                if (data_count % 4u == 0u) {
                    size_t decoded_len = 0, header_len = 0;
                    uint8_t *payload = encoded.data + p + 4u + MINLZ_CHECKSUM_SIZE;
                    CHECK(len >= MINLZ_CHECKSUM_SIZE);
                    CHECK(minlz_payload_decoded_len(payload, len - MINLZ_CHECKSUM_SIZE,
                          &decoded_len, &header_len) == MINLZ_OK && decoded_len == block_size);
                    encoded.data[p] = MINLZ_CHUNK_MINLZ_COMP_CRC;
                    minlz_store32(encoded.data + p + 4u,
                        minlz_crc(payload + header_len, len - MINLZ_CHECKSUM_SIZE - header_len));
                }
            }
        }
        p += 4u + len;
    }
    CHECK(data_count == 13u && raw_count != 0u && comp_count != 0u);
    r.data = encoded.data;
    r.len = encoded.len;
    r.pos = 0;
    CHECK(minlz_index_stream(test_read_cb, &r, &indexes[0], &index_lens[0]) == MINLZ_OK);
    CHECK(minlz_index_create(&sparse) == MINLZ_OK);
    CHECK(minlz_index_add_pair(sparse, (int64_t)chunks[0], 0) == MINLZ_OK);
    CHECK(minlz_index_serialize(sparse, &indexes[1], &index_lens[1],
                               (int64_t)src_len, (int64_t)encoded.len) == MINLZ_OK);
    minlz_index_destroy(sparse);
    for (mode = 0; mode < 2u; mode++) {
        minlz_reader *reader = NULL;
        minlz_readseeker *rs = NULL;
        minlz_readseeker *other = NULL;
        test_mem_write decoded = {0};
        uint64_t total = 0;
        r.pos = 0;
        CHECK(minlz_reader_create_seekable(&reader, test_read_cb, test_seek_cb, &r,
                                          &opts) == MINLZ_OK);
        CHECK(minlz_reader_readseeker(reader, indexes[mode], index_lens[mode], &rs) == MINLZ_OK);
        /* More than eight alternating raw/compressed blocks force cache eviction. */
        for (pass = 0; pass < 3u; pass++) {
            for (i = 0; i < 13u; i++) {
                size_t block = pass & 1u ? 12u - i : i;
                size_t offset = block * block_size + (block < 12u ? block_size - 129u : 0u);
                size_t want = src_len - offset;
                if (want > 257u) {
                    want = 257u;
                }
                memset(guarded, 0xa5, sizeof(guarded));
                CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u,
                                              (int64_t)offset, &n) == MINLZ_OK && n == want);
                CHECK(memcmp(guarded + 1u, src + offset, want) == 0);
                CHECK(guarded[0] == 0xa5 && guarded[want + 1u] == 0xa5);
            }
        }
        CHECK(minlz_reader_readseeker(reader, indexes[mode], index_lens[mode], &other) == MINLZ_OK);
        CHECK(minlz_readseeker_read_at(other, guarded + 1u, 257u, 4096, &n) == MINLZ_OK && n == 257u);
        CHECK(memcmp(guarded + 1u, src + 4096, n) == 0);
        r.pos = 0;
        CHECK(minlz_reader_reset_seekable(reader, test_read_cb, test_seek_cb, &r) == MINLZ_OK);
        CHECK(minlz_reader_write_to(reader, test_write_cb, &decoded, &total) == MINLZ_OK);
        CHECK(total == src_len && decoded.len == src_len && memcmp(decoded.data, src, src_len) == 0);
        CHECK(minlz_readseeker_read_at(other, guarded + 1u, 257u, 4096, &n) == MINLZ_OK && n == 257u);
        CHECK(memcmp(guarded + 1u, src + 4096, n) == 0);
        minlz_readseeker_destroy(other);
        minlz_readseeker_destroy(rs);
        minlz_reader_destroy(reader);
        free(decoded.data);
    }
    {
        minlz_reader *reader = NULL;
        minlz_readseeker *rs = NULL;
        r.pos = 0;
        CHECK(minlz_reader_create_seekable(&reader, test_read_cb, test_seek_cb, &r,
                                          &opts) == MINLZ_OK);
        CHECK(minlz_reader_readseeker(reader, indexes[0], index_lens[0], &rs) == MINLZ_OK);
        CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u, 0, &n) == MINLZ_OK && n == 257u);
        for (i = 10u; i <= 11u; i++) {
            uint8_t original = encoded.data[chunks[i] + 4u];
            encoded.data[chunks[i] + 4u] ^= 1u;
            CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u,
                                          (int64_t)(i * block_size), &n) == MINLZ_CRC && n == 0u);
            CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u, 0, &n) == MINLZ_OK && n == 257u);
            CHECK(memcmp(guarded + 1u, src, n) == 0);
            encoded.data[chunks[i] + 4u] = original;
            CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u,
                                          (int64_t)(i * block_size), &n) == MINLZ_OK && n == 257u);
            CHECK(memcmp(guarded + 1u, src + i * block_size, n) == 0);
        }
        minlz_readseeker_destroy(rs);
        minlz_reader_destroy(reader);
    }
    {
        minlz_reader *reader = NULL;
        minlz_readseeker *rs = NULL;
        uint8_t header[4];
        uint8_t *dense_index = NULL;
        size_t dense_len = 0;
        memcpy(header, encoded.data + chunks[0], sizeof(header));
        CHECK(minlz_index_create(&sparse) == MINLZ_OK);
        minlz_index_reset(sparse, block_size);
        for (i = 0; i < 13u; i++) {
            CHECK(minlz_index_add_pair(sparse, (int64_t)chunks[i],
                                      (int64_t)(i * block_size)) == MINLZ_OK);
        }
        CHECK(minlz_index_serialize(sparse, &dense_index, &dense_len,
                                   (int64_t)src_len, (int64_t)encoded.len) == MINLZ_OK);
        minlz_index_destroy(sparse);
        r.pos = 0;
        CHECK(minlz_reader_create_seekable(&reader, test_read_cb, test_seek_cb, &r,
                                          &opts) == MINLZ_OK);
        CHECK(minlz_reader_readseeker(reader, dense_index, dense_len, &rs) == MINLZ_OK);
        for (pass = 0; pass < 3u; pass++) {
            for (i = 1u; i < 6u; i++) {
                /* Fail after reserving large input scratch, then seek to a valid raw chunk. */
                encoded.data[chunks[0]] = MINLZ_CHUNK_MINLZ;
                encoded.data[chunks[0] + 1u] = 0;
                encoded.data[chunks[0] + 2u] = 0;
                encoded.data[chunks[0] + 3u] = 32u;
                CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u, 0, &n) ==
                      MINLZ_CORRUPT && n == 0u);
                memcpy(encoded.data + chunks[0], header, sizeof(header));
                CHECK(minlz_readseeker_read_at(rs, guarded + 1u, 257u,
                          (int64_t)(2u * i * block_size), &n) == MINLZ_OK && n == 257u);
                CHECK(memcmp(guarded + 1u, src + 2u * i * block_size, n) == 0);
            }
        }
        minlz_readseeker_destroy(rs);
        minlz_reader_destroy(reader);
        minlz_free(dense_index);
    }
    minlz_free(indexes[0]);
    minlz_free(indexes[1]);
    free(encoded.data);
    free(src);
    return 0;
}

typedef struct test_search_matches {
    int count;
    int saw_boundary;
    int saw_inner;
} test_search_matches;

typedef struct test_search_offsets {
    int64_t values[64];
    size_t count;
} test_search_offsets;

static minlz_status test_search_match_cb(
    void *ctx, const minlz_search_result *result) {
    test_search_matches *matches = (test_search_matches *)ctx;
    matches->count++;
    if (result->stream_offset == 4092) {
        matches->saw_boundary = 1;
        CHECK(result->previous_block != NULL);
        CHECK(result->offset >= 0);
    }
    if (result->stream_offset == 6000) {
        matches->saw_inner = 1;
    }
    return MINLZ_OK;
}

static minlz_status test_search_offset_cb(
    void *ctx, const minlz_search_result *result) {
    test_search_offsets *offsets = (test_search_offsets *)ctx;
    CHECK(offsets->count <
          sizeof(offsets->values) / sizeof(offsets->values[0]));
    offsets->values[offsets->count++] = result->stream_offset;
    return MINLZ_OK;
}

typedef struct test_prefix_only_matches {
    int count;
    int64_t expected_offset;
} test_prefix_only_matches;

typedef struct test_forward_matches {
    int calls;
    int expect_eof;
    int64_t expected_offset;
} test_forward_matches;

typedef struct test_lazy_previous_matches {
    int calls;
    int64_t expected_offset;
    size_t expected_len;
    uint8_t expected_byte;
} test_lazy_previous_matches;

typedef struct test_compressed_search_stats_capture {
    int calls;
    minlz_compressed_search_stats stats;
} test_compressed_search_stats_capture;

static void test_compressed_search_stats_cb(
    void *ctx, const minlz_compressed_search_stats *stats) {
    test_compressed_search_stats_capture *capture =
        (test_compressed_search_stats_capture *)ctx;
    if (!stats) {
        capture->calls = -1;
        return;
    }
    capture->calls++;
    capture->stats = *stats;
}

static int test_search_table_crc(void) {
    uint8_t block[MINLZ_MIN_BLOCK_SIZE];
    minlz_search_config config;
    minlz_search_config resolved;
    minlz_search_table_view view;
    uint8_t *chunk = NULL;
    uint8_t *table_storage = NULL;
    size_t chunk_len = 0u;
    size_t config_len = 0u;
    size_t i;

    for (i = 0u; i < sizeof(block); i++) {
        block[i] = (uint8_t)("search-table-crc"[i % 16u] + (uint8_t)(i >> 9));
    }

    minlz_search_config_init(&config);
    minlz_search_config_set_compression(&config, 0);
    CHECK(minlz_search_config_resolve(
              &resolved, &config, sizeof(block)) == MINLZ_OK);
    CHECK(minlz_search_make_table_chunk(
              &resolved, block, sizeof(block), NULL, 0u,
              &chunk, &chunk_len) == MINLZ_OK);
    CHECK(chunk[0] == MINLZ_CHUNK_SEARCH_TABLE);
    CHECK(minlz_search_parse_info(
              chunk + 4u, chunk_len - 4u, &config,
              &config_len) == MINLZ_OK);
    CHECK(config_len + 5u <= chunk_len - 4u);
    chunk[4u + config_len + 1u] ^= 1u;
    CHECK(minlz_search_parse_table(
              chunk + 4u, chunk_len - 4u, 0, &view) == MINLZ_CRC);
    CHECK(minlz_search_parse_table(
              chunk + 4u, chunk_len - 4u, 1, &view) == MINLZ_OK);
    free(chunk);
    chunk = NULL;

    minlz_search_config_init(&config);
    minlz_search_config_set_compression_force(&config, 1);
    CHECK(minlz_search_config_resolve(
              &resolved, &config, sizeof(block)) == MINLZ_OK);
    CHECK(minlz_search_make_table_chunk(
              &resolved, block, sizeof(block), NULL, 0u,
              &chunk, &chunk_len) == MINLZ_OK);
    CHECK(chunk[0] == MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED);
    CHECK(minlz_search_parse_info(
              chunk + 4u, chunk_len - 4u, &config,
              &config_len) == MINLZ_OK);
    CHECK(config_len + 7u <= chunk_len - 4u);
    chunk[4u + config_len + 1u] ^= 1u;
    CHECK(minlz_search_parse_table_compressed(
              chunk + 4u, chunk_len - 4u, 0,
              &table_storage, &view) == MINLZ_CRC);
    CHECK(table_storage == NULL);
    CHECK(minlz_search_parse_table_compressed(
              chunk + 4u, chunk_len - 4u, 1,
              &table_storage, &view) == MINLZ_OK);
    free(table_storage);
    free(chunk);
    return 0;
}

static minlz_status test_prefix_only_match_cb(
    void *ctx, const minlz_search_result *result) {
    test_prefix_only_matches *matches =
        (test_prefix_only_matches *)ctx;
    CHECK(result->stream_offset == matches->expected_offset);
    matches->count++;
    return MINLZ_OK;
}

static minlz_status test_forward_match_cb(
    void *ctx, const minlz_search_result *result) {
    test_forward_matches *matches =
        (test_forward_matches *)ctx;
    matches->calls++;
    CHECK(result->stream_offset == matches->expected_offset);
    if (matches->calls == 1) {
        CHECK(result->block != NULL);
        CHECK(result->block_len != 0u);
        return MINLZ_SEARCH_FORWARD;
    }
    CHECK(matches->calls == 2);
    CHECK(result->previous_block != NULL);
    CHECK(result->previous_block_len != 0u);
    if (matches->expect_eof) {
        CHECK(result->block == NULL);
        CHECK(result->block_len == 0u);
    } else {
        CHECK(result->block != NULL);
        CHECK(result->block_len != 0u);
    }
    return MINLZ_SEARCH_STOP;
}

static minlz_status test_lazy_previous_match_cb(
    void *ctx, const minlz_search_result *result) {
    test_lazy_previous_matches *matches =
        (test_lazy_previous_matches *)ctx;
    const uint8_t *previous = NULL;
    const uint8_t *previous_again = NULL;
    size_t previous_len = 0u;
    size_t previous_again_len = 0u;
    size_t i;

    matches->calls++;
    CHECK(result->stream_offset == matches->expected_offset);
    CHECK(result->previous_block == NULL);
    CHECK(result->previous_block_len == matches->expected_len);
    CHECK(result->offset ==
          result->stream_offset - result->block_start);
    CHECK(minlz_search_result_load_previous(
              result, &previous, &previous_len) == MINLZ_OK);
    CHECK(previous != NULL);
    CHECK(previous_len == matches->expected_len);
    for (i = 0u; i < previous_len; i++) {
        CHECK(previous[i] == matches->expected_byte);
    }
    CHECK(minlz_search_result_load_previous(
              result, &previous_again,
              &previous_again_len) == MINLZ_OK);
    CHECK(previous_again == previous);
    CHECK(previous_again_len == previous_len);
    return MINLZ_SEARCH_STOP;
}

static int test_sidecar_forward_case(
    const test_mem_write *encoded,
    const minlz_search_config *search_config,
    const uint8_t *pattern, size_t pattern_len,
    int64_t expected_offset, int expect_eof,
    int truncate_before_eof) {
    test_mem_read stream_read;
    test_mem_read main_read;
    test_mem_read sidecar_read;
    test_mem_write sidecar = {0};
    minlz_sidecar_searcher *searcher = NULL;
    test_forward_matches matches;
    size_t sidecar_len;

    stream_read.data = encoded->data;
    stream_read.len = encoded->len;
    stream_read.pos = 0u;
    CHECK(minlz_build_sidecar(
              test_write_cb, &sidecar,
              test_read_cb, &stream_read,
              search_config, 1u, 0) == MINLZ_OK);

    sidecar_len = sidecar.len;
    if (truncate_before_eof) {
        size_t pos = 0u;
        sidecar_len = 0u;
        while (pos + 4u <= sidecar.len) {
            size_t chunk_len =
                (size_t)sidecar.data[pos + 1u] |
                ((size_t)sidecar.data[pos + 2u] << 8) |
                ((size_t)sidecar.data[pos + 3u] << 16);
            CHECK(pos + 4u + chunk_len <= sidecar.len);
            if (sidecar.data[pos] == MINLZ_CHUNK_EOF) {
                sidecar_len = pos;
                break;
            }
            pos += 4u + chunk_len;
        }
        CHECK(sidecar_len != 0u);
    }

    main_read.data = encoded->data;
    main_read.len = encoded->len;
    main_read.pos = 0u;
    main_read.read_at_calls = 0u;
    main_read.read_at_bytes = 0u;
    sidecar_read.data = sidecar.data;
    sidecar_read.len = sidecar_len;
    sidecar_read.pos = 0u;
    matches.calls = 0;
    matches.expect_eof = expect_eof;
    matches.expected_offset = expected_offset;
    CHECK(minlz_sidecar_searcher_create(
              &searcher,
              test_read_at_cb, &main_read,
              test_read_cb, &sidecar_read) == MINLZ_OK);
    CHECK(minlz_sidecar_searcher_search(
              searcher, pattern, pattern_len,
              test_forward_match_cb, &matches) == MINLZ_OK);
    CHECK(matches.calls == 2);
    minlz_sidecar_searcher_destroy(searcher);
    free(sidecar.data);
    return 0;
}

static int test_search_tables_api(void) {
    static const uint8_t pattern[] = "BOUNDARY";
    uint8_t *src = (uint8_t *)malloc(9000);
    minlz_search_config search_config;
    minlz_stream_options opts;
    test_mem_read read;
    test_mem_write encoded = {0};
    test_mem_write decoded = {0};
    test_mem_write built_sidecar = {0};
    test_mem_write extracted_sidecar = {0};
    test_mem_write stripped = {0};
    test_mem_write stripped_decoded = {0};
    test_search_matches matches = {0};
    minlz_block_searcher *searcher = NULL;
    minlz_sidecar_searcher *sidecar_searcher = NULL;
    minlz_search_stats stats;
    minlz_status search_status;
    size_t pos;
    int info_chunks = 0;
    int table_chunks = 0;

    CHECK(src != NULL);
    memset(src, 'a', 9000);
    memcpy(src + 4092, pattern, sizeof(pattern) - 1u);
    memcpy(src + 6000, pattern, sizeof(pattern) - 1u);

    minlz_search_config_init(&search_config);
    {
        char summary[160];
        size_t summary_len = minlz_search_config_snprint(
            summary, sizeof(summary), &search_config);
        CHECK(summary_len > 0u);
        CHECK(strcmp(
                  summary,
                  "matchLen=6 baseTableSize=0 (1 entries) "
                  "no-prefix") == 0);
        CHECK(strstr(summary, "matchLen=6") != NULL);
        CHECK(strstr(summary, "no-prefix") != NULL);
    }
    CHECK(search_config.compress_tables == 1u);
    CHECK(search_config.compression_skip_pct_x100 == 1000u);
    CHECK(minlz_search_config_set_compression_skip(
              &search_config, -0.01) == MINLZ_OK);
    CHECK(search_config.compression_skip_pct_x100 == 0u);
    CHECK(minlz_search_config_set_compression_skip(
              &search_config, 50.01) == MINLZ_OK);
    CHECK(search_config.compression_skip_pct_x100 == 5000u);
    CHECK(minlz_search_config_set_compression_skip(
              &search_config, 7.259) == MINLZ_OK);
    CHECK(search_config.compression_skip_pct_x100 == 725u);
    CHECK(search_config.compress_tables == 1u);
    CHECK(minlz_search_config_set_compression_skip(
              &search_config, 10.0) == MINLZ_OK);
    {
        minlz_search_config prefix_config;
        static const uint8_t prefix[] = {'='};
        minlz_search_config_init(&prefix_config);
        CHECK(minlz_search_config_set_max_population(
                  &prefix_config, 75u) == MINLZ_OK);
        CHECK(minlz_search_config_set_byte_prefix(
                  &prefix_config, prefix, sizeof(prefix)) ==
              MINLZ_OK);
        CHECK(prefix_config.max_reduced_population_pct == 10u);

        minlz_search_config_init(&prefix_config);
        CHECK(minlz_search_config_set_max_reduced_population(
                  &prefix_config, 25u) == MINLZ_OK);
        CHECK(minlz_search_config_set_byte_prefix(
                  &prefix_config, prefix, sizeof(prefix)) ==
              MINLZ_OK);
        CHECK(prefix_config.max_reduced_population_pct == 25u);
    }
    {
        minlz_search_config long_config;
        static const uint8_t prefix[] = "id\":\"";
        minlz_search_config_init(&long_config);
        CHECK(minlz_search_config_set_long_prefix(
                  &long_config, prefix, sizeof(prefix) - 1u, 0u) ==
              MINLZ_OK);
        CHECK(minlz_search_config_set_extras(
                  &long_config, 4u) == MINLZ_OK);
        CHECK(long_config.extras == 4u);
        CHECK(minlz_search_config_set_match_len(
                  &long_config, 13u) == MINLZ_CORRUPT);
        CHECK(long_config.match_len == 6u);
        CHECK(long_config.extras == 4u);
        CHECK(minlz_search_config_set_extras(
                  &long_config, 11u) == MINLZ_CORRUPT);
        CHECK(long_config.extras == 4u);
        CHECK(minlz_search_config_set_extras(
                  &long_config, 2u) == MINLZ_OK);
        CHECK(long_config.extras == 2u);
    }
    {
        minlz_search_config resolved;
        minlz_search_table_view view;
        test_compressed_search_stats_capture capture = {0};
        uint8_t *chunk = NULL;
        uint8_t *table_storage = NULL;
        size_t chunk_len = 0;
        int can_use = 0;
        minlz_search_config_set_compression_force(
            &search_config, 1);
        minlz_search_config_set_compression_stats_callback(
            &search_config, test_compressed_search_stats_cb, &capture);
        CHECK(minlz_search_config_resolve(
                  &resolved, &search_config,
                  MINLZ_MIN_BLOCK_SIZE) == MINLZ_OK);
        CHECK(minlz_search_make_table_chunk(
                  &resolved, src, MINLZ_MIN_BLOCK_SIZE,
                  src + MINLZ_MIN_BLOCK_SIZE, 6,
                  &chunk, &chunk_len) == MINLZ_OK);
        CHECK(chunk_len > 4);
        CHECK(chunk[0] == MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED);
        CHECK(capture.calls == 1);
        CHECK(capture.stats.bitmap_bytes > 0u);
        CHECK(capture.stats.total_bits ==
              capture.stats.bitmap_bytes * 8u);
        CHECK(capture.stats.chunk_0x45_size > 4u);
        CHECK(capture.stats.chunk_0x46_size == chunk_len);
        CHECK(capture.stats.emitted_0x46);
        CHECK(capture.stats.sub_blocks > 0u);
        CHECK(capture.stats.blocks_own_table +
                  capture.stats.blocks_global_table +
                  capture.stats.blocks_raw +
                  capture.stats.blocks_rle +
                  capture.stats.blocks_sparse ==
              capture.stats.sub_blocks);
        CHECK(minlz_search_parse_table_compressed(
                  chunk + 4, chunk_len - 4, 0,
                  &table_storage, &view) == MINLZ_OK);
        CHECK(minlz_search_pattern_might_match(
                  &view, pattern, sizeof(pattern) - 1u,
                  &can_use));
        CHECK(can_use);
        free(table_storage);
        free(chunk);
        minlz_search_config_set_compression_force(
            &search_config, 0);
        minlz_search_config_set_compression_stats_callback(
            &search_config, NULL, NULL);
    }
    minlz_search_config_set_compression(&search_config, 0);
    minlz_stream_options_init(&opts);
    opts.block_size = MINLZ_MIN_BLOCK_SIZE;
    opts.level = MINLZ_LEVEL_FASTEST;
    opts.search = &search_config;

    read.data = src;
    read.len = 9000;
    read.pos = 0;
    CHECK(minlz_stream_encode(
              test_read_cb, &read, test_write_cb, &encoded, &opts) ==
          MINLZ_OK);

    pos = 0;
    while (pos + 4u <= encoded.len) {
        uint8_t type = encoded.data[pos];
        size_t chunk_len = (size_t)encoded.data[pos + 1u] |
                           ((size_t)encoded.data[pos + 2u] << 8) |
                           ((size_t)encoded.data[pos + 3u] << 16);
        CHECK(pos + 4u + chunk_len <= encoded.len);
        if (type == MINLZ_CHUNK_SEARCH_INFO) {
            info_chunks++;
        } else if (type == MINLZ_CHUNK_SEARCH_TABLE) {
            table_chunks++;
        }
        pos += 4u + chunk_len;
    }
    CHECK(pos == encoded.len);
    CHECK(info_chunks == 1);
    CHECK(table_chunks >= 2);

    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0;
    CHECK(minlz_stream_decode(
              test_read_cb, &read, test_write_cb, &decoded, &opts) ==
          MINLZ_OK);
    CHECK(decoded.len == 9000);
    CHECK(memcmp(decoded.data, src, 9000) == 0);

    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0;
    CHECK(minlz_block_searcher_create(
              &searcher, test_read_cb, &read) == MINLZ_OK);
    minlz_block_searcher_set_collect_stats(searcher, 1);
    search_status = minlz_block_searcher_search(
        searcher, pattern, sizeof(pattern) - 1u,
        test_search_match_cb, &matches);
    if (search_status != MINLZ_OK) {
        fprintf(stderr, "search failed: %s (%d)\n",
                minlz_status_string(search_status), (int)search_status);
    }
    CHECK(search_status == MINLZ_OK);
    CHECK(matches.count == 2);
    CHECK(matches.saw_boundary);
    CHECK(matches.saw_inner);
    CHECK(minlz_block_searcher_stats(searcher, &stats) == MINLZ_OK);
    CHECK(stats.blocks_total == 3);
    CHECK(stats.tables_present >= 2);
    CHECK(stats.table_bits_sum > 0u);
    CHECK(stats.uncompressed_size == 9000u);
    CHECK(stats.table_population_max >=
          stats.table_population_min);
    CHECK(stats.table_population_sum >= 0.0);
    CHECK(stats.matches == 2);
    {
        size_t window_count =
            minlz_block_searcher_window_count(searcher);
        minlz_search_window_stat *windows =
            (minlz_search_window_stat *)calloc(
                window_count, sizeof(*windows));
        size_t i;
        FILE *stats_out;
        CHECK(window_count ==
              sizeof(pattern) - 1u -
                  search_config.match_len + 1u);
        CHECK(windows != NULL);
        for (i = 0u; i < window_count; i++) {
            CHECK(minlz_block_searcher_window_get(
                      searcher, i, &windows[i]) == MINLZ_OK);
            CHECK(windows[i].bytes_len ==
                  search_config.match_len);
            CHECK(windows[i].present + windows[i].absent ==
                  stats.tables_present);
        }
        CHECK(minlz_block_searcher_window_get(
                  searcher, window_count, &windows[0]) ==
              MINLZ_CORRUPT);
        stats_out = tmpfile();
        CHECK(stats_out != NULL);
        minlz_search_stats_fprint_extended(
            stats_out, &stats, windows, window_count);
        CHECK(ftell(stats_out) > 0);
        fclose(stats_out);
        free(windows);
    }

    minlz_block_searcher_destroy(searcher);
    searcher = NULL;

    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0;
    CHECK(minlz_build_sidecar(
              test_write_cb, &built_sidecar,
              test_read_cb, &read,
              &search_config, 1, 0) == MINLZ_OK);
    pos = 0;
    table_chunks = 0;
    {
        int refs = 0;
        while (pos + 4u <= built_sidecar.len) {
            uint8_t type = built_sidecar.data[pos];
            size_t chunk_len =
                (size_t)built_sidecar.data[pos + 1u] |
                ((size_t)built_sidecar.data[pos + 2u] << 8) |
                ((size_t)built_sidecar.data[pos + 3u] << 16);
            CHECK(pos + 4u + chunk_len <= built_sidecar.len);
            if (type == MINLZ_CHUNK_SEARCH_TABLE) {
                table_chunks++;
            } else if (type == MINLZ_CHUNK_REMOTE_BLOCK_REF) {
                refs++;
            }
            pos += 4u + chunk_len;
        }
        CHECK(pos == built_sidecar.len);
        CHECK(table_chunks >= 2);
        CHECK(refs == 3);
    }
    matches.count = 0;
    matches.saw_boundary = 0;
    matches.saw_inner = 0;
    {
        test_mem_read main_read;
        test_mem_read sidecar_read;
        main_read.data = encoded.data;
        main_read.len = encoded.len;
        main_read.pos = 0;
        main_read.read_at_calls = 0u;
        main_read.read_at_bytes = 0u;
        sidecar_read.data = built_sidecar.data;
        sidecar_read.len = built_sidecar.len;
        sidecar_read.pos = 0;
        CHECK(minlz_sidecar_searcher_create(
                  &sidecar_searcher,
                  test_read_at_cb, &main_read,
                  test_read_cb, &sidecar_read) == MINLZ_OK);
        minlz_sidecar_searcher_set_collect_stats(
            sidecar_searcher, 1);
        search_status = minlz_sidecar_searcher_search(
            sidecar_searcher, pattern, sizeof(pattern) - 1u,
            test_search_match_cb, &matches);
        if (search_status != MINLZ_OK) {
            fprintf(stderr, "sidecar search failed: %s (%d)\n",
                    minlz_status_string(search_status),
                    (int)search_status);
        }
        CHECK(search_status == MINLZ_OK);
        CHECK(matches.count == 2);
        CHECK(matches.saw_boundary);
        CHECK(matches.saw_inner);
        CHECK(minlz_sidecar_searcher_stats(
                  sidecar_searcher, &stats) == MINLZ_OK);
        CHECK(stats.uncompressed_size == 9000u);
        CHECK(main_read.read_at_calls > 0u);
        CHECK(main_read.read_at_calls <= stats.blocks_total);
        CHECK(minlz_sidecar_searcher_window_count(
                  sidecar_searcher) > 0u);
        minlz_sidecar_searcher_destroy(sidecar_searcher);
        sidecar_searcher = NULL;
    }

    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0;
    CHECK(minlz_extract_sidecar(
              test_write_cb, &extracted_sidecar,
              test_write_cb, &stripped,
              test_read_cb, &read) == MINLZ_OK);
    read.data = stripped.data;
    read.len = stripped.len;
    read.pos = 0;
    CHECK(minlz_stream_decode(
              test_read_cb, &read, test_write_cb, &stripped_decoded,
              &opts) == MINLZ_OK);
    CHECK(stripped_decoded.len == 9000);
    CHECK(memcmp(stripped_decoded.data, src, 9000) == 0);

    free(stripped_decoded.data);
    free(stripped.data);
    free(extracted_sidecar.data);
    free(built_sidecar.data);
    free(decoded.data);
    free(encoded.data);
    free(src);
    return 0;
}

static int test_search_three_block_straddle(void) {
    static const uint8_t pattern[] = "NEEDLE";
    const size_t block_size = MINLZ_MIN_BLOCK_SIZE;
    const size_t tiny = 1u;
    const size_t src_len = 3u * MINLZ_MIN_BLOCK_SIZE;
    uint8_t *src = (uint8_t *)malloc(src_len);
    minlz_search_config config;
    minlz_stream_options opts;
    minlz_writer *writer = NULL;
    minlz_block_searcher *block_searcher = NULL;
    minlz_sidecar_searcher *sidecar_searcher = NULL;
    test_mem_write encoded = {0};
    test_mem_write sidecar = {0};
    test_mem_read inline_read;
    test_mem_read main_read;
    test_mem_read sidecar_read;
    test_search_offsets inline_offsets = {{0}, 0u};
    test_search_offsets sidecar_offsets = {{0}, 0u};
    size_t written = 0u;
    size_t pos;
    size_t expected_count = 0u;

    CHECK(src != NULL);
    memset(src, 'x', src_len);
    for (pos = block_size - 2u * (sizeof(pattern) - 1u);
         pos + sizeof(pattern) - 1u <= src_len &&
         pos < block_size + 2u * (sizeof(pattern) - 1u);
         pos += sizeof(pattern) - 1u) {
        memcpy(src + pos, pattern, sizeof(pattern) - 1u);
    }
    for (pos = block_size + tiny - 2u;
         pos + sizeof(pattern) - 1u <= src_len &&
         pos < block_size + tiny + 4u * (sizeof(pattern) - 1u);
         pos += sizeof(pattern) - 1u) {
        memcpy(src + pos, pattern, sizeof(pattern) - 1u);
    }
    memcpy(src + 7u, pattern, sizeof(pattern) - 1u);
    memcpy(src + 2u * block_size + 9u,
           pattern, sizeof(pattern) - 1u);

    minlz_search_config_init(&config);
    minlz_stream_options_init(&opts);
    opts.block_size = block_size;
    opts.threads = 1;
    opts.add_index = 0;
    opts.search = &config;
    opts.sidecar_write_cb = test_write_cb;
    opts.sidecar_write_ctx = &sidecar;
    CHECK(minlz_writer_create(
              &writer, test_write_cb, &encoded, &opts) == MINLZ_OK);
    CHECK(minlz_writer_write(
              writer, src, block_size + tiny, &written) == MINLZ_OK);
    CHECK(written == block_size + tiny);
    CHECK(minlz_writer_flush(writer) == MINLZ_OK);
    CHECK(minlz_writer_write(
              writer, src + block_size + tiny,
              src_len - block_size - tiny, &written) == MINLZ_OK);
    CHECK(written == src_len - block_size - tiny);
    CHECK(minlz_writer_close(writer) == MINLZ_OK);
    minlz_writer_destroy(writer);

    inline_read.data = encoded.data;
    inline_read.len = encoded.len;
    inline_read.pos = 0u;
    inline_read.read_at_calls = 0u;
    inline_read.read_at_bytes = 0u;
    CHECK(minlz_block_searcher_create(
              &block_searcher, test_read_cb, &inline_read) == MINLZ_OK);
    CHECK(minlz_block_searcher_search(
              block_searcher, pattern, sizeof(pattern) - 1u,
              test_search_offset_cb, &inline_offsets) == MINLZ_OK);
    minlz_block_searcher_destroy(block_searcher);

    main_read.data = encoded.data;
    main_read.len = encoded.len;
    main_read.pos = 0u;
    main_read.read_at_calls = 0u;
    main_read.read_at_bytes = 0u;
    sidecar_read.data = sidecar.data;
    sidecar_read.len = sidecar.len;
    sidecar_read.pos = 0u;
    sidecar_read.read_at_calls = 0u;
    sidecar_read.read_at_bytes = 0u;
    CHECK(minlz_sidecar_searcher_create(
              &sidecar_searcher,
              test_read_at_cb, &main_read,
              test_read_cb, &sidecar_read) == MINLZ_OK);
    CHECK(minlz_sidecar_searcher_search(
              sidecar_searcher, pattern, sizeof(pattern) - 1u,
              test_search_offset_cb, &sidecar_offsets) == MINLZ_OK);
    minlz_sidecar_searcher_destroy(sidecar_searcher);

    for (pos = 0u; pos + sizeof(pattern) - 1u <= src_len; pos++) {
        size_t i;
        int inline_found = 0;
        int sidecar_found = 0;
        if (memcmp(src + pos, pattern, sizeof(pattern) - 1u) != 0) {
            continue;
        }
        expected_count++;
        for (i = 0u; i < inline_offsets.count; i++) {
            inline_found |= inline_offsets.values[i] == (int64_t)pos;
        }
        for (i = 0u; i < sidecar_offsets.count; i++) {
            sidecar_found |= sidecar_offsets.values[i] == (int64_t)pos;
        }
        CHECK(inline_found);
        CHECK(sidecar_found);
    }
    CHECK(inline_offsets.count == expected_count);
    CHECK(sidecar_offsets.count == expected_count);

    free(sidecar.data);
    free(encoded.data);
    free(src);
    return 0;
}

static int test_search_prefix_only_tail(void) {
    static const uint8_t pattern[] = "co64";
    const size_t src_len = 9000u;
    const size_t match_offset = src_len - (sizeof(pattern) - 1u);
    uint8_t *src = (uint8_t *)malloc(src_len);
    minlz_search_config search_config;
    minlz_stream_options opts;
    test_mem_read read;
    test_mem_write encoded = {0};
    test_mem_write sidecar = {0};
    test_prefix_only_matches matches;
    minlz_block_searcher *block_searcher = NULL;
    minlz_sidecar_searcher *sidecar_searcher = NULL;
    minlz_search_stats stats;

    CHECK(src != NULL);
    memset(src, 'x', src_len);
    memcpy(src + match_offset, pattern, sizeof(pattern) - 1u);

    minlz_search_config_init(&search_config);
    CHECK(minlz_search_config_set_long_prefix(
              &search_config, pattern, sizeof(pattern) - 1u, 0u) ==
          MINLZ_OK);
    minlz_search_config_set_compression(&search_config, 0);
    minlz_stream_options_init(&opts);
    opts.block_size = MINLZ_MIN_BLOCK_SIZE;
    opts.level = MINLZ_LEVEL_FASTEST;
    opts.search = &search_config;

    read.data = src;
    read.len = src_len;
    read.pos = 0;
    CHECK(minlz_stream_encode(
              test_read_cb, &read, test_write_cb, &encoded, &opts) ==
          MINLZ_OK);

    matches.count = 0;
    matches.expected_offset = (int64_t)match_offset;
    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0;
    CHECK(minlz_block_searcher_create(
              &block_searcher, test_read_cb, &read) == MINLZ_OK);
    minlz_block_searcher_set_collect_stats(
        block_searcher, 1);
    CHECK(minlz_block_searcher_search(
              block_searcher, pattern, sizeof(pattern) - 1u,
              test_prefix_only_match_cb, &matches) == MINLZ_OK);
    CHECK(matches.count == 1);
    CHECK(minlz_block_searcher_stats(block_searcher, &stats) ==
          MINLZ_OK);
    CHECK(stats.blocks_total == 3u);
    CHECK(stats.blocks_searched >= 1u);
    CHECK(stats.matches == 1u);
    minlz_block_searcher_destroy(block_searcher);

    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0;
    CHECK(minlz_build_sidecar(
              test_write_cb, &sidecar,
              test_read_cb, &read,
              &search_config, 1u, 0) == MINLZ_OK);

    {
        test_mem_read main_read;
        test_mem_read sidecar_read;
        main_read.data = encoded.data;
        main_read.len = encoded.len;
        main_read.pos = 0;
        main_read.read_at_calls = 0u;
        main_read.read_at_bytes = 0u;
        sidecar_read.data = sidecar.data;
        sidecar_read.len = sidecar.len;
        sidecar_read.pos = 0;
        matches.count = 0;
        CHECK(minlz_sidecar_searcher_create(
                  &sidecar_searcher,
                  test_read_at_cb, &main_read,
                  test_read_cb, &sidecar_read) == MINLZ_OK);
        minlz_sidecar_searcher_set_collect_stats(
            sidecar_searcher, 1);
        CHECK(minlz_sidecar_searcher_search(
                  sidecar_searcher,
                  pattern, sizeof(pattern) - 1u,
                  test_prefix_only_match_cb, &matches) == MINLZ_OK);
        CHECK(matches.count == 1);
        CHECK(minlz_sidecar_searcher_stats(
                  sidecar_searcher, &stats) == MINLZ_OK);
        CHECK(stats.blocks_total == 3u);
        CHECK(stats.blocks_searched >= 1u);
        CHECK(stats.matches == 1u);
        minlz_sidecar_searcher_destroy(sidecar_searcher);
    }

    free(sidecar.data);
    free(encoded.data);
    free(src);
    return 0;
}

static int test_search_forward_api(void) {
    static const uint8_t pattern[] = "FORWARD_CONTEXT!";
    const size_t block_size = MINLZ_MIN_BLOCK_SIZE;
    uint8_t *src = (uint8_t *)malloc(block_size * 3u);
    minlz_search_config search_config;
    minlz_stream_options opts;
    test_mem_write encoded = {0};
    test_mem_read read;
    minlz_block_searcher *searcher = NULL;
    test_forward_matches matches;
    size_t eof_offset = 0u;
    size_t pos;

    CHECK(src != NULL);
    memset(src, 'a', block_size * 3u);
    memcpy(src + block_size - 20u,
           pattern, sizeof(pattern) - 1u);
    memcpy(src + block_size + 100u,
           pattern, sizeof(pattern) - 1u);

    minlz_search_config_init(&search_config);
    CHECK(minlz_search_config_set_match_len(
              &search_config, 4u) == MINLZ_OK);
    minlz_stream_options_init(&opts);
    opts.block_size = block_size;
    opts.level = MINLZ_LEVEL_FASTEST;
    opts.add_index = 0;
    opts.search = &search_config;
    read.data = src;
    read.len = block_size * 3u;
    read.pos = 0u;
    CHECK(minlz_stream_encode(
              test_read_cb, &read,
              test_write_cb, &encoded, &opts) == MINLZ_OK);

    matches.calls = 0;
    matches.expect_eof = 0;
    matches.expected_offset = (int64_t)block_size - 20;
    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0u;
    CHECK(minlz_block_searcher_create(
              &searcher, test_read_cb, &read) == MINLZ_OK);
    CHECK(minlz_block_searcher_search(
              searcher, pattern, sizeof(pattern) - 1u,
              test_forward_match_cb, &matches) == MINLZ_OK);
    CHECK(matches.calls == 2);
    minlz_block_searcher_destroy(searcher);
    searcher = NULL;
    CHECK(test_sidecar_forward_case(
              &encoded, &search_config,
              pattern, sizeof(pattern) - 1u,
              (int64_t)block_size - 20,
              0, 0) == 0);

    memset(src, 'b', block_size * 2u);
    memcpy(src + block_size * 2u - 20u,
           pattern, sizeof(pattern) - 1u);
    free(encoded.data);
    memset(&encoded, 0, sizeof(encoded));
    read.data = src;
    read.len = block_size * 2u;
    read.pos = 0u;
    CHECK(minlz_stream_encode(
              test_read_cb, &read,
              test_write_cb, &encoded, &opts) == MINLZ_OK);

    pos = 0u;
    while (pos + 4u <= encoded.len) {
        size_t chunk_len =
            (size_t)encoded.data[pos + 1u] |
            ((size_t)encoded.data[pos + 2u] << 8) |
            ((size_t)encoded.data[pos + 3u] << 16);
        CHECK(pos + 4u + chunk_len <= encoded.len);
        if (encoded.data[pos] == MINLZ_CHUNK_EOF) {
            eof_offset = pos;
            break;
        }
        pos += 4u + chunk_len;
    }
    CHECK(eof_offset != 0u);

    matches.calls = 0;
    matches.expect_eof = 1;
    matches.expected_offset = (int64_t)(block_size * 2u - 20u);
    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0u;
    CHECK(minlz_block_searcher_create(
              &searcher, test_read_cb, &read) == MINLZ_OK);
    CHECK(minlz_block_searcher_search(
              searcher, pattern, sizeof(pattern) - 1u,
              test_forward_match_cb, &matches) == MINLZ_OK);
    CHECK(matches.calls == 2);
    minlz_block_searcher_destroy(searcher);
    searcher = NULL;
    CHECK(test_sidecar_forward_case(
              &encoded, &search_config,
              pattern, sizeof(pattern) - 1u,
              (int64_t)(block_size * 2u - 20u),
              1, 0) == 0);

    /* Go's BlockSearcher also flushes deferred context at physical EOF. */
    matches.calls = 0;
    read.len = eof_offset;
    read.pos = 0u;
    CHECK(minlz_block_searcher_create(
              &searcher, test_read_cb, &read) == MINLZ_OK);
    CHECK(minlz_block_searcher_search(
              searcher, pattern, sizeof(pattern) - 1u,
              test_forward_match_cb, &matches) == MINLZ_OK);
    CHECK(matches.calls == 2);
    minlz_block_searcher_destroy(searcher);
    CHECK(test_sidecar_forward_case(
              &encoded, &search_config,
              pattern, sizeof(pattern) - 1u,
              (int64_t)(block_size * 2u - 20u),
              1, 1) == 0);

    free(encoded.data);
    free(src);
    return 0;
}

static int test_search_lazy_previous_api(void) {
    static const uint8_t pattern[] =
        ":LAZY_PREVIOUS_CONTEXT";
    static const uint8_t prefix[] = {':'};
    const size_t block_size = MINLZ_MIN_BLOCK_SIZE;
    uint8_t *src = (uint8_t *)malloc(block_size * 2u);
    minlz_search_config search_config;
    minlz_stream_options opts;
    test_mem_read read;
    test_mem_write encoded = {0};
    test_mem_write sidecar = {0};
    test_lazy_previous_matches matches;
    minlz_block_searcher *block_searcher = NULL;
    minlz_sidecar_searcher *sidecar_searcher = NULL;
    minlz_search_stats stats;

    CHECK(src != NULL);
    memset(src, 'A', block_size);
    memset(src + block_size, 'B', block_size);
    memcpy(src + block_size + 100u,
           pattern, sizeof(pattern) - 1u);

    minlz_search_config_init(&search_config);
    CHECK(minlz_search_config_set_match_len(
              &search_config, 4u) == MINLZ_OK);
    CHECK(minlz_search_config_set_byte_prefix(
              &search_config, prefix, sizeof(prefix)) == MINLZ_OK);
    minlz_search_config_set_compression(&search_config, 0);
    minlz_stream_options_init(&opts);
    opts.block_size = block_size;
    opts.level = MINLZ_LEVEL_FASTEST;
    opts.add_index = 0;
    opts.search = &search_config;

    read.data = src;
    read.len = block_size * 2u;
    read.pos = 0u;
    CHECK(minlz_stream_encode(
              test_read_cb, &read,
              test_write_cb, &encoded, &opts) == MINLZ_OK);

    matches.calls = 0;
    matches.expected_offset = (int64_t)block_size + 100;
    matches.expected_len = block_size;
    matches.expected_byte = 'A';
    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0u;
    CHECK(minlz_block_searcher_create(
              &block_searcher,
              test_read_cb, &read) == MINLZ_OK);
    minlz_block_searcher_set_collect_stats(block_searcher, 1);
    CHECK(minlz_block_searcher_search(
              block_searcher,
              pattern, sizeof(pattern) - 1u,
              test_lazy_previous_match_cb, &matches) == MINLZ_OK);
    CHECK(matches.calls == 1);
    CHECK(minlz_block_searcher_stats(
              block_searcher, &stats) == MINLZ_OK);
    CHECK(stats.blocks_skipped >= 1u);
    minlz_block_searcher_destroy(block_searcher);

    read.data = encoded.data;
    read.len = encoded.len;
    read.pos = 0u;
    CHECK(minlz_build_sidecar(
              test_write_cb, &sidecar,
              test_read_cb, &read,
              &search_config, 1u, 0) == MINLZ_OK);
    {
        test_mem_read main_read;
        test_mem_read sidecar_read;
        main_read.data = encoded.data;
        main_read.len = encoded.len;
        main_read.pos = 0u;
        main_read.read_at_calls = 0u;
        main_read.read_at_bytes = 0u;
        sidecar_read.data = sidecar.data;
        sidecar_read.len = sidecar.len;
        sidecar_read.pos = 0u;
        matches.calls = 0;
        CHECK(minlz_sidecar_searcher_create(
                  &sidecar_searcher,
                  test_read_at_cb, &main_read,
                  test_read_cb, &sidecar_read) == MINLZ_OK);
        minlz_sidecar_searcher_set_collect_stats(
            sidecar_searcher, 1);
        CHECK(minlz_sidecar_searcher_search(
                  sidecar_searcher,
                  pattern, sizeof(pattern) - 1u,
                  test_lazy_previous_match_cb,
                  &matches) == MINLZ_OK);
        CHECK(matches.calls == 1);
        CHECK(minlz_sidecar_searcher_stats(
                  sidecar_searcher, &stats) == MINLZ_OK);
        CHECK(stats.blocks_skipped >= 1u);
        minlz_sidecar_searcher_destroy(sidecar_searcher);
    }

    free(sidecar.data);
    free(encoded.data);
    free(src);
    return 0;
}

int main(void) {
    CHECK(test_backend_probe() == 0);
    CHECK(test_workspace_scratch_reuse() == 0);
    CHECK(test_varint_crc() == 0);
    CHECK(test_roundtrips() == 0);
    CHECK(test_balanced_16k_hash_loop() == 0);
    CHECK(test_max_block_roundtrip() == 0);
    CHECK(test_long_match_encode() == 0);
    CHECK(test_short_encode_buffer() == 0);
    CHECK(test_balanced_mixed_runs() == 0);
    CHECK(test_balanced_2mb_global_asm() == 0);
    CHECK(test_balanced_2mb_reference_boundaries() == 0);
    CHECK(test_superfast_small_repeat_size() == 0);
    CHECK(test_exact_tiny_matches() == 0);
    CHECK(test_manual_tags() == 0);
    CHECK(test_copy3_fast_margin() == 0);
    CHECK(test_fastest_2mb_natural() == 0);
    CHECK(test_fastest_2mb_structured() == 0);
    CHECK(test_corrupt() == 0);
    CHECK(test_index_api() == 0);
    CHECK(test_reader_writer_api() == 0);
    CHECK(test_readseeker_large_cache() == 0);
    CHECK(test_readseeker_mixed_cache() == 0);
    CHECK(test_search_table_crc() == 0);
    CHECK(test_search_tables_api() == 0);
    CHECK(test_search_three_block_straddle() == 0);
    CHECK(test_search_prefix_only_tail() == 0);
    CHECK(test_search_forward_api() == 0);
    CHECK(test_search_lazy_previous_api() == 0);
    return 0;
}
