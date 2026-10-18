/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#if defined(_WIN32)
#include <direct.h>
#include <windows.h>
#define minlz_getcwd _getcwd
#else
#include <unistd.h>
#define minlz_getcwd getcwd
#endif

#ifndef MINLZ_ENABLE_FASTEST64K_INLINE_PROBE
#define MINLZ_ENABLE_FASTEST64K_INLINE_PROBE 0
#endif

#ifndef MINLZ_ENABLE_L1_FAST16K_INLINE_ASM
#define MINLZ_ENABLE_L1_FAST16K_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM
#define MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM MINLZ_ENABLE_L1_FAST16K_INLINE_ASM
#endif

#ifndef MINLZ_ENABLE_L1_PERIODIC_FAST16K_INLINE
#define MINLZ_ENABLE_L1_PERIODIC_FAST16K_INLINE 1
#endif

#ifndef MINLZ_ENABLE_L1_FAST64K_INLINE_ASM
#define MINLZ_ENABLE_L1_FAST64K_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM
#define MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM MINLZ_ENABLE_L1_FAST64K_INLINE_ASM
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_GOASM
#define MINLZ_ENABLE_BETTER2MB_GOASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED
#define MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED 1
#endif

#ifndef MINLZ_INDEX_FIND_INNER_LOOPS
#define MINLZ_INDEX_FIND_INNER_LOOPS 2048
#endif

#ifndef MINLZ_SMALL_AUTO_LOOPS
#define MINLZ_SMALL_AUTO_LOOPS 20000
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_INLINE_ASM
#define MINLZ_ENABLE_BETTER2MB_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM
#define MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM 1
#endif

#ifndef MINLZ_ENCODE_WS_SPLIT_BALANCED2MB
#define MINLZ_ENCODE_WS_SPLIT_BALANCED2MB 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM
#define MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER
#define MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER 0
#endif

#ifndef MINLZ_ENABLE_L2_PERIODIC_REPEAT_FAST
#define MINLZ_ENABLE_L2_PERIODIC_REPEAT_FAST 1
#endif

#ifndef MINLZ_ENABLE_L2_PERIODIC_REPEAT_REGREWRITE
#define MINLZ_ENABLE_L2_PERIODIC_REPEAT_REGREWRITE 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_REGREWRITE_ASM
#define MINLZ_ENABLE_BETTER2MB_REGREWRITE_ASM 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM
#define MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM 1
#endif

#ifndef MINLZ_BETTER2MB_TABLE_RBP
#define MINLZ_BETTER2MB_TABLE_RBP 1
#endif

#ifndef MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH
#define MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH 0
#endif

#ifndef MINLZ_BETTER2MB_EXTERNAL_MEMSET
#define MINLZ_BETTER2MB_EXTERNAL_MEMSET 1
#endif

#ifndef MINLZ_BETTER2MB_HASH_CONST_XMM
#define MINLZ_BETTER2MB_HASH_CONST_XMM 0
#endif

#ifndef MINLZ_BETTER2MB_HASH_CONST_RBP
#define MINLZ_BETTER2MB_HASH_CONST_RBP 0
#endif

#ifndef MINLZ_BETTER2MB_HASH_CONST_STACK
#define MINLZ_BETTER2MB_HASH_CONST_STACK 0
#endif

#ifndef MINLZ_ENCODE_WS_SPLIT_RAW_ALL
#define MINLZ_ENCODE_WS_SPLIT_RAW_ALL 1
#endif

#ifndef MINLZ_DECODE_RAW_MID_MEMCPY_NONOVERLAP
#define MINLZ_DECODE_RAW_MID_MEMCPY_NONOVERLAP 1
#endif

#ifndef MINLZ_ENABLE_STRUCTURED_L2_DECODE_FAST
#define MINLZ_ENABLE_STRUCTURED_L2_DECODE_FAST 0
#endif

#ifndef MINLZ_ENABLE_L1_2MB_GOASM
#define MINLZ_ENABLE_L1_2MB_GOASM 1
#endif

#ifndef MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
#define MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L1_2MB_JSON_INLINE_ASM
#define MINLZ_ENABLE_L1_2MB_JSON_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L1_8M_GOASM
#define MINLZ_ENABLE_L1_8M_GOASM 1
#endif

#ifndef MINLZ_ENABLE_L2_BETTER16K_INLINE_ASM
#define MINLZ_ENABLE_L2_BETTER16K_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L2_16K_GENERIC_FAST
#define MINLZ_ENABLE_L2_16K_GENERIC_FAST 0
#endif

#ifndef MINLZ_ENABLE_BETTER16K_INLINE_ASM
#define MINLZ_ENABLE_BETTER16K_INLINE_ASM MINLZ_ENABLE_L2_BETTER16K_INLINE_ASM
#endif

#ifndef MINLZ_BETTER16K_HASH2_32BIT
#define MINLZ_BETTER16K_HASH2_32BIT 1
#endif

#ifndef MINLZ_BETTER16K_REUSE_HASH1
#define MINLZ_BETTER16K_REUSE_HASH1 1
#endif

typedef struct corpus {
    const char *name;
    uint8_t *data;
    size_t len;
} corpus;

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

typedef struct go_oracle_options {
    int enabled;
    int strict_size;
    int speed;
    int candidate_gate;
    int go_first;
} go_oracle_options;

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

static void print_perf_row(const char *kind, const char *name, int level, size_t raw_len,
                           size_t enc_len, double ratio, double *samples, int sample_count) {
    qsort(samples, (size_t)sample_count, sizeof(samples[0]), cmp_double);
    printf("%s,%s,%d,%zu,%zu,%.6f,%.2f,%.2f,%.2f\n",
           kind, name, level, raw_len, enc_len, ratio,
           samples[0], samples[sample_count / 2], samples[sample_count - 1]);
}

static double perf_now(void) {
#if defined(_WIN32)
    LARGE_INTEGER counter;
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)frequency.QuadPart;
#else
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (double)now.tv_sec + (double)now.tv_nsec * 1e-9;
#endif
}

static double elapsed(double a, double b) {
    double secs = b - a;
    if (secs <= 0.0) {
        secs = 1e-9;
    }
    return secs;
}

static uint32_t load32le(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int make_small_pattern(corpus *c, const char *name, size_t n) {
    size_t i;
    static const char pat[] = "MinLZ-small-latency-pattern:";
    c->data = (uint8_t *)malloc(n ? n : 1);
    if (!c->data) {
        return 0;
    }
    c->name = name;
    c->len = n;
    for (i = 0; i < n; i++) {
        c->data[i] = (uint8_t)(pat[i % (sizeof(pat) - 1)] + (uint8_t)(i >> 5));
    }
    return 1;
}

static minlz_status read_cb(void *ctx, uint8_t *dst, size_t cap, size_t *nread) {
    mem_read *r = (mem_read *)ctx;
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

static minlz_status seek_cb(void *ctx, int64_t offset, int whence, int64_t *new_pos) {
    mem_read *r = (mem_read *)ctx;
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

static minlz_status write_cb(void *ctx, const uint8_t *src, size_t len) {
    mem_write *w = (mem_write *)ctx;
    if (w->len + len > w->cap) {
        size_t next = w->cap ? w->cap * 2 : 65536;
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
    memcpy(w->data + w->len, src, len);
    w->len += len;
    return MINLZ_OK;
}

static int read_file(const char *name, uint8_t **out, size_t *out_len) {
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
    if (n <= 0) {
        fclose(f);
        return 0;
    }
    rewind(f);
    buf = (uint8_t *)malloc((size_t)n);
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

static int write_file(const char *name, const uint8_t *src, size_t src_len) {
    FILE *f = fopen(name, "wb");
    if (!f) {
        return 0;
    }
    if (src_len && fwrite(src, 1, src_len, f) != src_len) {
        fclose(f);
        return 0;
    }
    return fclose(f) == 0;
}

static int make_temp_path(char *dst, size_t cap, const char *name) {
    char cwd[1024];
    const char *base;
    size_t base_len;
    int n;
    if (!dst || !name || cap == 0) {
        return 0;
    }
    if (name[0] == '/' || name[0] == '\\' ||
        (strlen(name) > 2 && name[1] == ':' && (name[2] == '\\' || name[2] == '/'))) {
        n = snprintf(dst, cap, "%s", name);
        return n > 0 && (size_t)n < cap;
    }
    base = getenv("MINLZ_PERF_TMPDIR");
#if defined(_WIN32)
    if (!base || !*base) {
        base = getenv("TEMP");
    }
    if (!base || !*base) {
        base = getenv("TMP");
    }
#else
    if (!base || !*base) {
        base = getenv("TMPDIR");
    }
#endif
    if (base && *base) {
        base_len = strlen(base);
        n = snprintf(dst, cap, "%s%s%s", base,
                     (base_len && base[base_len - 1] != '/' && base[base_len - 1] != '\\') ?
                         "/" : "",
                     name);
        return n > 0 && (size_t)n < cap;
    }
    if (!minlz_getcwd(cwd, sizeof(cwd))) {
        return 0;
    }
    n = snprintf(dst, cap, "%s/%s", cwd, name);
    return n > 0 && (size_t)n < cap;
}

static int build_go_compat_cmd(char *cmd, size_t cap, const char *args) {
    const char *bin = getenv("MINLZ_GO_COMPAT_BIN");
    const char *repo = getenv("MINLZ_GO_REPO");
    const char *compat = getenv("MINLZ_GO_COMPAT");
    int n;
    if (!compat || !*compat) {
        compat = "./compat/minlz-go-compat/main.go";
    }
    if (bin && *bin) {
#if defined(_WIN32)
        /* cmd.exe needs outer quotes around a quoted executable and its args. */
        n = snprintf(cmd, cap, "\"\"%s\" %s\"", bin, args);
#else
        n = snprintf(cmd, cap, "\"%s\" %s", bin, args);
#endif
        return n > 0 && (size_t)n < cap;
    }
    if (repo && *repo) {
        n = snprintf(cmd, cap, "cd \"%s\" && go run \"%s\" %s", repo, compat, args);
        return n > 0 && (size_t)n < cap;
    }
    n = snprintf(cmd, cap, "go run \"%s\" %s", compat, args);
    return n > 0 && (size_t)n < cap;
}

static const char *backend_label(void) {
    const char *force = getenv("MINLZ_FORCE_CPU");
    if (force && *force) {
        return force;
    }
    return "default";
}

static const char *candidate_path_label(const corpus *c, int level, size_t c_enc_len) {
    if (c_enc_len >= c->len) {
        return "uncompressed-fallback";
    }
    if (c->len > (2u << 20) && strstr(c->name, "repeat") != NULL) {
        if (level == MINLZ_LEVEL_SUPERFAST) {
            return "xfast-periodic-repeat";
        }
        if (level == MINLZ_LEVEL_FASTEST) {
#if MINLZ_ENABLE_L1_PERIODIC_FAST16K_INLINE
            return "l1-periodic-repeat-fast16k-inline";
#else
            return "l1-periodic-repeat";
#endif
        }
        if (level == MINLZ_LEVEL_BALANCED) {
#if MINLZ_ENABLE_L2_PERIODIC_REPEAT_REGREWRITE
            return "l2-periodic-repeat-regrewrite";
#elif MINLZ_ENABLE_L2_PERIODIC_REPEAT_FAST
            return "l2-periodic-repeat-fast";
#else
            return "l2-periodic-repeat";
#endif
        }
    }
    if (level == MINLZ_LEVEL_FASTEST) {
        if (c->len <= 1024u) {
            return "l1-fastest-1k-c";
        }
        if (c->len <= 4096u) {
            return "l1-fastest-4k-c";
        }
        if (c->len <= 16384u) {
#if MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM
            return "l1-block16k-inline-asm";
#else
#if MINLZ_ENABLE_FASTEST64K_INLINE_PROBE
            return "l1-fastest-16k-inline-probe";
#else
            return "l1-fastest-16k-c";
#endif
#endif
        }
        if (c->len <= 65536u) {
#if MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM
            return "l1-block64k-inline-asm";
#else
#if MINLZ_ENABLE_FASTEST64K_INLINE_PROBE
            return "l1-fastest-64k-inline-probe";
#else
            return "l1-fastest-64k-c";
#endif
#endif
        }
        if (c->len <= (2u << 20)) {
#if MINLZ_ENABLE_L1_2MB_JSON_INLINE_ASM && MINLZ_ENABLE_L1_2MB_GOASM && \
    MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
            if (c->len > (512u << 10) && minlz_backend_has_inline_asm_amd64() &&
                (c->data[0] == '{' || c->data[0] == '[') &&
                (c->data[1] == '"' || c->data[1] == '{' ||
                 c->data[1] == '[' || c->data[1] == '\n')) {
                return "l1-block2mb-json-inline-asm";
            }
#endif
#if MINLZ_ENABLE_L1_2MB_GOASM
            if (strstr(c->name, "twain") != NULL) {
#if MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
                return "l1-block2mb-inline-asm";
#else
                return "l1-block2mb-natural-goasm-call";
#endif
            }
#endif
            return "l1-fastest-2mb-c-inline";
        }
#if MINLZ_ENABLE_L1_8M_GOASM
        if (strstr(c->name, "twain") != NULL) {
            return "l1-block8m-goasm-call";
        }
#endif
        return "l1-fastest-8m-c-inline";
    }
    if (level == MINLZ_LEVEL_BALANCED) {
        if (c->len <= 1024u) {
            return "l2-better-1k-goasm-call";
        }
        if (c->len <= 4096u) {
            return "l2-better-4k-goasm-call";
        }
        if (c->len <= 16384u) {
            if (!minlz_backend_has_inline_asm_amd64()) {
                return "l2-balanced-16k-c-fallback";
            }
#if MINLZ_ENABLE_L2_16K_GENERIC_FAST
            return "l2-balanced-16k-generic-fast";
#else
#if MINLZ_ENABLE_BETTER16K_INLINE_ASM
#if MINLZ_BETTER16K_HASH2_32BIT && MINLZ_BETTER16K_REUSE_HASH1
            return "l2-better16k-hash-reuse-inline";
#else
            return "l2-better16k-regrewrite";
#endif
#else
            return "l2-better-16k-goasm-call";
#endif
#endif
        }
        if (c->len <= 65536u) {
            return "l2-better-64k-goasm-call";
        }
        if (c->len <= (512u << 10)) {
            return "l2-better-512k-goasm-call";
        }
        if (c->len <= (2u << 20)) {
#if MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER
            return "l2-better-2mb-structured-smaller-decode-asm";
#elif MINLZ_ENCODE_WS_SPLIT_BALANCED2MB && !MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM
            return "l2-better-2mb-goasm-call";
#elif MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED
#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM
#if MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM
#if MINLZ_BETTER2MB_TABLE_RBP
#if MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH && !defined(_WIN32)
            return "l2-better-2mb-folded-search-asm";
#else
            return "l2-better-2mb-exact-regrewrite-v2-table-rbp";
#endif
#elif MINLZ_BETTER2MB_HASH_CONST_STACK
            return "l2-better-2mb-exact-regrewrite-v2-stackhash";
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
            return "l2-better-2mb-exact-regrewrite-v2-rbphash";
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
            return "l2-better-2mb-exact-regrewrite-v2-xmmhash";
#else
            return "l2-better-2mb-exact-regrewrite-v2";
#endif
#elif MINLZ_ENABLE_BETTER2MB_REGREWRITE_ASM
            return "l2-better-2mb-exact-regrewrite";
#elif MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM
            return "l2-better-2mb-structured-global";
#elif MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM
            return "l2-better-2mb-exact-inline";
#else
            return "l2-better-2mb-natural-goasm-structured-inline";
#endif
#else
            return "l2-better-2mb-goasm-call";
#endif
#else
            if (strstr(c->name, "logjson") != NULL) {
#if MINLZ_ENABLE_STRUCTURED_L2_DECODE_FAST
                return "l2-better-2mb-structured-smaller-decode-fast";
#else
                return "l2-better-2mb-structured-inline";
#endif
            }
#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM
            return "l2-better-2mb-inline-asm-or-structured";
#else
            return "l2-better-2mb-goasm-call-or-structured";
#endif
#endif
        }
        return "l2-better-8m-goasm-call";
    }
    return "unknown";
}

static void print_candidate_gate(const minlz_workspace *ws, const corpus *c, int level, size_t c_enc_len,
                                 size_t go_enc_len, double c_median_mbps, int speed_requested) {
    const int size_ok = c_enc_len <= go_enc_len;
    fprintf(stderr,
            "# candidate-gate corpus=%s level=%d path=%s backend=%s c_size=%zu go_size=%zu "
            "c_median_mbps=%.2f size_gate=%s speed_gate=%s reason=%s "
            "workspace_hint=%u scratch_capacity=%zu\n",
            c->name, level, candidate_path_label(c, level, c_enc_len), backend_label(),
            c_enc_len, go_enc_len, c_median_mbps, size_ok ? "pass" : "fail",
            speed_requested ? "see-go-block-encode-row" : "not-requested",
            size_ok ? "size-ok" : "c-output-larger-than-go",
            ws ? (unsigned)ws->encode_scratch_hint : 0u,
            ws ? ws->buffer_caps[2] : 0u);
}

static int go_block_oracle(minlz_workspace *ws, const corpus *c, int level,
                           const uint8_t *c_enc, size_t c_enc_len, int strict_size,
                           int speed, int loops, int samples,
                           int candidate_gate, double c_median_mbps) {
    static unsigned oracle_seq = 0;
    char in_name[512];
    char out_name[512];
    char c_name[512];
    char c_dec_name[512];
    char rel_in[96];
    char rel_out[96];
    char rel_c[96];
    char rel_c_dec[96];
    char args[2048];
    char cmd[4096];
    uint8_t *go_enc = NULL;
    uint8_t *go_dec = NULL;
    uint8_t *dec = NULL;
    size_t go_enc_len = 0;
    size_t go_dec_len = 0;
    size_t dec_len;
    int ok = 0;
    unsigned seq = ++oracle_seq;

    snprintf(rel_in, sizeof(rel_in), "minlz_perf_go_input_%u_%d.bin", seq, level);
    snprintf(rel_out, sizeof(rel_out), "minlz_perf_go_output_%u_%d.mzb", seq, level);
    snprintf(rel_c, sizeof(rel_c), "minlz_perf_c_output_%u_%d.mzb", seq, level);
    snprintf(rel_c_dec, sizeof(rel_c_dec), "minlz_perf_c_decoded_%u_%d.bin", seq, level);
    if (!make_temp_path(in_name, sizeof(in_name), rel_in) ||
        !make_temp_path(out_name, sizeof(out_name), rel_out) ||
        !make_temp_path(c_name, sizeof(c_name), rel_c) ||
        !make_temp_path(c_dec_name, sizeof(c_dec_name), rel_c_dec)) {
        fprintf(stderr, "go oracle: unable to create temporary paths\n");
        return 0;
    }

    if (!write_file(in_name, c->data, c->len)) {
        fprintf(stderr, "go oracle: unable to write %s\n", in_name);
        return 0;
    }
    snprintf(args, sizeof(args), "encode-block \"%s\" \"%s\" %d",
             in_name, out_name, level);
    if (!build_go_compat_cmd(cmd, sizeof(cmd), args)) {
        fprintf(stderr, "go oracle: unable to build Go command\n");
        remove(in_name);
        return 0;
    }
    if (system(cmd) != 0) {
        fprintf(stderr, "go oracle: command failed: %s\n", cmd);
        remove(in_name);
        remove(out_name);
        return 0;
    }
    if (!read_file(out_name, &go_enc, &go_enc_len)) {
        fprintf(stderr, "go oracle: unable to read %s\n", out_name);
        goto done;
    }
    dec = (uint8_t *)malloc(c->len ? c->len : 1);
    if (!dec) {
        goto done;
    }
    dec_len = c->len;
    if (minlz_decode_ws(ws, dec, c->len, &dec_len, go_enc, go_enc_len) != MINLZ_OK ||
        dec_len != c->len || memcmp(dec, c->data, c->len) != 0) {
        fprintf(stderr, "go oracle: C failed to decode Go block for %s level %d\n",
                c->name, level);
        goto done;
    }
    if (!write_file(c_name, c_enc, c_enc_len)) {
        fprintf(stderr, "go oracle: unable to write C block for %s level %d\n",
                c->name, level);
        goto done;
    }
    snprintf(args, sizeof(args), "decode-block \"%s\" \"%s\"", c_name, c_dec_name);
    if (!build_go_compat_cmd(cmd, sizeof(cmd), args) || system(cmd) != 0 ||
        !read_file(c_dec_name, &go_dec, &go_dec_len) ||
        go_dec_len != c->len || memcmp(go_dec, c->data, c->len) != 0) {
        fprintf(stderr, "go oracle: Go failed to decode C block for %s level %d\n",
                c->name, level);
        goto done;
    }
    printf("go-block-size,%s,%d,%zu,%zu,%.6f,0.00,0.00,0.00\n",
           c->name, level, c->len, go_enc_len,
           c->len ? (double)go_enc_len / (double)c->len : 0.0);
    fflush(stdout);
    if (candidate_gate && c_median_mbps > 0.0) {
        print_candidate_gate(ws, c, level, c_enc_len, go_enc_len, c_median_mbps, speed);
    }
    if (strict_size && c_enc_len > go_enc_len) {
        fprintf(stderr,
                "ratio gate failed: C block %s level %d is %zu bytes, Go is %zu bytes\n",
                c->name, level, c_enc_len, go_enc_len);
        goto done;
    }
    if (speed) {
        snprintf(args, sizeof(args), "bench-block \"%s\" %s %d %d %d",
                 in_name, c->name, level, loops, samples);
        if (!build_go_compat_cmd(cmd, sizeof(cmd), args)) {
            fprintf(stderr, "go oracle: unable to build Go benchmark command\n");
            goto done;
        }
        fflush(stdout);
        if (system(cmd) != 0) {
            fprintf(stderr, "go oracle: benchmark command failed: %s\n", cmd);
            goto done;
        }
    }
    ok = 1;

done:
    free(go_enc);
    free(go_dec);
    free(dec);
    remove(in_name);
    remove(out_name);
    remove(c_name);
    remove(c_dec_name);
    return ok;
}

static int go_stream_oracle(minlz_workspace *ws, const corpus *c, int level,
                            size_t c_enc_len, int strict_size,
                            int speed, int loops, int samples) {
    static unsigned oracle_seq = 0;
    char in_name[512];
    char out_name[512];
    char rel_in[96];
    char rel_out[96];
    char args[2048];
    char cmd[4096];
    uint8_t *go_enc = NULL;
    size_t go_enc_len = 0;
    int ok = 0;
    unsigned seq = ++oracle_seq;

    snprintf(rel_in, sizeof(rel_in), "minlz_perf_go_stream_input_%u_%d.bin", seq, level);
    snprintf(rel_out, sizeof(rel_out), "minlz_perf_go_stream_output_%u_%d.mz", seq, level);
    if (!make_temp_path(in_name, sizeof(in_name), rel_in) ||
        !make_temp_path(out_name, sizeof(out_name), rel_out)) {
        fprintf(stderr, "go stream oracle: unable to create temporary paths\n");
        return 0;
    }
    if (!write_file(in_name, c->data, c->len)) {
        fprintf(stderr, "go stream oracle: unable to write %s\n", in_name);
        return 0;
    }
    snprintf(args, sizeof(args), "encode-stream \"%s\" \"%s\" %d",
             in_name, out_name, level);
    if (!build_go_compat_cmd(cmd, sizeof(cmd), args)) {
        fprintf(stderr, "go stream oracle: unable to build Go command\n");
        remove(in_name);
        return 0;
    }
    if (system(cmd) != 0) {
        fprintf(stderr, "go stream oracle: command failed: %s\n", cmd);
        remove(in_name);
        remove(out_name);
        return 0;
    }
    if (!read_file(out_name, &go_enc, &go_enc_len)) {
        fprintf(stderr, "go stream oracle: unable to read %s\n", out_name);
        goto done;
    }
    {
        mem_read r = {go_enc, go_enc_len, 0};
        mem_write decoded = {0};
        minlz_stream_options opts;
        minlz_stream_options_init(&opts);
        opts.block_size = MINLZ_MAX_BLOCK_SIZE;
        opts.allow_fallback = 1;
        if (minlz_stream_decode_ws(ws, read_cb, &r, write_cb, &decoded, &opts) != MINLZ_OK ||
            decoded.len != c->len || memcmp(decoded.data, c->data, c->len) != 0) {
            free(decoded.data);
            fprintf(stderr, "go stream oracle: C failed to decode Go stream for %s level %d\n",
                    c->name, level);
            goto done;
        }
        free(decoded.data);
    }
    printf("go-stream-size,%s,%d,%zu,%zu,%.6f,0.00,0.00,0.00\n",
           c->name, level, c->len, go_enc_len,
           c->len ? (double)go_enc_len / (double)c->len : 0.0);
    fflush(stdout);
    if (strict_size && c_enc_len > go_enc_len) {
        fprintf(stderr,
                "stream ratio gate failed: C stream %s level %d is %zu bytes, Go is %zu bytes\n",
                c->name, level, c_enc_len, go_enc_len);
        goto done;
    }
    if (speed) {
        snprintf(args, sizeof(args), "bench-stream \"%s\" %s %d %d %d",
                 in_name, c->name, level, loops, samples);
        if (!build_go_compat_cmd(cmd, sizeof(cmd), args)) {
            fprintf(stderr, "go stream oracle: unable to build Go benchmark command\n");
            goto done;
        }
        fflush(stdout);
        if (system(cmd) != 0) {
            fprintf(stderr, "go stream oracle: benchmark command failed: %s\n", cmd);
            goto done;
        }
    }
    ok = 1;

done:
    free(go_enc);
    remove(in_name);
    remove(out_name);
    return ok;
}

static int go_index_oracle(const corpus *c, int level, int loops, int samples) {
    static unsigned oracle_seq = 0;
    char in_name[512];
    char rel_in[96];
    char args[2048];
    char cmd[4096];
    unsigned seq = ++oracle_seq;

    snprintf(rel_in, sizeof(rel_in), "minlz_perf_go_index_input_%u_%d.bin", seq, level);
    if (!make_temp_path(in_name, sizeof(in_name), rel_in)) {
        fprintf(stderr, "go index oracle: unable to create temporary path\n");
        return 0;
    }
    if (!write_file(in_name, c->data, c->len)) {
        fprintf(stderr, "go index oracle: unable to write %s\n", in_name);
        return 0;
    }
    snprintf(args, sizeof(args), "bench-index \"%s\" %s %d %d %d",
             in_name, c->name, level, loops, samples);
    if (!build_go_compat_cmd(cmd, sizeof(cmd), args)) {
        fprintf(stderr, "go index oracle: unable to build Go benchmark command\n");
        remove(in_name);
        return 0;
    }
    fflush(stdout);
    if (system(cmd) != 0) {
        fprintf(stderr, "go index oracle: benchmark command failed: %s\n", cmd);
        remove(in_name);
        return 0;
    }
    remove(in_name);
    return 1;
}

static MINLZ_ALWAYS_INLINE int perf_equal_sse42(const uint8_t *a, const uint8_t *b, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (!minlz_backend_has_sse42()) {
        return -1;
    }
    while (len >= 64u) {
        unsigned char eq;
        __asm__ volatile(
            "movdqu 0(%[a]), %%xmm0\n\t"
            "movdqu 0(%[b]), %%xmm4\n\t"
            "movdqu 16(%[a]), %%xmm1\n\t"
            "movdqu 16(%[b]), %%xmm5\n\t"
            "movdqu 32(%[a]), %%xmm2\n\t"
            "movdqu 32(%[b]), %%xmm6\n\t"
            "movdqu 48(%[a]), %%xmm3\n\t"
            "movdqu 48(%[b]), %%xmm7\n\t"
            "pxor %%xmm4, %%xmm0\n\t"
            "pxor %%xmm5, %%xmm1\n\t"
            "pxor %%xmm6, %%xmm2\n\t"
            "pxor %%xmm7, %%xmm3\n\t"
            "por %%xmm1, %%xmm0\n\t"
            "por %%xmm2, %%xmm0\n\t"
            "por %%xmm3, %%xmm0\n\t"
            "ptest %%xmm0, %%xmm0\n\t"
            "sete %[eq]"
            : [eq] "=qm"(eq)
            : [a] "r"(a), [b] "r"(b)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7", "memory");
        if (!eq) {
            return 0;
        }
        a += 64u;
        b += 64u;
        len -= 64u;
    }
    if (len != 0u && memcmp(a, b, len) != 0) {
        return 0;
    }
    return 1;
#else
    (void)a;
    (void)b;
    (void)len;
    return -1;
#endif
}

static MINLZ_ALWAYS_INLINE int perf_equal_avx2(const uint8_t *a, const uint8_t *b, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (!minlz_backend_has_avx2()) {
        return -1;
    }
    while (len >= 128u) {
        unsigned char eq;
        __asm__ volatile(
            "vmovdqu 0(%[a]), %%ymm0\n\t"
            "vmovdqu 0(%[b]), %%ymm4\n\t"
            "vmovdqu 32(%[a]), %%ymm1\n\t"
            "vmovdqu 32(%[b]), %%ymm5\n\t"
            "vmovdqu 64(%[a]), %%ymm2\n\t"
            "vmovdqu 64(%[b]), %%ymm6\n\t"
            "vmovdqu 96(%[a]), %%ymm3\n\t"
            "vmovdqu 96(%[b]), %%ymm7\n\t"
            "vpxor %%ymm4, %%ymm0, %%ymm0\n\t"
            "vpxor %%ymm5, %%ymm1, %%ymm1\n\t"
            "vpxor %%ymm6, %%ymm2, %%ymm2\n\t"
            "vpxor %%ymm7, %%ymm3, %%ymm3\n\t"
            "vpor %%ymm1, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm2, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm3, %%ymm0, %%ymm0\n\t"
            "vptest %%ymm0, %%ymm0\n\t"
            "sete %[eq]"
            : [eq] "=qm"(eq)
            : [a] "r"(a), [b] "r"(b)
            : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7", "memory");
        if (!eq) {
            __asm__ volatile("vzeroupper" ::: "memory");
            return 0;
        }
        a += 128u;
        b += 128u;
        len -= 128u;
    }
    __asm__ volatile("vzeroupper" ::: "memory");
    return perf_equal_sse42(a, b, len);
#else
    (void)a;
    (void)b;
    (void)len;
    return -1;
#endif
}

static MINLZ_ALWAYS_INLINE int perf_equal(const uint8_t *a, const uint8_t *b, size_t len) {
    int ok;
    if (len < 4096u) {
        return memcmp(a, b, len) == 0;
    }
    ok = perf_equal_avx2(a, b, len);
    if (ok >= 0) {
        return ok;
    }
    ok = perf_equal_sse42(a, b, len);
    if (ok >= 0) {
        return ok;
    }
    return memcmp(a, b, len) == 0;
}

typedef void (*copy_lab_fn)(uint8_t *dst, const uint8_t *src, size_t len);

static MINLZ_ALWAYS_INLINE void copy_lab_current_encode(uint8_t *dst, const uint8_t *src,
                                                        size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    minlz_store64(dst + 2u, minlz_load64(src));
    minlz_store64(dst + 8u, minlz_load64(src + 6u));
    memcpy(dst + 16u, src + 14u, len - 14u);
}

static MINLZ_ALWAYS_INLINE void copy_lab_libc_encode(uint8_t *dst, const uint8_t *src,
                                                     size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    memcpy(dst + 2u, src, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_memmove_encode(uint8_t *dst, const uint8_t *src,
                                                        size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    memmove(dst + 2u, src, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_erms_encode(uint8_t *dst, const uint8_t *src,
                                                     size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    if (!minlz_copy_uncompressed_erms_movsb(dst + 2u, src, len)) {
        copy_lab_memmove_encode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_gpr128(uint8_t *dst, const uint8_t *src, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t blocks = len >> 7;
    size_t tail = len & 127u;
    if (blocks != 0u) {
        __asm__ volatile(
            "1:\n\t"
            "movq 0(%[src]), %%rax\n\t"
            "movq 8(%[src]), %%r8\n\t"
            "movq 16(%[src]), %%r9\n\t"
            "movq 24(%[src]), %%r10\n\t"
            "movq 32(%[src]), %%r11\n\t"
            "movq 40(%[src]), %%r12\n\t"
            "movq 48(%[src]), %%r13\n\t"
            "movq 56(%[src]), %%r14\n\t"
            "movq %%rax, 0(%[dst])\n\t"
            "movq %%r8, 8(%[dst])\n\t"
            "movq %%r9, 16(%[dst])\n\t"
            "movq %%r10, 24(%[dst])\n\t"
            "movq %%r11, 32(%[dst])\n\t"
            "movq %%r12, 40(%[dst])\n\t"
            "movq %%r13, 48(%[dst])\n\t"
            "movq %%r14, 56(%[dst])\n\t"
            "movq 64(%[src]), %%rax\n\t"
            "movq 72(%[src]), %%r8\n\t"
            "movq 80(%[src]), %%r9\n\t"
            "movq 88(%[src]), %%r10\n\t"
            "movq 96(%[src]), %%r11\n\t"
            "movq 104(%[src]), %%r12\n\t"
            "movq 112(%[src]), %%r13\n\t"
            "movq 120(%[src]), %%r14\n\t"
            "movq %%rax, 64(%[dst])\n\t"
            "movq %%r8, 72(%[dst])\n\t"
            "movq %%r9, 80(%[dst])\n\t"
            "movq %%r10, 88(%[dst])\n\t"
            "movq %%r11, 96(%[dst])\n\t"
            "movq %%r12, 104(%[dst])\n\t"
            "movq %%r13, 112(%[dst])\n\t"
            "movq %%r14, 120(%[dst])\n\t"
            "addq $128, %[src]\n\t"
            "addq $128, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "rax", "r8", "r9", "r10", "r11", "r12", "r13", "r14", "cc", "memory");
    }
    if (tail != 0u) {
        memcpy(dst, src, tail);
    }
#else
    memcpy(dst, src, len);
#endif
}

static MINLZ_ALWAYS_INLINE void copy_lab_xmm256(uint8_t *dst, const uint8_t *src, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t blocks = len >> 8;
    size_t tail = len & 255u;
    if (blocks != 0u) {
        __asm__ volatile(
            "1:\n\t"
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu 64(%[src]), %%xmm4\n\t"
            "movdqu 80(%[src]), %%xmm5\n\t"
            "movdqu 96(%[src]), %%xmm6\n\t"
            "movdqu 112(%[src]), %%xmm7\n\t"
            "movdqu 128(%[src]), %%xmm8\n\t"
            "movdqu 144(%[src]), %%xmm9\n\t"
            "movdqu 160(%[src]), %%xmm10\n\t"
            "movdqu 176(%[src]), %%xmm11\n\t"
            "movdqu 192(%[src]), %%xmm12\n\t"
            "movdqu 208(%[src]), %%xmm13\n\t"
            "movdqu 224(%[src]), %%xmm14\n\t"
            "movdqu 240(%[src]), %%xmm15\n\t"
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "movdqu %%xmm4, 64(%[dst])\n\t"
            "movdqu %%xmm5, 80(%[dst])\n\t"
            "movdqu %%xmm6, 96(%[dst])\n\t"
            "movdqu %%xmm7, 112(%[dst])\n\t"
            "movdqu %%xmm8, 128(%[dst])\n\t"
            "movdqu %%xmm9, 144(%[dst])\n\t"
            "movdqu %%xmm10, 160(%[dst])\n\t"
            "movdqu %%xmm11, 176(%[dst])\n\t"
            "movdqu %%xmm12, 192(%[dst])\n\t"
            "movdqu %%xmm13, 208(%[dst])\n\t"
            "movdqu %%xmm14, 224(%[dst])\n\t"
            "movdqu %%xmm15, 240(%[dst])\n\t"
            "addq $256, %[src]\n\t"
            "addq $256, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",
              "xmm15", "cc", "memory");
    }
    if (tail != 0u) {
        memcpy(dst, src, tail);
    }
#else
    memcpy(dst, src, len);
#endif
}

static MINLZ_ALWAYS_INLINE void copy_lab_gpr128_encode(uint8_t *dst, const uint8_t *src,
                                                       size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    copy_lab_gpr128(dst + 2u, src, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_xmm256_encode(uint8_t *dst, const uint8_t *src,
                                                       size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    copy_lab_xmm256(dst + 2u, src, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_xmm256_align14_encode(uint8_t *dst, const uint8_t *src,
                                                               size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    minlz_store64(dst + 2u, minlz_load64(src));
    minlz_store64(dst + 8u, minlz_load64(src + 6u));
    copy_lab_xmm256(dst + 16u, src + 14u, len - 14u);
}

static MINLZ_ALWAYS_INLINE void copy_lab_avx2_encode(uint8_t *dst, const uint8_t *src,
                                                     size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    if (!minlz_copy_uncompressed_avx2_aligned_store(dst + 2u, src, len)) {
        copy_lab_memmove_encode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_avx2_go_encode(uint8_t *dst, const uint8_t *src,
                                                        size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    if (!minlz_copy_uncompressed_avx2_go_fwd(dst + 2u, src, len)) {
        copy_lab_memmove_encode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_avx2_go256_encode(uint8_t *dst, const uint8_t *src,
                                                           size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    if (!minlz_copy_uncompressed_avx2_go_fwd256(dst + 2u, src, len)) {
        copy_lab_memmove_encode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_avx2_mod2_encode(uint8_t *dst, const uint8_t *src,
                                                          size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    if (len != 10000u || !minlz_copy_uncompressed_10000_avx2_mod2(dst + 2u, src)) {
        copy_lab_memmove_encode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_xmm_aligned_encode(uint8_t *dst, const uint8_t *src,
                                                           size_t len) {
    dst[0] = 0;
    dst[1] = 0;
    if (!minlz_copy_uncompressed_sse42_xmm_aligned_store(dst + 2u, src, len, 1)) {
        copy_lab_current_encode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_libc_decode(uint8_t *dst, const uint8_t *src,
                                                     size_t len) {
    memcpy(dst, src + 2u, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_memmove_decode(uint8_t *dst, const uint8_t *src,
                                                        size_t len) {
    memmove(dst, src + 2u, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_erms_decode(uint8_t *dst, const uint8_t *src,
                                                     size_t len) {
    if (!minlz_copy_uncompressed_erms_movsb(dst, src + 2u, len)) {
        memmove(dst, src + 2u, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_gpr128_decode(uint8_t *dst, const uint8_t *src,
                                                       size_t len) {
    copy_lab_gpr128(dst, src + 2u, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_xmm256_decode(uint8_t *dst, const uint8_t *src,
                                                       size_t len) {
    copy_lab_xmm256(dst, src + 2u, len);
}

static MINLZ_ALWAYS_INLINE void copy_lab_avx2_decode(uint8_t *dst, const uint8_t *src,
                                                     size_t len) {
    if (!minlz_copy_uncompressed_avx2_aligned_store(dst, src + 2u, len)) {
        memmove(dst, src + 2u, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_avx2_go_decode(uint8_t *dst, const uint8_t *src,
                                                        size_t len) {
    if (!minlz_copy_uncompressed_avx2_go_fwd(dst, src + 2u, len)) {
        memmove(dst, src + 2u, len);
    }
}

static MINLZ_ALWAYS_INLINE void copy_lab_xmm_aligned_decode(uint8_t *dst, const uint8_t *src,
                                                           size_t len) {
    if (!minlz_copy_uncompressed_sse42_xmm_aligned_store(dst, src + 2u, len, 0)) {
        memcpy(dst, src + 2u, len);
    }
}

static int bench_copy_variant(const char *kind, const corpus *c, copy_lab_fn fn,
                              uint8_t *dst, const uint8_t *src, int loops, int samples) {
    double *vals = (double *)malloc((size_t)samples * sizeof(vals[0]));
    volatile uint8_t guard = 0;
    int sample;
    if (!vals) {
        return 0;
    }
    for (sample = 0; sample < samples; sample++) {
        double a = perf_now();
        double b;
        int i;
        for (i = 0; i < loops; i++) {
            fn(dst, src, c->len);
        }
        b = perf_now();
        guard ^= dst[(size_t)sample % (c->len ? c->len : 1u)];
        vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
    }
    (void)guard;
    print_perf_row(kind, c->name, MINLZ_LEVEL_UNCOMPRESSED, c->len, c->len + 2u,
                   c->len ? (double)(c->len + 2u) / (double)c->len : 0.0, vals, samples);
    free(vals);
    return 1;
}

static int go_copy_oracle(const corpus *c, int loops, int samples) {
    static unsigned oracle_seq = 0;
    char in_name[512];
    char rel_in[96];
    char args[2048];
    char cmd[4096];
    unsigned seq = ++oracle_seq;

    snprintf(rel_in, sizeof(rel_in), "minlz_perf_go_copy_input_%u.bin", seq);
    if (!make_temp_path(in_name, sizeof(in_name), rel_in)) {
        fprintf(stderr, "go copy oracle: unable to create temporary path\n");
        return 0;
    }
    if (!write_file(in_name, c->data, c->len)) {
        fprintf(stderr, "go copy oracle: unable to write %s\n", in_name);
        return 0;
    }
    snprintf(args, sizeof(args), "bench-copy \"%s\" %s %d %d",
             in_name, c->name, loops, samples);
    if (!build_go_compat_cmd(cmd, sizeof(cmd), args)) {
        fprintf(stderr, "go copy oracle: unable to build Go benchmark command\n");
        remove(in_name);
        return 0;
    }
    fflush(stdout);
    if (system(cmd) != 0) {
        fprintf(stderr, "go copy oracle: benchmark command failed: %s\n", cmd);
        remove(in_name);
        return 0;
    }
    remove(in_name);
    return 1;
}

static int bench_copy_lab(const corpus *c, int loops, int samples, int run_go) {
    uint8_t *enc = (uint8_t *)malloc(c->len + 64u);
    uint8_t *dec = (uint8_t *)malloc(c->len + 64u);
    if (!enc || !dec) {
        free(enc);
        free(dec);
        return 0;
    }
    enc[0] = 0;
    enc[1] = 0;
    memcpy(enc + 2u, c->data, c->len);
    fprintf(stderr,
            "# copy-lab corpus=%s len=%zu src_mod64=%zu enc_mod64=%zu enc_payload_mod64=%zu "
            "dec_mod64=%zu backend=%s\n",
            c->name, c->len, ((size_t)c->data) & 63u, ((size_t)enc) & 63u,
            ((size_t)(enc + 2u)) & 63u, ((size_t)dec) & 63u,
            minlz_backend_name(minlz_select_backend()));
    if (!bench_copy_variant("copy-encode-current", c, copy_lab_current_encode, enc, c->data,
                            loops, samples) ||
        !bench_copy_variant("copy-encode-libc-direct", c, copy_lab_libc_encode, enc, c->data,
                            loops, samples) ||
        !bench_copy_variant("copy-encode-memmove-direct", c, copy_lab_memmove_encode, enc, c->data,
                            loops, samples) ||
        !bench_copy_variant("copy-encode-erms-movsb", c, copy_lab_erms_encode, enc, c->data,
                            loops, samples) ||
        !bench_copy_variant("copy-encode-asm-gpr128", c, copy_lab_gpr128_encode, enc, c->data,
                            loops, samples) ||
        !bench_copy_variant("copy-encode-xmm256", c, copy_lab_xmm256_encode, enc, c->data,
                            loops, samples) ||
        !bench_copy_variant("copy-encode-xmm256-align14", c, copy_lab_xmm256_align14_encode, enc,
                            c->data, loops, samples) ||
        !bench_copy_variant("copy-encode-avx2-aligned-store", c, copy_lab_avx2_encode, enc,
                            c->data, loops, samples) ||
        !bench_copy_variant("copy-encode-avx2-go-fwd", c, copy_lab_avx2_go_encode, enc,
                            c->data, loops, samples) ||
        !bench_copy_variant("copy-encode-avx2-go-fwd256", c, copy_lab_avx2_go256_encode, enc,
                            c->data, loops, samples) ||
        !bench_copy_variant("copy-encode-avx2-mod2-10000", c, copy_lab_avx2_mod2_encode, enc,
                            c->data, loops, samples) ||
        !bench_copy_variant("copy-encode-xmm-aligned-store", c, copy_lab_xmm_aligned_encode, enc,
                            c->data, loops, samples) ||
        !bench_copy_variant("copy-decode-libc-direct", c, copy_lab_libc_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-memmove-direct", c, copy_lab_memmove_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-erms-movsb", c, copy_lab_erms_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-asm-gpr128", c, copy_lab_gpr128_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-xmm256", c, copy_lab_xmm256_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-avx2-aligned-store", c, copy_lab_avx2_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-avx2-go-fwd", c, copy_lab_avx2_go_decode, dec, enc,
                            loops, samples) ||
        !bench_copy_variant("copy-decode-xmm-aligned-store", c, copy_lab_xmm_aligned_decode, dec,
                            enc, loops, samples)) {
        free(enc);
        free(dec);
        return 0;
    }
    if (memcmp(enc + 2u, c->data, c->len) != 0 ||
        memcmp(dec, c->data, c->len) != 0) {
        fprintf(stderr, "copy-lab validation failed\n");
        free(enc);
        free(dec);
        return 0;
    }
    if (run_go && !go_copy_oracle(c, loops, samples)) {
        free(enc);
        free(dec);
        return 0;
    }
    free(enc);
    free(dec);
    return 1;
}

static int make_twain_expand(corpus *c, const char *name,
                             const uint8_t *seed, size_t seed_len, size_t n) {
    size_t pos = 0;
    uint8_t cnt = 0;
    c->data = (uint8_t *)malloc(n);
    if (!c->data) {
        return 0;
    }
    c->name = name;
    c->len = n;
    while (pos < n) {
        size_t m = seed_len;
        size_t i;
        if (m > n - pos) {
            m = n - pos;
        }
        for (i = 0; i < m; i++) {
            c->data[pos + i] = (uint8_t)(seed[i] ^ cnt);
        }
        pos += m;
        cnt++;
    }
    return 1;
}

static int make_twain_repeat(corpus *c, const char *name,
                             const uint8_t *seed, size_t seed_len, size_t n) {
    size_t pos = 0;
    c->data = (uint8_t *)malloc(n);
    if (!c->data) {
        return 0;
    }
    c->name = name;
    c->len = n;
    while (pos < n) {
        size_t m = seed_len;
        if (m > n - pos) {
            m = n - pos;
        }
        memcpy(c->data + pos, seed, m);
        pos += m;
    }
    return 1;
}

static int make_random(corpus *c, const char *name, size_t n) {
    uint32_t x = 1;
    size_t i;
    c->data = (uint8_t *)malloc(n);
    if (!c->data) {
        return 0;
    }
    c->name = name;
    c->len = n;
    for (i = 0; i < n; i++) {
        x = x * 1664525u + 1013904223u;
        c->data[i] = (uint8_t)(x >> 24);
    }
    return 1;
}

static int make_logjson(corpus *c, const char *name, size_t n) {
    size_t pos = 0;
    unsigned line = 0;
    c->data = (uint8_t *)malloc(n);
    if (!c->data) {
        return 0;
    }
    c->name = name;
    c->len = n;
    while (pos < n) {
        char tmp[192];
        int m = snprintf(tmp, sizeof(tmp),
                         "{\"ts\":\"2026-05-07T12:%02u:%02uZ\",\"level\":\"info\","
                         "\"svc\":\"minlz\",\"msg\":\"block processed\","
                         "\"block\":%u,\"bytes\":%u,\"crc\":\"%08x\"}\n",
                         line % 60u, (line / 60u) % 60u, line, 4096u + (line & 8191u),
                         line * 2654435761u);
        if (m <= 0) {
            return 0;
        }
        if ((size_t)m > n - pos) {
            m = (int)(n - pos);
        }
        memcpy(c->data + pos, tmp, (size_t)m);
        pos += (size_t)m;
        line++;
    }
    return 1;
}

static int bench_block(minlz_workspace *ws, const corpus *c, int level, int loops, int samples,
                       const go_oracle_options *oracle) {
    size_t cap = minlz_max_encoded_len(c->len);
    uint8_t *enc = (uint8_t *)malloc(cap ? cap : 1);
    uint8_t *dec = (uint8_t *)malloc(c->len ? c->len : 1);
    double *vals = (double *)malloc((size_t)samples * sizeof(vals[0]));
    size_t enc_len = 0;
    int validate_decode_outside_loop = 0;
    int sample;
    if (!enc || !dec || !vals) {
        free(enc);
        free(dec);
        free(vals);
        return 0;
    }
    if (oracle && oracle->enabled && oracle->go_first) {
        if (minlz_encode_ws(ws, enc, cap, &enc_len, c->data, c->len, level) != MINLZ_OK ||
            !go_block_oracle(ws, c, level, enc, enc_len, oracle->strict_size,
                             oracle->speed, loops, samples, 0, 0.0)) {
            free(enc);
            free(dec);
            free(vals);
            return 0;
        }
    }
    for (sample = 0; sample < samples; sample++) {
        double a = perf_now();
        double b;
        int i;
        for (i = 0; i < loops; i++) {
            if (minlz_encode_ws(ws, enc, cap, &enc_len, c->data, c->len, level) != MINLZ_OK) {
                free(enc);
                free(dec);
                free(vals);
                return 0;
            }
        }
        b = perf_now();
        vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
    }
    print_perf_row("block-encode", c->name, level, c->len, enc_len,
                   c->len ? (double)enc_len / (double)c->len : 0.0, vals, samples);
    validate_decode_outside_loop =
        c->len >= (1u << 20) || (enc_len >= 2u && enc[0] == 0 && enc[1] == 0);
    {
        double c_encode_median = vals[samples / 2];
        if (oracle && oracle->enabled && !oracle->go_first &&
            !go_block_oracle(ws, c, level, enc, enc_len, oracle->strict_size,
                             oracle->speed, loops, samples, oracle->candidate_gate,
                             c_encode_median)) {
            free(enc);
            free(dec);
            free(vals);
            return 0;
        }
    }

    for (sample = 0; sample < samples; sample++) {
        double a = perf_now();
        double b;
        int i;
        for (i = 0; i < loops; i++) {
            size_t dec_len = c->len;
            if (minlz_decode_ws(ws, dec, c->len, &dec_len, enc, enc_len) != MINLZ_OK ||
                dec_len != c->len ||
                (!validate_decode_outside_loop && !perf_equal(dec, c->data, c->len))) {
                free(enc);
                free(dec);
                free(vals);
                return 0;
            }
        }
        b = perf_now();
        if (validate_decode_outside_loop && !perf_equal(dec, c->data, c->len)) {
            free(enc);
            free(dec);
            free(vals);
            return 0;
        }
        vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
    }
    print_perf_row("block-decode", c->name, level, c->len, enc_len,
                   c->len ? (double)enc_len / (double)c->len : 0.0, vals, samples);
    free(enc);
    free(dec);
    free(vals);
    return 1;
}

static int bench_stream(minlz_workspace *ws, const corpus *c, int level, int loops, int samples,
                        const go_oracle_options *oracle) {
    minlz_stream_options opts;
    mem_write encoded = {0};
    double *vals = (double *)malloc((size_t)samples * sizeof(vals[0]));
    int sample;
    if (!vals) {
        return 0;
    }
    minlz_stream_options_init(&opts);
    opts.level = level;
    opts.block_size = MINLZ_MAX_BLOCK_SIZE;
    opts.add_index = 1;

    for (sample = 0; sample < samples; sample++) {
        double a = perf_now();
        double b;
        int i;
        for (i = 0; i < loops; i++) {
            mem_read r = {c->data, c->len, 0};
            encoded.len = 0;
            if (minlz_stream_encode_ws(ws, read_cb, &r, write_cb, &encoded, &opts) != MINLZ_OK) {
                free(encoded.data);
                free(vals);
                return 0;
            }
        }
        b = perf_now();
        vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
    }
    print_perf_row("stream-encode", c->name, level, c->len, encoded.len,
                   c->len ? (double)encoded.len / (double)c->len : 0.0, vals, samples);
    if (oracle && oracle->enabled &&
        !go_stream_oracle(ws, c, level, encoded.len, oracle->strict_size,
                          oracle->speed, loops, samples)) {
        free(encoded.data);
        free(vals);
        return 0;
    }

    for (sample = 0; sample < samples; sample++) {
        double a = perf_now();
        double b;
        int i;
        for (i = 0; i < loops; i++) {
            mem_read r = {encoded.data, encoded.len, 0};
            mem_write decoded = {0};
            if (minlz_stream_decode_ws(ws, read_cb, &r, write_cb, &decoded, &opts) != MINLZ_OK ||
                decoded.len != c->len || memcmp(decoded.data, c->data, c->len) != 0) {
                free(decoded.data);
                free(encoded.data);
                free(vals);
                return 0;
            }
            free(decoded.data);
        }
        b = perf_now();
        vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
    }
    print_perf_row("stream-decode", c->name, level, c->len, encoded.len,
                   c->len ? (double)encoded.len / (double)c->len : 0.0, vals, samples);

    {
        double *idx_vals = vals;
        uint8_t *idx_bytes = NULL;
        size_t idx_len = 0;
        size_t index_start = 0;
        size_t sample;
        int have_embedded_index = 0;

        if (encoded.len >= 10) {
            uint32_t sz = load32le(encoded.data + encoded.len - 10);
            if (sz <= encoded.len && sz >= 20 &&
                memcmp(encoded.data + encoded.len - 6, "\0xdi2s", 6) == 0) {
                index_start = encoded.len - sz;
                have_embedded_index = 1;
            }
        }

        for (sample = 0; sample < (size_t)samples; sample++) {
            double a = perf_now();
            double b;
            int i;
            for (i = 0; i < loops; i++) {
                mem_read r = {encoded.data, encoded.len, 0};
                free(idx_bytes);
                idx_bytes = NULL;
                idx_len = 0;
                if (minlz_index_stream(read_cb, &r, &idx_bytes, &idx_len) != MINLZ_OK) {
                    free(idx_bytes);
                    free(encoded.data);
                    free(vals);
                    return 0;
                }
            }
            b = perf_now();
            idx_vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
        }
        print_perf_row("index-build", c->name, level, c->len, idx_len,
                       c->len ? (double)idx_len / (double)c->len : 0.0, idx_vals, samples);

        if (have_embedded_index) {
            for (sample = 0; sample < (size_t)samples; sample++) {
                double a = perf_now();
                double b;
                int i;
                for (i = 0; i < loops; i++) {
                    minlz_index *idx = NULL;
                    if (minlz_index_create(&idx) != MINLZ_OK ||
                        minlz_index_load(idx, encoded.data + index_start,
                                         encoded.len - index_start, NULL) != MINLZ_OK) {
                        minlz_index_destroy(idx);
                        free(idx_bytes);
                        free(encoded.data);
                        free(vals);
                        return 0;
                    }
                    minlz_index_destroy(idx);
                }
                b = perf_now();
                idx_vals[sample] = ((double)c->len * loops / (1024.0 * 1024.0)) / elapsed(a, b);
            }
            print_perf_row("index-load", c->name, level, c->len, encoded.len - index_start,
                           c->len ? (double)(encoded.len - index_start) / (double)c->len : 0.0,
                           idx_vals, samples);

            for (sample = 0; sample < (size_t)samples; sample++) {
                minlz_index *idx = NULL;
                double a;
                double b;
                int i;
                if (minlz_index_create(&idx) != MINLZ_OK ||
                    minlz_index_load(idx, encoded.data + index_start,
                                     encoded.len - index_start, NULL) != MINLZ_OK) {
                    minlz_index_destroy(idx);
                    free(idx_bytes);
                    free(encoded.data);
                    free(vals);
                    return 0;
                }
                a = perf_now();
                for (i = 0; i < loops * MINLZ_INDEX_FIND_INNER_LOOPS; i++) {
                    minlz_index_pair pair;
                    int64_t off = c->len ? (int64_t)(((uint64_t)i * 104729u) % c->len) : 0;
                    if (minlz_index_find(idx, off, &pair) != MINLZ_OK) {
                        minlz_index_destroy(idx);
                        free(idx_bytes);
                        free(encoded.data);
                        free(vals);
                        return 0;
                    }
                }
                b = perf_now();
                idx_vals[sample] = ((double)c->len * loops * MINLZ_INDEX_FIND_INNER_LOOPS /
                                    (1024.0 * 1024.0)) /
                                   elapsed(a, b);
                minlz_index_destroy(idx);
            }
            print_perf_row("index-find", c->name, level, c->len, encoded.len - index_start,
                           c->len ? (double)(encoded.len - index_start) / (double)c->len : 0.0,
                           idx_vals, samples);
        }

        for (sample = 0; sample < (size_t)samples; sample++) {
            minlz_reader *reader = NULL;
            minlz_readseeker *rs = NULL;
            mem_read r = {encoded.data, encoded.len, 0};
            uint8_t tmp[256];
                double a;
                double b;
            size_t got = 0;
            int i;
            if (minlz_reader_create_seekable(&reader, read_cb, seek_cb, &r, &opts) != MINLZ_OK ||
                minlz_reader_readseeker(reader, have_embedded_index ? encoded.data + index_start : NULL,
                                        have_embedded_index ? encoded.len - index_start : 0,
                                        &rs) != MINLZ_OK) {
                minlz_readseeker_destroy(rs);
                minlz_reader_destroy(reader);
                free(idx_bytes);
                free(encoded.data);
                free(vals);
                return 0;
            }
            if (minlz_readseeker_read_at(rs, tmp, sizeof(tmp), 0, &got) != MINLZ_OK ||
                got != sizeof(tmp) || memcmp(tmp, c->data, sizeof(tmp)) != 0) {
                minlz_readseeker_destroy(rs);
                minlz_reader_destroy(reader);
                free(idx_bytes);
                free(encoded.data);
                free(vals);
                return 0;
            }
            a = perf_now();
            for (i = 0; i < loops * 16; i++) {
                int64_t off = c->len > sizeof(tmp)
                    ? (int64_t)(((uint64_t)i * 65537u) % (c->len - sizeof(tmp)))
                    : 0;
                if (minlz_readseeker_read_at(rs, tmp, sizeof(tmp), off, &got) != MINLZ_OK ||
                    got != sizeof(tmp) || memcmp(tmp, c->data + off, sizeof(tmp)) != 0) {
                    minlz_readseeker_destroy(rs);
                    minlz_reader_destroy(reader);
                    free(idx_bytes);
                    free(encoded.data);
                    free(vals);
                    return 0;
                }
            }
            b = perf_now();
            idx_vals[sample] = ((double)sizeof(((uint8_t *)0)[0]) * sizeof(tmp) * loops * 16 /
                                (1024.0 * 1024.0)) / elapsed(a, b);
            minlz_readseeker_destroy(rs);
            minlz_reader_destroy(reader);
        }
        print_perf_row("read-at-hot", c->name, level, c->len, encoded.len,
                       c->len ? (double)encoded.len / (double)c->len : 0.0,
                       idx_vals, samples);
        if (oracle && oracle->enabled && oracle->speed &&
            !go_index_oracle(c, level, loops, samples)) {
            free(idx_bytes);
            free(encoded.data);
            free(vals);
            return 0;
        }
        free(idx_bytes);
    }

    free(encoded.data);
    free(vals);
    return 1;
}

int main(int argc, char **argv) {
    const char *twain = "testdata/Mark.Twain-Tom.Sawyer.txt";
    int loops = 200;
    int samples = 1;
    int run_go_reference = 0;
    int copy_lab = 0;
    int run_block = 1;
    int run_stream_index = 1;
    int positional = 0;
    int loops_explicit = 0;
    const char *only_corpus = NULL;
    int only_level = 0;
    int have_only_level = 0;
    int ran_any = 0;
    go_oracle_options oracle = {0};
    uint8_t *seed = NULL;
    size_t seed_len = 0;
    corpus corpora[12];
    minlz_workspace *ws = NULL;
    size_t i;
    int levels[] = {
        MINLZ_LEVEL_SUPERFAST,
        MINLZ_LEVEL_FASTEST,
        MINLZ_LEVEL_BALANCED,
        MINLZ_LEVEL_SMALLEST,
        MINLZ_LEVEL_UNCOMPRESSED
    };
    int argi;
    for (argi = 1; argi < argc; argi++) {
        if (strcmp(argv[argi], "--iterations") == 0 || strcmp(argv[argi], "-n") == 0) {
            if (++argi >= argc) {
                fprintf(stderr, "--iterations requires a value\n");
                return 2;
            }
            loops = atoi(argv[argi]);
            loops_explicit = 1;
        } else if (strcmp(argv[argi], "--samples") == 0) {
            if (++argi >= argc) {
                fprintf(stderr, "--samples requires a value\n");
                return 2;
            }
            samples = atoi(argv[argi]);
        } else if (strcmp(argv[argi], "--go-reference") == 0) {
            run_go_reference = 1;
        } else if (strcmp(argv[argi], "--go-oracle") == 0) {
            oracle.enabled = 1;
        } else if (strcmp(argv[argi], "--go-speed") == 0) {
            oracle.enabled = 1;
            oracle.speed = 1;
        } else if (strcmp(argv[argi], "--strict-go-size") == 0) {
            oracle.enabled = 1;
            oracle.strict_size = 1;
        } else if (strcmp(argv[argi], "--candidate-gate") == 0) {
            oracle.enabled = 1;
            oracle.candidate_gate = 1;
        } else if (strcmp(argv[argi], "--go-first") == 0) {
            oracle.enabled = 1;
            oracle.go_first = 1;
        } else if (strcmp(argv[argi], "--copy-lab") == 0) {
            copy_lab = 1;
        } else if (strcmp(argv[argi], "--ops") == 0) {
            if (++argi >= argc) {
                fprintf(stderr, "--ops requires one of: all, block, stream, index\n");
                return 2;
            }
            if (strcmp(argv[argi], "all") == 0) {
                run_block = 1;
                run_stream_index = 1;
            } else if (strcmp(argv[argi], "block") == 0) {
                run_block = 1;
                run_stream_index = 0;
            } else if (strcmp(argv[argi], "stream") == 0 || strcmp(argv[argi], "index") == 0) {
                run_block = 0;
                run_stream_index = 1;
            } else {
                fprintf(stderr, "unknown --ops value: %s\n", argv[argi]);
                return 2;
            }
        } else if (strcmp(argv[argi], "--corpus") == 0) {
            if (++argi >= argc) {
                fprintf(stderr, "--corpus requires a substring\n");
                return 2;
            }
            only_corpus = argv[argi];
        } else if (strcmp(argv[argi], "--level") == 0) {
            if (++argi >= argc) {
                fprintf(stderr, "--level requires a value\n");
                return 2;
            }
            only_level = atoi(argv[argi]);
            have_only_level = 1;
        } else if (strcmp(argv[argi], "--format") == 0) {
            if (++argi >= argc || strcmp(argv[argi], "csv") != 0) {
                fprintf(stderr, "only --format csv is supported\n");
                return 2;
            }
        } else if (strcmp(argv[argi], "--help") == 0 || strcmp(argv[argi], "-h") == 0) {
            printf("usage: minlz_perf [twain-file] [loops] [--iterations N] [--samples N] [--corpus SUBSTR] [--level LEVEL] [--ops all|block|stream|index] [--format csv] [--go-reference] [--go-oracle] [--go-speed] [--strict-go-size] [--candidate-gate] [--go-first] [--copy-lab]\n");
            return 0;
        } else if (positional == 0) {
            twain = argv[argi];
            positional++;
        } else if (positional == 1) {
            loops = atoi(argv[argi]);
            loops_explicit = 1;
            positional++;
        } else {
            fprintf(stderr, "unexpected argument: %s\n", argv[argi]);
            return 2;
        }
    }
    if (loops <= 0) {
        loops = 1;
    }
    if (samples <= 0) {
        samples = 1;
    }
    memset(corpora, 0, sizeof(corpora));
    if (!read_file(twain, &seed, &seed_len)) {
        fprintf(stderr, "unable to read %s\n", twain);
        return 2;
    }
    if (!make_small_pattern(&corpora[0], "small-16", 16) ||
        !make_small_pattern(&corpora[1], "small-64", 64) ||
        !make_small_pattern(&corpora[2], "small-256", 256) ||
        !make_small_pattern(&corpora[3], "small-1024", 1024) ||
        !make_small_pattern(&corpora[4], "small-2k", 2048) ||
        !make_small_pattern(&corpora[5], "small-4k", 4096) ||
        !make_small_pattern(&corpora[6], "small-8k", 8192) ||
        !make_twain_expand(&corpora[7], "twain-expand-10k", seed, seed_len, 10000) ||
        !make_twain_expand(&corpora[8], "twain-expand-1m", seed, seed_len, 1000000) ||
        !make_twain_repeat(&corpora[9], "twain-repeat-8m", seed, seed_len, MINLZ_MAX_BLOCK_SIZE) ||
        !make_random(&corpora[10], "random-1m", 1000000) ||
        !make_logjson(&corpora[11], "logjson-1m", 1000000) ||
        minlz_workspace_create(&ws) != MINLZ_OK) {
        fprintf(stderr, "setup failed\n");
        free(seed);
        return 2;
    }
    printf("kind,corpus,level,raw_bytes,encoded_bytes,ratio,mbps_min,mbps_median,mbps_max\n");
    if (copy_lab) {
        int local_loops = loops;
        size_t ci;
        if (!loops_explicit && local_loops < MINLZ_SMALL_AUTO_LOOPS) {
            local_loops = MINLZ_SMALL_AUTO_LOOPS;
        }
        for (ci = 0; ci < sizeof(corpora) / sizeof(corpora[0]); ci++) {
            if (only_corpus && strstr(corpora[ci].name, only_corpus) == NULL) {
                continue;
            }
            if (!bench_copy_lab(&corpora[ci], local_loops, samples,
                                oracle.enabled || oracle.speed)) {
                fprintf(stderr, "copy-lab failed on %s\n", corpora[ci].name);
                return 2;
            }
            ran_any = 1;
        }
        if (!ran_any) {
            fprintf(stderr, "copy-lab matched no corpora\n");
            return 2;
        }
        goto done;
    }
    for (i = 0; i < sizeof(corpora) / sizeof(corpora[0]); i++) {
        size_t j;
        int local_loops = loops;
        if (only_corpus && strstr(corpora[i].name, only_corpus) == NULL) {
            continue;
        }
        /*
         * A tiny block measured with a low explicit loop count is below the
         * timer resolution and produces order-dependent Go wins.  Batch tiny
         * and 10K inputs regardless of whether the caller supplied a global
         * iteration count; large corpora still honor that count exactly.
         */
        if (corpora[i].len <= 1024 && local_loops < MINLZ_SMALL_AUTO_LOOPS) {
            local_loops = MINLZ_SMALL_AUTO_LOOPS;
        } else if (corpora[i].len <= 10000 && local_loops < 20000) {
            local_loops = 20000;
        }
        if (!loops_explicit && corpora[i].len >= MINLZ_MAX_BLOCK_SIZE && local_loops > 30) {
            local_loops = 30;
        }
        for (j = 0; j < sizeof(levels) / sizeof(levels[0]); j++) {
            if (have_only_level && levels[j] != only_level) {
                continue;
            }
            if (run_block) {
                if (!bench_block(ws, &corpora[i], levels[j], local_loops, samples, &oracle)) {
                    fprintf(stderr, "block benchmark failed on %s level %d\n", corpora[i].name, levels[j]);
                    return 2;
                }
                ran_any = 1;
            }
        }
        if (run_stream_index && corpora[i].len >= 1000000) {
            for (j = 0; j < sizeof(levels) / sizeof(levels[0]); j++) {
                if (have_only_level && levels[j] != only_level) {
                    continue;
                }
                if (!bench_stream(ws, &corpora[i], levels[j], local_loops > 20 ? 20 : local_loops,
                                  samples, &oracle)) {
                    fprintf(stderr, "stream benchmark failed on %s level %d\n",
                            corpora[i].name, levels[j]);
                    return 2;
                }
                ran_any = 1;
            }
        }
    }
    if (!ran_any) {
        fprintf(stderr, "no benchmarks matched the requested filters\n");
        return 2;
    }
done:
    for (i = 0; i < sizeof(corpora) / sizeof(corpora[0]); i++) {
        free(corpora[i].data);
    }
    minlz_workspace_destroy(ws);
    free(seed);
    if (run_go_reference) {
        fflush(stdout);
        fprintf(stderr, "# go-reference-begin\n");
        if (system("go test -run NONE -bench=BenchmarkTwain -benchtime=1s -count=5") != 0) {
            fprintf(stderr, "# go-reference-failed\n");
            return 2;
        }
        fprintf(stderr, "# go-reference-end\n");
    }
    return 0;
}
