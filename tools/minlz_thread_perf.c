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

typedef struct mem_read {
    const uint8_t *data;
    size_t len;
    size_t pos;
} mem_read;

typedef struct mem_write {
    uint8_t *data;
    size_t len;
    size_t cap;
} mem_write;

typedef struct discard_write {
    uint64_t total;
} discard_write;

static double now_seconds(void) {
#if defined(CLOCK_MONOTONIC)
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000.0;
#else
    return (double)clock() / (double)CLOCKS_PER_SEC;
#endif
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static double mbps(size_t bytes, int loops, double secs) {
    if (secs <= 0.0) {
        secs = 1e-9;
    }
    return ((double)bytes * (double)loops / (1024.0 * 1024.0)) / secs;
}

static void print_row(const char *kind, const char *name, int level, int threads,
                      size_t raw_len, size_t enc_len, double *samples, int sample_count) {
    qsort(samples, (size_t)sample_count, sizeof(samples[0]), cmp_double);
    printf("%s,%s,%d,%d,%zu,%zu,%.6f,%.2f,%.2f,%.2f\n",
           kind, name, level, threads, raw_len, enc_len,
           raw_len ? (double)enc_len / (double)raw_len : 0.0,
           samples[0], samples[sample_count / 2], samples[sample_count - 1]);
}

static minlz_status read_cb(void *ctx, uint8_t *dst, size_t cap, size_t *nread) {
    mem_read *r = (mem_read *)ctx;
    size_t n = r->len - r->pos;
    if (n > cap) {
        n = cap;
    }
    if (n) {
        memcpy(dst, r->data + r->pos, n);
        r->pos += n;
    }
    if (nread) {
        *nread = n;
    }
    return MINLZ_OK;
}

static minlz_status write_cb(void *ctx, const uint8_t *src, size_t len) {
    mem_write *w = (mem_write *)ctx;
    if (w->len + len > w->cap) {
        size_t next = w->cap ? w->cap * 2u : 65536u;
        uint8_t *p;
        while (next < w->len + len) {
            next *= 2u;
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

static minlz_status discard_write_cb(void *ctx, const uint8_t *src, size_t len) {
    discard_write *w = (discard_write *)ctx;
    (void)src;
    w->total += (uint64_t)len;
    return MINLZ_OK;
}

static int read_file(const char *path, uint8_t **data, size_t *len) {
    FILE *f = fopen(path, "rb");
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
    if (n < 0) {
        fclose(f);
        return 0;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)n ? (size_t)n : 1u);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if ((size_t)n != 0 && fread(buf, 1, (size_t)n, f) != (size_t)n) {
        free(buf);
        fclose(f);
        return 0;
    }
    fclose(f);
    *data = buf;
    *len = (size_t)n;
    return 1;
}

static int encode_stream_once(const uint8_t *data, size_t len, int level, mem_write *encoded) {
    minlz_stream_options opts;
    mem_read r;
    minlz_stream_options_init(&opts);
    opts.level = level;
    opts.block_size = MINLZ_MAX_BLOCK_SIZE;
    opts.add_index = 1;
    r.data = data;
    r.len = len;
    r.pos = 0;
    return minlz_stream_encode(read_cb, &r, write_cb, encoded, &opts) == MINLZ_OK;
}

static int bench_c_threads(const char *name, const uint8_t *data, size_t len,
                           int level, int threads, int loops, int samples) {
    mem_write encoded = {0};
    double *vals = (double *)calloc((size_t)samples, sizeof(vals[0]));
    int sample;
    if (!vals || !encode_stream_once(data, len, level, &encoded)) {
        free(vals);
        free(encoded.data);
        return 0;
    }
    {
        mem_read r = {encoded.data, encoded.len, 0};
        mem_write decoded = {0};
        minlz_reader *reader = NULL;
        uint64_t written = 0;
        minlz_status st = minlz_reader_create(&reader, read_cb, &r, NULL);
        if (st == MINLZ_OK) {
            st = minlz_reader_decode_concurrent(reader, write_cb, &decoded, threads, &written);
        }
        minlz_reader_destroy(reader);
        if (st != MINLZ_OK || written != len || decoded.len != len ||
            memcmp(decoded.data, data, len) != 0) {
            free(decoded.data);
            free(vals);
            free(encoded.data);
            return 0;
        }
        free(decoded.data);
    }
    for (sample = 0; sample < samples; sample++) {
        double start = now_seconds();
        int i;
        for (i = 0; i < loops; i++) {
            mem_read r = {encoded.data, encoded.len, 0};
            discard_write out = {0};
            minlz_reader *reader = NULL;
            uint64_t written = 0;
            minlz_status st = minlz_reader_create(&reader, read_cb, &r, NULL);
            if (st == MINLZ_OK) {
                st = minlz_reader_decode_concurrent(reader, discard_write_cb, &out,
                                                    threads, &written);
            }
            minlz_reader_destroy(reader);
            if (st != MINLZ_OK || written != len || out.total != len) {
                free(vals);
                free(encoded.data);
                return 0;
            }
        }
        vals[sample] = mbps(len, loops, now_seconds() - start);
    }
    print_row("c-stream-decode-concurrent", name, level, threads,
              len, encoded.len, vals, samples);
    free(vals);
    free(encoded.data);
    return 1;
}

static int bench_c_stream_seq(const char *name, const uint8_t *data, size_t len,
                              int level, int loops, int samples) {
    mem_write encoded = {0};
    double *vals = (double *)calloc((size_t)samples, sizeof(vals[0]));
    int sample;
    if (!vals || !encode_stream_once(data, len, level, &encoded)) {
        free(vals);
        free(encoded.data);
        return 0;
    }
    for (sample = 0; sample < samples; sample++) {
        double start = now_seconds();
        int i;
        for (i = 0; i < loops; i++) {
            mem_read r = {encoded.data, encoded.len, 0};
            discard_write out = {0};
            minlz_stream_options opts;
            minlz_stream_options_init(&opts);
            opts.block_size = MINLZ_MAX_BLOCK_SIZE;
            if (minlz_stream_decode(read_cb, &r, discard_write_cb, &out, &opts) != MINLZ_OK ||
                out.total != len) {
                free(vals);
                free(encoded.data);
                return 0;
            }
        }
        vals[sample] = mbps(len, loops, now_seconds() - start);
    }
    print_row("c-stream-decode-seq", name, level, 1, len, encoded.len, vals, samples);
    free(vals);
    free(encoded.data);
    return 1;
}

int main(int argc, char **argv) {
    const char *path;
    const char *name;
    uint8_t *data = NULL;
    size_t len = 0;
    int level;
    int loops;
    int samples;
    int threads;
    if (argc < 7) {
        fprintf(stderr,
                "usage: minlz_thread_perf <input> <name> <level> <threads> <loops> <samples>\n");
        return 2;
    }
    path = argv[1];
    name = argv[2];
    level = atoi(argv[3]);
    threads = atoi(argv[4]);
    loops = atoi(argv[5]);
    samples = atoi(argv[6]);
    if (threads <= 0 || loops <= 0 || samples <= 0) {
        return 2;
    }
    if (!read_file(path, &data, &len)) {
        fprintf(stderr, "unable to read %s\n", path);
        return 2;
    }
    printf("kind,corpus,level,threads,raw_len,encoded_len,ratio,min_mbps,median_mbps,max_mbps\n");
    if (!bench_c_stream_seq(name, data, len, level, loops, samples) ||
        !bench_c_threads(name, data, len, level, threads, loops, samples)) {
        free(data);
        return 1;
    }
    free(data);
    return 0;
}
