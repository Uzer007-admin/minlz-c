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

static int read_all(const char *name, uint8_t **out, size_t *out_len) {
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
    if (n < 0 || (size_t)n > MINLZ_MAX_BLOCK_SIZE) {
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

static double secs(clock_t a, clock_t b) {
    return (double)(b - a) / (double)CLOCKS_PER_SEC;
}

int main(int argc, char **argv) {
    const char *name;
    int loops = 100;
    uint8_t *src = NULL;
    uint8_t *enc = NULL;
    uint8_t *dec = NULL;
    minlz_workspace *ws = NULL;
    size_t src_len = 0;
    size_t enc_len = 0;
    size_t dec_len = 0;
    size_t cap;
    int i;
    clock_t a;
    clock_t b;

    if (argc < 2 || argc > 3) {
        fprintf(stderr, "usage: %s <file <=8MiB> [loops]\n", argv[0]);
        return 1;
    }
    name = argv[1];
    if (argc == 3) {
        loops = atoi(argv[2]);
        if (loops <= 0) {
            loops = 1;
        }
    }
    if (!read_all(name, &src, &src_len)) {
        fprintf(stderr, "unable to read %s or file is larger than 8MiB\n", name);
        return 2;
    }
    cap = minlz_max_encoded_len(src_len);
    enc = (uint8_t *)malloc(cap ? cap : 1);
    dec = (uint8_t *)malloc(src_len ? src_len : 1);
    if (!enc || !dec || minlz_workspace_create(&ws) != MINLZ_OK) {
        return 2;
    }

    a = clock();
    for (i = 0; i < loops; i++) {
        minlz_status st = minlz_encode_ws(ws, enc, cap, &enc_len, src, src_len, MINLZ_LEVEL_BALANCED);
        if (st != MINLZ_OK) {
            fprintf(stderr, "encode: %s\n", minlz_status_string(st));
            return 2;
        }
    }
    b = clock();
    printf("encode: %.2f MB/s, ratio %.3f\n",
           ((double)src_len * loops / (1024.0 * 1024.0)) / secs(a, b),
           src_len ? (double)enc_len / (double)src_len : 0.0);

    a = clock();
    for (i = 0; i < loops; i++) {
        dec_len = src_len;
        if (minlz_decode_ws(ws, dec, src_len, &dec_len, enc, enc_len) != MINLZ_OK ||
            dec_len != src_len || memcmp(dec, src, src_len) != 0) {
            fprintf(stderr, "decode failed\n");
            return 2;
        }
    }
    b = clock();
    printf("decode: %.2f MB/s\n",
           ((double)src_len * loops / (1024.0 * 1024.0)) / secs(a, b));
    free(src);
    free(enc);
    free(dec);
    minlz_workspace_destroy(ws);
    return 0;
}
