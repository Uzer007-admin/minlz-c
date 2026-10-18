/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static int read_all(const char *name, uint8_t **out, size_t *out_len, size_t max_len) {
    FILE *f = fopen(name, "rb");
    long n;
    uint8_t *buf;
    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    n = ftell(f);
    if (n < 0 || (max_len != 0 && (size_t)n > max_len)) {
        fclose(f);
        return 0;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)n ? (size_t)n : 1);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *out = buf;
    *out_len = (size_t)n;
    return 1;
}

static double elapsed(clock_t a, clock_t b) {
    return (double)(b - a) / (double)CLOCKS_PER_SEC;
}

static int bench_current(const uint8_t *src, size_t src_len, int loops,
                         uint8_t **encoded, size_t *encoded_len) {
    size_t cap = minlz_max_encoded_len(src_len);
    uint8_t *enc = (uint8_t *)malloc(cap ? cap : 1);
    uint8_t *dec = (uint8_t *)malloc(src_len ? src_len : 1);
    minlz_workspace *ws = NULL;
    clock_t a;
    clock_t b;
    int i;
    if (!enc || !dec || minlz_workspace_create(&ws) != MINLZ_OK) {
        free(enc);
        free(dec);
        return 0;
    }
    a = clock();
    for (i = 0; i < loops; i++) {
        minlz_status st = minlz_encode_ws(ws, enc, cap, encoded_len, src, src_len, MINLZ_LEVEL_FASTEST);
        if (st != MINLZ_OK) {
            fprintf(stderr, "current encode failed: %s\n", minlz_status_string(st));
            free(enc);
            free(dec);
            return 0;
        }
    }
    b = clock();
    printf("current encode: %.2f MB/s, ratio %.6f, bytes %zu\n",
           ((double)src_len * loops / (1024.0 * 1024.0)) / elapsed(a, b),
           src_len ? (double)*encoded_len / (double)src_len : 0.0,
           *encoded_len);

    a = clock();
    for (i = 0; i < loops; i++) {
        size_t dec_len = src_len;
        minlz_status st = minlz_decode_ws(ws, dec, src_len, &dec_len, enc, *encoded_len);
        if (st != MINLZ_OK || dec_len != src_len || memcmp(dec, src, src_len) != 0) {
            fprintf(stderr, "current decode failed\n");
            free(enc);
            free(dec);
            return 0;
        }
    }
    b = clock();
    printf("current decode(current block): %.2f MB/s\n",
           ((double)src_len * loops / (1024.0 * 1024.0)) / elapsed(a, b));
    free(dec);
    minlz_workspace_destroy(ws);
    *encoded = enc;
    return 1;
}

static int bench_external_block(const uint8_t *src, size_t src_len,
                                const uint8_t *block, size_t block_len, int loops) {
    uint8_t *dec = (uint8_t *)malloc(src_len ? src_len : 1);
    minlz_workspace *ws = NULL;
    clock_t a;
    clock_t b;
    int i;
    if (!dec || minlz_workspace_create(&ws) != MINLZ_OK) {
        free(dec);
        return 0;
    }
    a = clock();
    for (i = 0; i < loops; i++) {
        size_t dec_len = src_len;
        minlz_status st = minlz_decode_ws(ws, dec, src_len, &dec_len, block, block_len);
        if (st != MINLZ_OK || dec_len != src_len || memcmp(dec, src, src_len) != 0) {
            fprintf(stderr, "external block is not compatible with current decoder\n");
            free(dec);
            return 0;
        }
    }
    b = clock();
    printf("current decode(external block): %.2f MB/s, external bytes %zu, ratio %.6f\n",
           ((double)src_len * loops / (1024.0 * 1024.0)) / elapsed(a, b),
           block_len,
           src_len ? (double)block_len / (double)src_len : 0.0);
    free(dec);
    minlz_workspace_destroy(ws);
    return 1;
}

int main(int argc, char **argv) {
    uint8_t *src = NULL;
    uint8_t *cur = NULL;
    uint8_t *old = NULL;
    size_t src_len = 0;
    size_t cur_len = 0;
    size_t old_len = 0;
    int loops = 100;
    int ok;

    if (argc < 2 || argc > 4) {
        fprintf(stderr, "usage: %s <raw-input <=8MiB> [external-old-block.mzb] [loops]\n", argv[0]);
        return 1;
    }
    if (argc == 3) {
        loops = atoi(argv[2]);
    } else if (argc == 4) {
        loops = atoi(argv[3]);
    }
    if (loops <= 0) {
        loops = 1;
    }
    if (!read_all(argv[1], &src, &src_len, MINLZ_MAX_BLOCK_SIZE)) {
        fprintf(stderr, "unable to read raw input or input is larger than 8MiB\n");
        return 2;
    }
    ok = bench_current(src, src_len, loops, &cur, &cur_len);
    if (ok && argc == 4) {
        if (!read_all(argv[2], &old, &old_len, minlz_max_encoded_len(MINLZ_MAX_BLOCK_SIZE))) {
            fprintf(stderr, "unable to read external old block\n");
            ok = 0;
        } else {
            ok = bench_external_block(src, src_len, old, old_len, loops);
        }
    }
    free(src);
    free(cur);
    free(old);
    return ok ? 0 : 2;
}
