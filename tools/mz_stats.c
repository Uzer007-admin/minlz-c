/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "mz_stats.h"

#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <io.h>
#else
#include <glob.h>
#endif

#define MZ_STATS_HIST_BUCKETS 25
#define MZ_STATS_OPS 8
#define MZ_STATS_MIN_COPY2_OFFSET 64u
#define MZ_STATS_MIN_COPY3_OFFSET 65536u

enum {
    MZ_OP_LITERAL = 0,
    MZ_OP_REPEAT,
    MZ_OP_COPY1,
    MZ_OP_COPY2,
    MZ_OP_COPY2_FUSED,
    MZ_OP_COPY3,
    MZ_OP_COPY3_FUSED,
    MZ_OP_FUSED_LITERAL
};

static const char *const mz_op_names[MZ_STATS_OPS] = {
    "Literal", "Repeat", "Copy1", "Copy2",
    "Copy2-F", "Copy3", "Copy3-F", "FusedLit"
};

typedef struct mz_op_stats {
    uint64_t count;
    uint64_t out_bytes;
    uint64_t header_bytes;
    uint64_t fused_literal_bytes;
    uint64_t fused_ops;
    uint32_t min_len;
    uint32_t max_len;
    uint64_t sum_len;
    uint32_t min_offset;
    uint32_t max_offset;
    uint64_t sum_offset;
    uint64_t len_hist[MZ_STATS_HIST_BUCKETS];
    uint64_t offset_hist[MZ_STATS_HIST_BUCKETS];
    uint64_t header_hist[8];
    uint64_t fused_len_hist[5];
} mz_op_stats;

typedef struct mz_block_stats {
    size_t index;
    uint8_t chunk_type;
    size_t compressed_size;
    size_t uncompressed_size;
    mz_op_stats ops[MZ_STATS_OPS];
    uint64_t repeat_previous[MZ_STATS_OPS];
    uint32_t longest_copy;
    uint32_t longest_literal_run;
} mz_block_stats;

typedef struct mz_stream_stats {
    uint64_t chunk_count[256];
    uint64_t chunk_bytes[256];
    size_t declared_max_block;
    uint64_t stream_ids;
    uint64_t crc_mode_02;
    uint64_t crc_mode_03;
    uint64_t block_size_hist[MZ_STATS_HIST_BUCKETS];
    uint64_t uncompressed_size_hist[MZ_STATS_HIST_BUCKETS];
    uint64_t ratio_hist[11];
    uint64_t legacy_blocks;
    uint64_t uncompressed_blocks;
    uint64_t minlz_blocks;
} mz_stream_stats;

typedef struct mz_file_report {
    const char *name;
    int64_t input_size;
    const char *format;
    double elapsed_seconds;
    mz_stream_stats stream;
    mz_op_stats op_totals[MZ_STATS_OPS];
    mz_block_stats *blocks;
    size_t block_count;
    size_t block_cap;
    uint64_t total_ops;
    uint64_t total_output;
    uint64_t total_encoded;
    uint64_t total_literal;
    uint64_t total_copy;
    uint64_t total_repeat;
    uint32_t longest_copy;
    uint32_t longest_literal_run;
    uint64_t repeat_previous[MZ_STATS_OPS];
} mz_file_report;

typedef struct mz_stats_options {
    int json;
    int csv;
    int per_block;
    int no_hist;
    int quiet;
} mz_stats_options;

static char *mz_stats_strdup(const char *value) {
    size_t len = strlen(value) + 1u;
    char *copy = (char *)malloc(len);
    if (copy) {
        memcpy(copy, value, len);
    }
    return copy;
}

static int mz_stats_add_file(char ***files, size_t *count,
                             size_t *cap, const char *name) {
    char **next;
    char *copy;
    if (*count == *cap) {
        size_t next_cap = *cap ? *cap * 2u : 16u;
        if (next_cap > SIZE_MAX / sizeof(**files)) {
            return 0;
        }
        next = (char **)realloc(
            *files, next_cap * sizeof(**files));
        if (!next) {
            return 0;
        }
        *files = next;
        *cap = next_cap;
    }
    copy = mz_stats_strdup(name);
    if (!copy) {
        return 0;
    }
    (*files)[(*count)++] = copy;
    return 1;
}

static int mz_stats_expand_file(char ***files, size_t *count,
                                size_t *cap, const char *pattern) {
    if (strcmp(pattern, "-") == 0 ||
        strpbrk(pattern, "*?[") == NULL) {
        return mz_stats_add_file(files, count, cap, pattern);
    }
#ifdef _WIN32
    {
        struct _finddata_t found;
        intptr_t handle;
        const char *slash1 = strrchr(pattern, '\\');
        const char *slash2 = strrchr(pattern, '/');
        const char *slash =
            !slash1 ? slash2
                    : (!slash2 || slash1 > slash2 ? slash1 : slash2);
        size_t prefix_len =
            slash ? (size_t)(slash - pattern + 1) : 0u;
        size_t before = *count;
        handle = _findfirst(pattern, &found);
        if (handle == -1) {
            return 0;
        }
        do {
            char *path;
            size_t name_len;
            if ((found.attrib & _A_SUBDIR) != 0) {
                continue;
            }
            name_len = strlen(found.name);
            path = (char *)malloc(prefix_len + name_len + 1u);
            if (!path) {
                _findclose(handle);
                return 0;
            }
            if (prefix_len != 0u) {
                memcpy(path, pattern, prefix_len);
            }
            memcpy(path + prefix_len, found.name, name_len + 1u);
            if (!mz_stats_add_file(files, count, cap, path)) {
                free(path);
                _findclose(handle);
                return 0;
            }
            free(path);
        } while (_findnext(handle, &found) == 0);
        _findclose(handle);
        return *count != before;
    }
#else
    {
        glob_t matches;
        size_t i;
        int result = glob(pattern, 0, NULL, &matches);
        if (result != 0) {
            globfree(&matches);
            return 0;
        }
        for (i = 0u; i < matches.gl_pathc; i++) {
            if (!mz_stats_add_file(
                    files, count, cap, matches.gl_pathv[i])) {
                globfree(&matches);
                return 0;
            }
        }
        globfree(&matches);
        return 1;
    }
#endif
}

static void mz_stats_free_files(char **files, size_t count) {
    size_t i;
    for (i = 0u; i < count; i++) {
        free(files[i]);
    }
    free(files);
}

static uint32_t mz_load32(const uint8_t *src) {
    return (uint32_t)src[0] |
           ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) |
           ((uint32_t)src[3] << 24);
}

static unsigned mz_bucket(uint32_t value) {
    unsigned bucket = 0u;
    if (value == 0u) {
        return 0u;
    }
    while (value > 1u) {
        value >>= 1;
        bucket++;
    }
    return bucket < MZ_STATS_HIST_BUCKETS
               ? bucket
               : MZ_STATS_HIST_BUCKETS - 1u;
}

static unsigned mz_header_bucket(size_t value) {
    return value > 7u ? 7u : (unsigned)value;
}

static unsigned mz_ratio_bucket(size_t uncompressed, size_t compressed) {
    size_t ratio;
    if (uncompressed == 0u ||
        compressed > SIZE_MAX / 100u) {
        return 10u;
    }
    ratio = compressed * 100u / uncompressed;
    return ratio >= 100u ? 10u : (unsigned)(ratio / 10u);
}

static int mz_read_uvarint(const uint8_t *src, size_t src_len,
                           uint64_t *value, size_t *used) {
    uint64_t result = 0u;
    unsigned shift = 0u;
    size_t i;
    for (i = 0u; i < src_len && i < 10u; i++) {
        uint8_t byte = src[i];
        if (byte < 0x80u) {
            if (i == 9u && byte > 1u) {
                return 0;
            }
            result |= (uint64_t)byte << shift;
            *value = result;
            *used = i + 1u;
            return 1;
        }
        result |= (uint64_t)(byte & 0x7fu) << shift;
        shift += 7u;
    }
    return 0;
}

static void mz_op_add_len(mz_op_stats *stats, uint32_t value) {
    if (stats->min_len == 0u || value < stats->min_len) {
        stats->min_len = value;
    }
    if (value > stats->max_len) {
        stats->max_len = value;
    }
    stats->sum_len += value;
    stats->len_hist[mz_bucket(value)]++;
}

static void mz_op_add_offset(mz_op_stats *stats, uint32_t value) {
    if (stats->min_offset == 0u || value < stats->min_offset) {
        stats->min_offset = value;
    }
    if (value > stats->max_offset) {
        stats->max_offset = value;
    }
    stats->sum_offset += value;
    stats->offset_hist[mz_bucket(value)]++;
}

static void mz_emit_op(mz_block_stats *block, int op, size_t header,
                       uint32_t length, uint32_t fused_literals,
                       uint32_t offset, int *previous_op,
                       int *previous_set, uint32_t *literal_run) {
    mz_op_stats *stats = &block->ops[op];
    stats->count++;
    stats->out_bytes += length;
    stats->header_bytes += header;
    if (op != MZ_OP_FUSED_LITERAL) {
        stats->header_hist[mz_header_bucket(header)]++;
    }
    if (length != 0u) {
        mz_op_add_len(stats, length);
    }
    if (op == MZ_OP_COPY2_FUSED ||
        op == MZ_OP_COPY3_FUSED) {
        stats->fused_literal_bytes += fused_literals;
        stats->fused_ops++;
        if (fused_literals < 5u) {
            stats->fused_len_hist[fused_literals]++;
        }
    }
    if (op != MZ_OP_LITERAL && op != MZ_OP_FUSED_LITERAL) {
        mz_op_add_offset(stats, offset);
        if (length > block->longest_copy) {
            block->longest_copy = length;
        }
    }
    if (op == MZ_OP_REPEAT && *previous_set) {
        block->repeat_previous[*previous_op]++;
    }
    if (op == MZ_OP_LITERAL) {
        *literal_run += length;
        if (*literal_run > block->longest_literal_run) {
            block->longest_literal_run = *literal_run;
        }
    } else if (op == MZ_OP_COPY2_FUSED ||
               op == MZ_OP_COPY3_FUSED) {
        uint32_t run = *literal_run + fused_literals;
        if (run > block->longest_literal_run) {
            block->longest_literal_run = run;
        }
        *literal_run = 0u;
    } else if (op != MZ_OP_FUSED_LITERAL) {
        *literal_run = 0u;
    }
    if (op != MZ_OP_FUSED_LITERAL) {
        *previous_op = op;
        *previous_set = 1;
    }
}

static int mz_walk_block(const uint8_t *src, size_t src_len,
                         size_t wanted, mz_block_stats *block,
                         char *error, size_t error_cap) {
    size_t d = 0u;
    size_t s = 0u;
    uint32_t offset = 1u;
    int previous_op = -1;
    int previous_set = 0;
    uint32_t literal_run = 0u;

#define MZ_WALK_FAIL(...) do { \
    snprintf(error, error_cap, __VA_ARGS__); \
    return 0; \
} while (0)

    while (s < src_len) {
        size_t op_start = s;
        uint8_t tag = src[s];
        switch (tag & 3u) {
        case 0u: {
            int repeat = (tag & 4u) != 0u;
            uint32_t x = tag >> 3;
            uint32_t length;
            if (x < 29u) {
                s++;
                length = x + 1u;
            } else if (x == 29u) {
                if (src_len - s < 2u) {
                    MZ_WALK_FAIL("truncated literal/repeat length(1)");
                }
                length = 30u + src[s + 1u];
                s += 2u;
            } else if (x == 30u) {
                if (src_len - s < 3u) {
                    MZ_WALK_FAIL("truncated literal/repeat length(2)");
                }
                length = 30u + (uint32_t)src[s + 1u] +
                         ((uint32_t)src[s + 2u] << 8);
                s += 3u;
            } else {
                if (src_len - s < 4u) {
                    MZ_WALK_FAIL("truncated literal/repeat length(3)");
                }
                length = 30u + (uint32_t)src[s + 1u] +
                         ((uint32_t)src[s + 2u] << 8) +
                         ((uint32_t)src[s + 3u] << 16);
                s += 4u;
            }
            if ((size_t)length > wanted - d) {
                MZ_WALK_FAIL("literal/repeat length %u exceeds output", length);
            }
            if (repeat) {
                if (offset > d) {
                    MZ_WALK_FAIL("repeat offset %u exceeds output %zu",
                                 offset, d);
                }
                mz_emit_op(block, MZ_OP_REPEAT, s - op_start,
                           length, 0u, offset, &previous_op,
                           &previous_set, &literal_run);
                d += length;
            } else {
                if ((size_t)length > src_len - s) {
                    MZ_WALK_FAIL("literal body truncated");
                }
                mz_emit_op(block, MZ_OP_LITERAL, s - op_start,
                           length, 0u, 0u, &previous_op,
                           &previous_set, &literal_run);
                s += length;
                d += length;
            }
            break;
        }
        case 1u: {
            uint32_t length = (tag >> 2) & 15u;
            uint16_t encoded;
            if (src_len - s < 2u) {
                MZ_WALK_FAIL("truncated copy1");
            }
            encoded = (uint16_t)src[s] |
                      ((uint16_t)src[s + 1u] << 8);
            offset = ((uint32_t)encoded >> 6) + 1u;
            if (length == 15u) {
                if (src_len - s < 3u) {
                    MZ_WALK_FAIL("truncated copy1 extension");
                }
                length = (uint32_t)src[s + 2u] + 18u;
                s += 3u;
            } else {
                length += 4u;
                s += 2u;
            }
            if ((size_t)length > wanted - d || offset > d) {
                MZ_WALK_FAIL("invalid copy1 length/offset");
            }
            mz_emit_op(block, MZ_OP_COPY1, s - op_start,
                       length, 0u, offset, &previous_op,
                       &previous_set, &literal_run);
            d += length;
            break;
        }
        case 2u: {
            uint32_t length = tag >> 2;
            uint32_t encoded_offset;
            if (src_len - s < 3u) {
                MZ_WALK_FAIL("truncated copy2");
            }
            encoded_offset = (uint32_t)src[s + 1u] |
                             ((uint32_t)src[s + 2u] << 8);
            if (length <= 60u) {
                length += 4u;
                s += 3u;
            } else if (length == 61u) {
                if (src_len - s < 4u) {
                    MZ_WALK_FAIL("truncated copy2.61");
                }
                length = (uint32_t)src[s + 3u] + 64u;
                s += 4u;
            } else if (length == 62u) {
                if (src_len - s < 5u) {
                    MZ_WALK_FAIL("truncated copy2.62");
                }
                length = (uint32_t)src[s + 3u] |
                         ((uint32_t)src[s + 4u] << 8);
                length += 64u;
                s += 5u;
            } else {
                if (src_len - s < 6u) {
                    MZ_WALK_FAIL("truncated copy2.63");
                }
                length = (uint32_t)src[s + 3u] |
                         ((uint32_t)src[s + 4u] << 8) |
                         ((uint32_t)src[s + 5u] << 16);
                length += 64u;
                s += 6u;
            }
            offset = encoded_offset + MZ_STATS_MIN_COPY2_OFFSET;
            if ((size_t)length > wanted - d || offset > d) {
                MZ_WALK_FAIL("invalid copy2 length/offset");
            }
            mz_emit_op(block, MZ_OP_COPY2, s - op_start,
                       length, 0u, offset, &previous_op,
                       &previous_set, &literal_run);
            d += length;
            break;
        }
        default: {
            uint32_t value;
            uint32_t literal_length;
            uint32_t length;
            int op;
            if (src_len - s < 4u) {
                MZ_WALK_FAIL("truncated copy3/copy2-fused");
            }
            value = mz_load32(src + s);
            literal_length = (value >> 3) & 3u;
            if ((value & 4u) == 0u) {
                op = MZ_OP_COPY2_FUSED;
                length = 4u + ((value >> 5) & 7u);
                offset = ((value >> 8) & 65535u) +
                         MZ_STATS_MIN_COPY2_OFFSET;
                s += 3u;
                literal_length++;
            } else {
                uint32_t encoded_length = (value >> 5) & 63u;
                op = literal_length
                         ? MZ_OP_COPY3_FUSED
                         : MZ_OP_COPY3;
                offset = (value >> 11) + MZ_STATS_MIN_COPY3_OFFSET;
                if (encoded_length < 61u) {
                    length = encoded_length + 4u;
                    s += 4u;
                } else if (encoded_length == 61u) {
                    if (src_len - s < 5u) {
                        MZ_WALK_FAIL("truncated copy3.61");
                    }
                    length = (uint32_t)src[s + 4u] + 64u;
                    s += 5u;
                } else if (encoded_length == 62u) {
                    if (src_len - s < 6u) {
                        MZ_WALK_FAIL("truncated copy3.62");
                    }
                    length = (uint32_t)src[s + 4u] |
                             ((uint32_t)src[s + 5u] << 8);
                    length += 64u;
                    s += 6u;
                } else {
                    if (src_len - s < 7u) {
                        MZ_WALK_FAIL("truncated copy3.63");
                    }
                    length = (uint32_t)src[s + 4u] |
                             ((uint32_t)src[s + 5u] << 8) |
                             ((uint32_t)src[s + 6u] << 16);
                    length += 64u;
                    s += 7u;
                }
            }
            if ((size_t)literal_length > src_len - s ||
                (size_t)literal_length > wanted - d) {
                MZ_WALK_FAIL("invalid fused literal length");
            }
            s += literal_length;
            d += literal_length;
            if ((size_t)length > wanted - d || offset > d) {
                MZ_WALK_FAIL("invalid copy3/copy2-fused length/offset");
            }
            mz_emit_op(block, op, s - literal_length - op_start,
                       length, literal_length, offset,
                       &previous_op, &previous_set, &literal_run);
            if (literal_length != 0u) {
                mz_emit_op(block, MZ_OP_FUSED_LITERAL, 0u,
                           literal_length, 0u, 0u,
                           &previous_op, &previous_set,
                           &literal_run);
            }
            d += length;
            break;
        }
        }
    }
    if (d != wanted) {
        MZ_WALK_FAIL("block produced %zu bytes, expected %zu", d, wanted);
    }
    return 1;
#undef MZ_WALK_FAIL
}

static void mz_merge_op(mz_op_stats *dst, const mz_op_stats *src) {
    size_t i;
    if (src->count == 0u) {
        return;
    }
    if (dst->min_len == 0u ||
        (src->min_len != 0u && src->min_len < dst->min_len)) {
        dst->min_len = src->min_len;
    }
    if (src->max_len > dst->max_len) {
        dst->max_len = src->max_len;
    }
    if (dst->min_offset == 0u ||
        (src->min_offset != 0u &&
         src->min_offset < dst->min_offset)) {
        dst->min_offset = src->min_offset;
    }
    if (src->max_offset > dst->max_offset) {
        dst->max_offset = src->max_offset;
    }
    dst->count += src->count;
    dst->out_bytes += src->out_bytes;
    dst->header_bytes += src->header_bytes;
    dst->fused_literal_bytes += src->fused_literal_bytes;
    dst->fused_ops += src->fused_ops;
    dst->sum_len += src->sum_len;
    dst->sum_offset += src->sum_offset;
    for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
        dst->len_hist[i] += src->len_hist[i];
        dst->offset_hist[i] += src->offset_hist[i];
    }
    for (i = 0u; i < 8u; i++) {
        dst->header_hist[i] += src->header_hist[i];
    }
    for (i = 0u; i < 5u; i++) {
        dst->fused_len_hist[i] += src->fused_len_hist[i];
    }
}

static uint64_t mz_block_op_count(const mz_block_stats *block) {
    uint64_t count = 0u;
    size_t i;
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        if (i != MZ_OP_FUSED_LITERAL) {
            count += block->ops[i].count;
        }
    }
    return count;
}

static int mz_report_add_block(mz_file_report *report,
                               const mz_block_stats *block,
                               const mz_stats_options *options) {
    size_t i;
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        mz_merge_op(&report->op_totals[i], &block->ops[i]);
        if (i != MZ_OP_FUSED_LITERAL) {
            report->total_ops += block->ops[i].count;
        }
        report->total_output += block->ops[i].out_bytes;
        report->total_encoded += block->ops[i].header_bytes;
    }
    report->total_encoded +=
        block->ops[MZ_OP_LITERAL].out_bytes +
        block->ops[MZ_OP_FUSED_LITERAL].out_bytes;
    report->total_literal +=
        block->ops[MZ_OP_LITERAL].out_bytes +
        block->ops[MZ_OP_FUSED_LITERAL].out_bytes;
    report->total_repeat += block->ops[MZ_OP_REPEAT].out_bytes;
    report->total_copy +=
        block->ops[MZ_OP_COPY1].out_bytes +
        block->ops[MZ_OP_COPY2].out_bytes +
        block->ops[MZ_OP_COPY2_FUSED].out_bytes +
        block->ops[MZ_OP_COPY3].out_bytes +
        block->ops[MZ_OP_COPY3_FUSED].out_bytes;
    if (block->longest_copy > report->longest_copy) {
        report->longest_copy = block->longest_copy;
    }
    if (block->longest_literal_run >
        report->longest_literal_run) {
        report->longest_literal_run =
            block->longest_literal_run;
    }
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        report->repeat_previous[i] +=
            block->repeat_previous[i];
    }
    if (!options->per_block) {
        return 1;
    }
    if (report->block_count == report->block_cap) {
        size_t next_cap = report->block_cap
                              ? report->block_cap * 2u
                              : 32u;
        mz_block_stats *next;
        if (next_cap > SIZE_MAX / sizeof(*next)) {
            return 0;
        }
        next = (mz_block_stats *)realloc(
            report->blocks, next_cap * sizeof(*next));
        if (!next) {
            return 0;
        }
        report->blocks = next;
        report->block_cap = next_cap;
    }
    report->blocks[report->block_count++] = *block;
    return 1;
}

static int mz_read_all(FILE *file, uint8_t **data, size_t *length) {
    size_t cap = 65536u;
    size_t len = 0u;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        return 0;
    }
    for (;;) {
        size_t got;
        if (len == cap) {
            size_t next_cap =
                cap <= SIZE_MAX / 2u ? cap * 2u : SIZE_MAX;
            uint8_t *next;
            if (next_cap == cap) {
                free(buf);
                return 0;
            }
            next = (uint8_t *)realloc(buf, next_cap);
            if (!next) {
                free(buf);
                return 0;
            }
            buf = next;
            cap = next_cap;
        }
        got = fread(buf + len, 1u, cap - len, file);
        len += got;
        if (got == 0u) {
            if (ferror(file)) {
                free(buf);
                return 0;
            }
            break;
        }
    }
    *data = buf;
    *length = len;
    return 1;
}

static int mz_analyze_block_bytes(
    const uint8_t *data, size_t length,
    mz_file_report *report, const mz_stats_options *options,
    uint8_t chunk_type, size_t compressed_size,
    size_t header_skip, uint64_t declared_size,
    char *error, size_t error_cap) {
    mz_block_stats block;
    const uint8_t *body;
    size_t body_len;
    memset(&block, 0, sizeof(block));
    block.index = report->block_count;
    block.chunk_type = chunk_type;
    block.compressed_size = compressed_size;
    block.uncompressed_size = (size_t)declared_size;
    if (header_skip > length) {
        snprintf(error, error_cap, "truncated block header");
        return 0;
    }
    body = data + header_skip;
    body_len = length - header_skip;
    if (declared_size == 0u) {
        uint32_t literal_length;
        int previous_op = -1;
        int previous_set = 0;
        uint32_t literal_run = 0u;
        if (body_len > UINT32_MAX) {
            snprintf(error, error_cap, "literal-only block too large");
            return 0;
        }
        literal_length = (uint32_t)body_len;
        mz_emit_op(&block, MZ_OP_LITERAL, header_skip,
                   literal_length, 0u, 0u, &previous_op,
                   &previous_set, &literal_run);
        block.uncompressed_size = body_len;
    } else if (!mz_walk_block(
                   body, body_len, (size_t)declared_size,
                   &block, error, error_cap)) {
        return 0;
    }
    return mz_report_add_block(report, &block, options);
}

static int mz_analyze_raw(
    const uint8_t *data, size_t length,
    mz_file_report *report, const mz_stats_options *options,
    char *error, size_t error_cap) {
    uint64_t decoded = 0u;
    size_t used = 0u;
    if (length == 0u || data[0] != 0u) {
        snprintf(error, error_cap,
                 "not a MinLZ block: missing 0x00 marker");
        return 0;
    }
    report->format = "block";
    if (length == 1u) {
        return 1;
    }
    if (!mz_read_uvarint(data + 1u, length - 1u,
                         &decoded, &used)) {
        snprintf(error, error_cap, "corrupt block length varint");
        return 0;
    }
    if (decoded > SIZE_MAX) {
        snprintf(error, error_cap, "decoded block too large");
        return 0;
    }
    return mz_analyze_block_bytes(
        data, length, report, options, 0u, length,
        1u + used, decoded, error, error_cap);
}

static const char *mz_chunk_name(unsigned type) {
    switch (type) {
    case 0x00u: return "legacy-comp(s2)";
    case 0x01u: return "uncompressed";
    case 0x02u: return "minlz(crc-unc)";
    case 0x03u: return "minlz(crc-cmp)";
    case 0x20u: return "eof";
    case 0x40u: return "index";
    case 0x44u: return "search-info";
    case 0x45u: return "search-table";
    case 0x46u: return "search-table-compressed";
    case 0x47u: return "remote-block-ref";
    case 0xfeu: return "padding";
    case 0xffu: return "stream-id";
    default:
        if (type >= 0x80u && type <= 0xbfu) {
            return "user-skippable";
        }
        if (type >= 0xc0u && type <= 0xfdu) {
            return "user-non-skip";
        }
        return type <= 0x3fu
                   ? "reserved-non-skip"
                   : "reserved-skippable";
    }
}

static int mz_analyze_stream(
    const uint8_t *data, size_t length,
    mz_file_report *report, const mz_stats_options *options,
    char *error, size_t error_cap) {
    size_t pos = 0u;
    int stream_open = 0;
    uint64_t stream_size = 0u;
    report->format = "stream";
    while (pos < length) {
        uint8_t type;
        size_t chunk_len;
        const uint8_t *payload;
        if (length - pos < 4u) {
            snprintf(error, error_cap, "truncated chunk header");
            return 0;
        }
        type = data[pos];
        chunk_len = (size_t)data[pos + 1u] |
                    ((size_t)data[pos + 2u] << 8) |
                    ((size_t)data[pos + 3u] << 16);
        if (chunk_len > length - pos - 4u) {
            snprintf(error, error_cap,
                     "truncated chunk 0x%02x", type);
            return 0;
        }
        payload = data + pos + 4u;
        report->stream.chunk_count[type]++;
        report->stream.chunk_bytes[type] += chunk_len + 4u;
        switch (type) {
        case 0xffu:
            if (chunk_len < 6u ||
                memcmp(payload, "MinLz", 5u) != 0 ||
                (payload[5] & 0xc0u) != 0u ||
                (payload[5] & 15u) > 13u) {
                snprintf(error, error_cap,
                         "invalid MinLz stream identifier");
                return 0;
            }
            report->stream.declared_max_block =
                (size_t)1u << ((payload[5] & 15u) + 10u);
            report->stream.stream_ids++;
            stream_open = 1;
            stream_size = 0u;
            break;
        case 0x02u:
        case 0x03u: {
            uint64_t decoded = 0u;
            size_t used = 0u;
            if (!stream_open || chunk_len < 4u ||
                !mz_read_uvarint(payload + 4u, chunk_len - 4u,
                                 &decoded, &used) ||
                decoded > SIZE_MAX) {
                snprintf(error, error_cap,
                         "invalid compressed stream block");
                return 0;
            }
            if (!mz_analyze_block_bytes(
                    payload, chunk_len, report, options,
                    type, chunk_len, 4u + used, decoded,
                    error, error_cap)) {
                return 0;
            }
            report->stream.minlz_blocks++;
            if (type == 0x02u) {
                report->stream.crc_mode_02++;
            } else {
                report->stream.crc_mode_03++;
            }
            report->stream.block_size_hist[
                mz_bucket((uint32_t)(chunk_len > UINT32_MAX
                                         ? UINT32_MAX
                                         : chunk_len))]++;
            report->stream.uncompressed_size_hist[
                mz_bucket((uint32_t)(decoded > UINT32_MAX
                                         ? UINT32_MAX
                                         : decoded))]++;
            report->stream.ratio_hist[
                mz_ratio_bucket((size_t)decoded, chunk_len)]++;
            stream_size += decoded;
            break;
        }
        case 0x01u: {
            size_t decoded;
            if (!stream_open || chunk_len < 4u) {
                snprintf(error, error_cap,
                         "invalid uncompressed stream block");
                return 0;
            }
            decoded = chunk_len - 4u;
            report->stream.uncompressed_blocks++;
            report->stream.block_size_hist[
                mz_bucket((uint32_t)(chunk_len > UINT32_MAX
                                         ? UINT32_MAX
                                         : chunk_len))]++;
            report->stream.uncompressed_size_hist[
                mz_bucket((uint32_t)(decoded > UINT32_MAX
                                         ? UINT32_MAX
                                         : decoded))]++;
            report->stream.ratio_hist[
                mz_ratio_bucket(decoded, chunk_len)]++;
            stream_size += decoded;
            break;
        }
        case 0x00u:
            report->stream.legacy_blocks++;
            break;
        case 0x20u:
            if (chunk_len != 0u) {
                uint64_t declared = 0u;
                size_t used = 0u;
                if (!mz_read_uvarint(payload, chunk_len,
                                     &declared, &used) ||
                    used != chunk_len ||
                    declared != stream_size) {
                    snprintf(error, error_cap,
                             "EOF size mismatch");
                    return 0;
                }
            }
            stream_open = 0;
            break;
        default:
            if (type <= 0x3fu) {
                snprintf(error, error_cap,
                         "unknown non-skippable chunk 0x%02x", type);
                return 0;
            }
            break;
        }
        pos += 4u + chunk_len;
    }
    if (stream_open) {
        snprintf(error, error_cap,
                 "stream ended without EOF chunk");
        return 0;
    }
    return 1;
}

static int mz_file_size(FILE *file, int64_t *size) {
    long pos;
    long end;
    if (file == stdin) {
        *size = -1;
        return 1;
    }
    pos = ftell(file);
    if (pos < 0 || fseek(file, 0, SEEK_END) != 0) {
        return 0;
    }
    end = ftell(file);
    if (end < 0 || fseek(file, pos, SEEK_SET) != 0) {
        return 0;
    }
    *size = (int64_t)end;
    return 1;
}

static int mz_analyze_file(const char *name, mz_file_report *report,
                           const mz_stats_options *options,
                           char *error, size_t error_cap) {
    FILE *file = strcmp(name, "-") == 0 ? stdin : fopen(name, "rb");
    uint8_t *data = NULL;
    size_t length = 0u;
    clock_t start;
    int ok;
    if (!file) {
        snprintf(error, error_cap, "open failed: %s", strerror(errno));
        return 0;
    }
    memset(report, 0, sizeof(*report));
    report->name = name;
    if (!mz_file_size(file, &report->input_size)) {
        report->input_size = -1;
    }
    if (!mz_read_all(file, &data, &length)) {
        snprintf(error, error_cap, "read failed");
        if (file != stdin) {
            fclose(file);
        }
        return 0;
    }
    if (file != stdin) {
        fclose(file);
    }
    if (report->input_size < 0) {
        report->input_size = (int64_t)length;
    }
    if (length == 0u) {
        snprintf(error, error_cap, "empty input");
        free(data);
        return 0;
    }
    start = clock();
    if (data[0] == 0xffu) {
        ok = mz_analyze_stream(
            data, length, report, options, error, error_cap);
    } else if (data[0] == 0u) {
        ok = mz_analyze_raw(
            data, length, report, options, error, error_cap);
    } else {
        snprintf(error, error_cap,
                 "not a MinLZ stream or block (first byte 0x%02x)",
                 data[0]);
        ok = 0;
    }
    report->elapsed_seconds =
        (double)(clock() - start) / (double)CLOCKS_PER_SEC;
    free(data);
    return ok;
}

static void mz_human_size(char *dst, size_t cap, uint64_t bytes) {
    const double value = (double)bytes;
    if (bytes < 1024u) {
        snprintf(dst, cap, "%" PRIu64 " B", bytes);
    } else if (bytes < (1u << 20)) {
        snprintf(dst, cap, "%.2f KiB", value / 1024.0);
    } else if (bytes < (UINT64_C(1) << 30)) {
        snprintf(dst, cap, "%.2f MiB", value / (double)(1u << 20));
    } else {
        snprintf(dst, cap, "%.2f GiB",
                 value / (double)(UINT64_C(1) << 30));
    }
}

static void mz_abbrev_bound(char *dst, size_t cap, uint64_t value,
                            int upper) {
    uint64_t shown = upper ? value + 1u : value;
    if (shown >= (UINT64_C(1) << 20) &&
        (shown & ((UINT64_C(1) << 20) - 1u)) == 0u) {
        snprintf(dst, cap, "%" PRIu64 "M", shown >> 20);
    } else if (shown >= 1024u &&
               (shown & 1023u) == 0u) {
        snprintf(dst, cap, "%" PRIu64 "K", shown >> 10);
    } else {
        snprintf(dst, cap, "%" PRIu64, value);
    }
}

static void mz_bucket_label(char *dst, size_t cap, unsigned bucket) {
    uint64_t lo;
    uint64_t hi;
    if (bucket == 0u) {
        snprintf(dst, cap, "1");
        return;
    }
    lo = UINT64_C(1) << bucket;
    if (bucket >= MZ_STATS_HIST_BUCKETS - 1u) {
        char lo_text[24];
        mz_abbrev_bound(lo_text, sizeof(lo_text), lo, 0);
        snprintf(dst, cap, ">=%s", lo_text);
        return;
    }
    hi = (UINT64_C(1) << (bucket + 1u)) - 1u;
    {
        char lo_text[24];
        char hi_text[24];
        mz_abbrev_bound(lo_text, sizeof(lo_text), lo, 0);
        mz_abbrev_bound(hi_text, sizeof(hi_text), hi, 1);
        snprintf(dst, cap, "%s-%s", lo_text, hi_text);
    }
}

static void mz_render_hist(FILE *out, const uint64_t *hist,
                           uint64_t total) {
    size_t i;
    if (total == 0u) {
        return;
    }
    for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
        char label[48];
        if (hist[i] == 0u) {
            continue;
        }
        mz_bucket_label(label, sizeof(label), (unsigned)i);
        fprintf(out, "  %-18s %10" PRIu64 "  %5.1f%%\n",
                label, hist[i],
                (double)hist[i] * 100.0 / (double)total);
    }
}

static void mz_render_text(FILE *out, const mz_file_report *report,
                           const mz_stats_options *options) {
    char input_size[48];
    size_t i;
    mz_human_size(input_size, sizeof(input_size),
                  report->input_size < 0
                      ? 0u
                      : (uint64_t)report->input_size);
    if (!options->quiet) {
        fprintf(out, "\n========== %s ==========\n", report->name);
        fprintf(out, "Input size:  %s (%" PRId64 " bytes)\n",
                input_size, report->input_size);
        fprintf(out, "Format:      %s\n", report->format);
        fprintf(out, "Elapsed:     %.6f s\n",
                report->elapsed_seconds);
    }
    if (strcmp(report->format, "stream") == 0) {
        fprintf(out, "\n--- Stream ---\n");
        fprintf(out, "Declared max block size: %zu\n",
                report->stream.declared_max_block);
        fprintf(out, "Stream identifiers seen: %" PRIu64 "\n",
                report->stream.stream_ids);
        fprintf(out,
                "MinLZ blocks:            %" PRIu64
                " (0x02: %" PRIu64 ", 0x03: %" PRIu64 ")\n",
                report->stream.minlz_blocks,
                report->stream.crc_mode_02,
                report->stream.crc_mode_03);
        fprintf(out, "Uncompressed blocks:     %" PRIu64 "\n",
                report->stream.uncompressed_blocks);
        fprintf(out, "\nChunk type breakdown:\n");
        fprintf(out, "  %-4s %-26s %10s %14s\n",
                "id", "name", "count", "bytes");
        for (i = 0u; i < 256u; i++) {
            if (report->stream.chunk_count[i] != 0u) {
                fprintf(out,
                        "  0x%02zx %-26s %10" PRIu64
                        " %14" PRIu64 "\n",
                        i, mz_chunk_name((unsigned)i),
                        report->stream.chunk_count[i],
                        report->stream.chunk_bytes[i]);
            }
        }
    }
    fprintf(out, "\n--- Aggregate ---\n");
    fprintf(out, "Total ops:        %" PRIu64 "\n",
            report->total_ops);
    fprintf(out, "Output bytes:     %" PRIu64 "\n",
            report->total_output);
    fprintf(out, "Encoding bytes:   %" PRIu64 "\n",
            report->total_encoded);
    if (report->total_output != 0u) {
        fprintf(out, "Bytes/output:     %.4f\n",
                (double)report->total_encoded /
                    (double)report->total_output);
        if (report->total_ops != 0u) {
            fprintf(out, "Output/op:        %.2f bytes/op\n",
                    (double)report->total_output /
                        (double)report->total_ops);
            fprintf(out, "Ops/KB out:       %.2f\n",
                    (double)report->total_ops * 1024.0 /
                        (double)report->total_output);
        }
        fprintf(out, "Literal bytes:    %" PRIu64 " (%.1f%%)\n",
                report->total_literal,
                (double)report->total_literal * 100.0 /
                    (double)report->total_output);
        fprintf(out, "Copy bytes:       %" PRIu64 " (%.1f%%)\n",
                report->total_copy,
                (double)report->total_copy * 100.0 /
                    (double)report->total_output);
        fprintf(out, "Repeat bytes:     %" PRIu64 " (%.1f%%)\n",
                report->total_repeat,
                (double)report->total_repeat * 100.0 /
                    (double)report->total_output);
    }
    fprintf(out, "Longest copy:     %u bytes\n",
            report->longest_copy);
    fprintf(out, "Longest lit run:  %u bytes\n",
            report->longest_literal_run);
    fprintf(out, "\n--- Per-op breakdown ---\n");
    fprintf(out,
            "%-8s %10s %12s %12s %8s %8s %12s\n",
            "Type", "Count", "OutBytes", "EncBytes",
            "AvgOut", "AvgEnc", "Saved");
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        const mz_op_stats *stats = &report->op_totals[i];
        uint64_t encoded;
        int64_t saved = 0;
        if (stats->count == 0u) {
            continue;
        }
        encoded = stats->header_bytes;
        if (i == MZ_OP_LITERAL || i == MZ_OP_FUSED_LITERAL) {
            encoded += stats->out_bytes;
        } else {
            saved = (int64_t)stats->out_bytes -
                    (int64_t)encoded;
        }
        fprintf(out,
                "%-8s %10" PRIu64 " %12" PRIu64
                " %12" PRIu64 " %8.2f %8.2f %12" PRId64 "\n",
                mz_op_names[i], stats->count, stats->out_bytes,
                encoded,
                (double)stats->out_bytes / (double)stats->count,
                (double)encoded / (double)stats->count,
                saved);
    }
    if (report->op_totals[MZ_OP_COPY2_FUSED].count != 0u ||
        report->op_totals[MZ_OP_COPY3_FUSED].count != 0u) {
        const int fused_ops[2] = {
            MZ_OP_COPY2_FUSED, MZ_OP_COPY3_FUSED
        };
        size_t fi;
        fprintf(out, "\n--- Fused literal stats ---\n");
        for (fi = 0u; fi < 2u; fi++) {
            const mz_op_stats *stats =
                &report->op_totals[fused_ops[fi]];
            size_t length;
            if (stats->count == 0u) {
                continue;
            }
            fprintf(out,
                    "%s: %" PRIu64 " ops, %" PRIu64
                    " fused-lit bytes (%.2f avg/op)\n",
                    mz_op_names[fused_ops[fi]], stats->count,
                    stats->fused_literal_bytes,
                    (double)stats->fused_literal_bytes /
                        (double)stats->count);
            for (length = 0u; length < 5u; length++) {
                if (stats->fused_len_hist[length] != 0u) {
                    fprintf(out,
                            "    lit=%zu: %" PRIu64 " (%.1f%%)\n",
                            length, stats->fused_len_hist[length],
                            (double)stats->fused_len_hist[length] *
                                100.0 / (double)stats->count);
                }
            }
        }
    }
    fprintf(out, "\n--- Header-size distribution (bytes/op) ---\n");
    fprintf(out, "%-8s", "Type");
    for (i = 1u; i <= 7u; i++) {
        fprintf(out, " %8zu", i);
    }
    fputc('\n', out);
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        const mz_op_stats *stats = &report->op_totals[i];
        size_t header;
        if (i == MZ_OP_FUSED_LITERAL || stats->count == 0u) {
            continue;
        }
        fprintf(out, "%-8s", mz_op_names[i]);
        for (header = 1u; header <= 7u; header++) {
            fprintf(out, " %8" PRIu64,
                    stats->header_hist[header]);
        }
        fputc('\n', out);
    }
    if (report->op_totals[MZ_OP_REPEAT].count != 0u) {
        uint64_t total_lineage = 0u;
        fprintf(out,
                "\n--- Repeat lineage (op preceding each repeat) ---\n");
        for (i = 0u; i < MZ_STATS_OPS; i++) {
            total_lineage += report->repeat_previous[i];
        }
        for (i = 0u; i < MZ_STATS_OPS; i++) {
            if (report->repeat_previous[i] != 0u) {
                fprintf(out, "%-8s %10" PRIu64 "  %5.1f%%\n",
                        mz_op_names[i], report->repeat_previous[i],
                        total_lineage
                            ? (double)report->repeat_previous[i] *
                                  100.0 / (double)total_lineage
                            : 0.0);
            }
        }
    }
    if (!options->no_hist) {
        fprintf(out, "\n--- Length histograms ---\n");
        for (i = 0u; i < MZ_STATS_OPS; i++) {
            const mz_op_stats *stats = &report->op_totals[i];
            if (stats->count == 0u) {
                continue;
            }
            fprintf(out, "\n%s: count=%" PRIu64
                    " min=%u max=%u avg=%.2f\n",
                    mz_op_names[i], stats->count,
                    stats->min_len, stats->max_len,
                    (double)stats->sum_len /
                        (double)stats->count);
            mz_render_hist(out, stats->len_hist, stats->count);
        }
        fprintf(out, "\n--- Offset histograms ---\n");
        for (i = 0u; i < MZ_STATS_OPS; i++) {
            const mz_op_stats *stats = &report->op_totals[i];
            if (i == MZ_OP_LITERAL ||
                i == MZ_OP_FUSED_LITERAL ||
                stats->count == 0u) {
                continue;
            }
            fprintf(out, "\n%s: count=%" PRIu64
                    " min=%u max=%u avg=%.2f\n",
                    mz_op_names[i], stats->count,
                    stats->min_offset, stats->max_offset,
                    (double)stats->sum_offset /
                        (double)stats->count);
            mz_render_hist(
                out, stats->offset_hist, stats->count);
        }
        if (strcmp(report->format, "stream") == 0) {
            static const char *const ratio_names[11] = {
                "0-10%", "10-20%", "20-30%", "30-40%",
                "40-50%", "50-60%", "60-70%", "70-80%",
                "80-90%", "90-100%", ">=100%"
            };
            uint64_t compressed_total = 0u;
            uint64_t uncompressed_total = 0u;
            uint64_t ratio_total = 0u;
            for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
                compressed_total +=
                    report->stream.block_size_hist[i];
                uncompressed_total +=
                    report->stream.uncompressed_size_hist[i];
            }
            for (i = 0u; i < 11u; i++) {
                ratio_total += report->stream.ratio_hist[i];
            }
            fprintf(out,
                    "\n--- Block size histogram "
                    "(compressed chunk bytes) ---\n");
            mz_render_hist(out, report->stream.block_size_hist,
                           compressed_total);
            fprintf(out,
                    "\n--- Block size histogram "
                    "(uncompressed bytes) ---\n");
            mz_render_hist(out,
                           report->stream.uncompressed_size_hist,
                           uncompressed_total);
            fprintf(out,
                    "\n--- Per-block ratio buckets "
                    "(comp/uncomp) ---\n");
            for (i = 0u; i < 11u; i++) {
                if (report->stream.ratio_hist[i] != 0u) {
                    fprintf(out, "  %-8s %8" PRIu64 "  %.1f%%\n",
                            ratio_names[i],
                            report->stream.ratio_hist[i],
                            ratio_total
                                ? (double)report->stream.ratio_hist[i] *
                                      100.0 / (double)ratio_total
                                : 0.0);
                }
            }
        }
    }
    if (options->per_block && report->block_count != 0u) {
        fprintf(out, "\n--- Per-block summary ---\n");
        fprintf(out,
                "%-6s %-6s %10s %10s %8s %8s   %s\n",
                "idx", "ct", "comp", "uncomp", "ratio", "ops",
                "L/R/C1/C2/C2F/C3/C3F/FL");
        for (i = 0u; i < report->block_count; i++) {
            const mz_block_stats *block = &report->blocks[i];
            double ratio = block->uncompressed_size
                               ? (double)block->compressed_size *
                                     100.0 /
                                     (double)block->uncompressed_size
                               : 0.0;
            fprintf(out,
                    "%-6zu 0x%02x   %10zu %10zu %7.1f%% %8" PRIu64
                    "   %" PRIu64 "/%" PRIu64 "/%" PRIu64
                    "/%" PRIu64 "/%" PRIu64 "/%" PRIu64
                    "/%" PRIu64 "/%" PRIu64 "\n",
                    block->index, block->chunk_type,
                    block->compressed_size, block->uncompressed_size,
                    ratio, mz_block_op_count(block),
                    block->ops[0].count, block->ops[1].count,
                    block->ops[2].count, block->ops[3].count,
                    block->ops[4].count, block->ops[5].count,
                    block->ops[6].count, block->ops[7].count);
        }
    }
}

static void mz_json_string(FILE *out, const char *value) {
    const unsigned char *p = (const unsigned char *)value;
    fputc('"', out);
    while (*p) {
        switch (*p) {
        case '"': fputs("\\\"", out); break;
        case '\\': fputs("\\\\", out); break;
        case '\b': fputs("\\b", out); break;
        case '\f': fputs("\\f", out); break;
        case '\n': fputs("\\n", out); break;
        case '\r': fputs("\\r", out); break;
        case '\t': fputs("\\t", out); break;
        default:
            if (*p < 0x20u) {
                fprintf(out, "\\u%04x", *p);
            } else {
                fputc(*p, out);
            }
            break;
        }
        p++;
    }
    fputc('"', out);
}

static void mz_render_json_file(FILE *out,
                                const mz_file_report *report,
                                const mz_stats_options *options) {
    size_t i;
    fputs("{\n      \"name\": ", out);
    mz_json_string(out, report->name);
    fprintf(out,
            ",\n      \"inputSize\": %" PRId64
            ",\n      \"format\": ",
            report->input_size);
    mz_json_string(out, report->format);
    fprintf(out,
            ",\n      \"elapsedSeconds\": %.9f,"
            "\n      \"totals\": {"
            "\"ops\": %" PRIu64 ", \"outputBytes\": %" PRIu64
            ", \"encodingBytes\": %" PRIu64
            ", \"literalBytes\": %" PRIu64
            ", \"copyBytes\": %" PRIu64
            ", \"repeatBytes\": %" PRIu64
            ", \"longestCopy\": %u, \"longestLitRun\": %u},"
            "\n      \"ops\": [",
            report->elapsed_seconds, report->total_ops,
            report->total_output, report->total_encoded,
            report->total_literal, report->total_copy,
            report->total_repeat, report->longest_copy,
            report->longest_literal_run);
    {
        int first = 1;
        for (i = 0u; i < MZ_STATS_OPS; i++) {
            const mz_op_stats *stats = &report->op_totals[i];
            if (stats->count == 0u) {
                continue;
            }
            fputs(first ? "\n        " : ",\n        ", out);
            first = 0;
            fprintf(out,
                    "{\"type\":\"%s\",\"count\":%" PRIu64
                    ",\"outBytes\":%" PRIu64
                    ",\"headerBytes\":%" PRIu64
                    ",\"minLen\":%u,\"maxLen\":%u"
                    ",\"minOff\":%u,\"maxOff\":%u"
                    ",\"headerSizeHist\":[",
                    mz_op_names[i], stats->count,
                    stats->out_bytes, stats->header_bytes,
                    stats->min_len, stats->max_len,
                    stats->min_offset, stats->max_offset);
            {
                size_t j;
                for (j = 1u; j <= 7u; j++) {
                    fprintf(out, "%s%" PRIu64,
                            j == 1u ? "" : ",",
                            stats->header_hist[j]);
                }
                fputc(']', out);
                if (!options->no_hist) {
                    fputs(",\"lenHist\":[", out);
                    for (j = 0u; j < MZ_STATS_HIST_BUCKETS; j++) {
                        fprintf(out, "%s%" PRIu64,
                                j == 0u ? "" : ",",
                                stats->len_hist[j]);
                    }
                    fputs("],\"offHist\":[", out);
                    for (j = 0u; j < MZ_STATS_HIST_BUCKETS; j++) {
                        fprintf(out, "%s%" PRIu64,
                                j == 0u ? "" : ",",
                                stats->offset_hist[j]);
                    }
                    fputc(']', out);
                }
                fputc('}', out);
            }
        }
        if (!first) {
            fputc('\n', out);
        }
    }
    fputs("      ]", out);
    if (report->op_totals[MZ_OP_COPY2_FUSED].count != 0u ||
        report->op_totals[MZ_OP_COPY3_FUSED].count != 0u) {
        const int fused_ops[2] = {
            MZ_OP_COPY2_FUSED, MZ_OP_COPY3_FUSED
        };
        size_t fi;
        int first = 1;
        fputs(",\n      \"fusedLitBreakdown\": [", out);
        for (fi = 0u; fi < 2u; fi++) {
            const mz_op_stats *stats =
                &report->op_totals[fused_ops[fi]];
            size_t j;
            if (stats->count == 0u) {
                continue;
            }
            fprintf(out,
                    "%s{\"type\":\"%s\",\"count\":%" PRIu64
                    ",\"bytes\":%" PRIu64 ",\"lengths\":[",
                    first ? "" : ",", mz_op_names[fused_ops[fi]],
                    stats->count, stats->fused_literal_bytes);
            first = 0;
            for (j = 0u; j < 5u; j++) {
                fprintf(out, "%s%" PRIu64,
                        j == 0u ? "" : ",",
                        stats->fused_len_hist[j]);
            }
            fputs("]}", out);
        }
        fputc(']', out);
    }
    if (report->op_totals[MZ_OP_REPEAT].count != 0u) {
        int first = 1;
        fputs(",\n      \"repeatLineage\": [", out);
        for (i = 0u; i < MZ_STATS_OPS; i++) {
            if (report->repeat_previous[i] != 0u) {
                fprintf(out,
                        "%s{\"prevType\":\"%s\",\"count\":%" PRIu64 "}",
                        first ? "" : ",", mz_op_names[i],
                        report->repeat_previous[i]);
                first = 0;
            }
        }
        fputc(']', out);
    }
    if (strcmp(report->format, "stream") == 0) {
        int first = 1;
        fprintf(out,
                ",\n      \"stream\": {"
                "\"declaredMaxBlockSize\":%zu,"
                "\"streamIdentifiers\":%" PRIu64 ","
                "\"minlzBlocks\":%" PRIu64 ","
                "\"uncompressedBlocks\":%" PRIu64 ","
                "\"chunkTypes\":[",
                report->stream.declared_max_block,
                report->stream.stream_ids,
                report->stream.minlz_blocks,
                report->stream.uncompressed_blocks);
        for (i = 0u; i < 256u; i++) {
            if (report->stream.chunk_count[i] == 0u) {
                continue;
            }
            fprintf(out,
                    "%s{\"id\":\"0x%02zx\",\"name\":\"%s\","
                    "\"count\":%" PRIu64 ",\"bytes\":%" PRIu64 "}",
                    first ? "" : ",", i, mz_chunk_name((unsigned)i),
                    report->stream.chunk_count[i],
                    report->stream.chunk_bytes[i]);
            first = 0;
        }
        fputs("]}", out);
    }
    if (options->per_block) {
        fputs(",\n      \"blocks\": [", out);
        for (i = 0u; i < report->block_count; i++) {
            const mz_block_stats *block = &report->blocks[i];
            fprintf(out,
                    "%s{\"index\":%zu,\"chunkType\":\"0x%02x\","
                    "\"compSize\":%zu,\"uncompSize\":%zu,"
                    "\"ops\":%" PRIu64 "}",
                    i == 0u ? "" : ",", block->index,
                    block->chunk_type, block->compressed_size,
                    block->uncompressed_size,
                    mz_block_op_count(block));
        }
        fputc(']', out);
    }
    fputs("\n    }", out);
}

static void mz_csv_field(FILE *out, const char *value) {
    const char *p;
    int quote = strchr(value, ',') != NULL ||
                strchr(value, '"') != NULL ||
                strchr(value, '\n') != NULL ||
                strchr(value, '\r') != NULL;
    if (!quote) {
        fputs(value, out);
        return;
    }
    fputc('"', out);
    for (p = value; *p; p++) {
        if (*p == '"') {
            fputc('"', out);
        }
        fputc(*p, out);
    }
    fputc('"', out);
}

static void mz_render_csv(FILE *out, const mz_file_report *report,
                          const mz_stats_options *options) {
    size_t i;
    fputs("# file,", out);
    mz_csv_field(out, report->name);
    fprintf(out, ",format,%s,inputSize,%" PRId64 "\n",
            report->format, report->input_size);
    fputs("# section,per-op\n"
          "type,count,outBytes,headerBytes,fusedLitBytes,fusedOps,"
          "minLen,maxLen,sumLen,minOff,maxOff,sumOff\n",
          out);
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        const mz_op_stats *stats = &report->op_totals[i];
        fprintf(out,
                "%s,%" PRIu64 ",%" PRIu64 ",%" PRIu64
                ",%" PRIu64 ",%" PRIu64 ",%u,%u,%" PRIu64
                ",%u,%u,%" PRIu64 "\n",
                mz_op_names[i], stats->count, stats->out_bytes,
                stats->header_bytes, stats->fused_literal_bytes,
                stats->fused_ops, stats->min_len, stats->max_len,
                stats->sum_len, stats->min_offset,
                stats->max_offset, stats->sum_offset);
    }
    fputs("# section,len-hist\n"
          "type", out);
    for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
        char label[48];
        mz_bucket_label(label, sizeof(label), (unsigned)i);
        fprintf(out, ",%s", label);
    }
    fputc('\n', out);
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        size_t j;
        fprintf(out, "%s", mz_op_names[i]);
        for (j = 0u; j < MZ_STATS_HIST_BUCKETS; j++) {
            fprintf(out, ",%" PRIu64,
                    report->op_totals[i].len_hist[j]);
        }
        fputc('\n', out);
    }
    fputs("# section,off-hist\ntype", out);
    for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
        char label[48];
        mz_bucket_label(label, sizeof(label), (unsigned)i);
        fprintf(out, ",%s", label);
    }
    fputc('\n', out);
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        size_t j;
        fprintf(out, "%s", mz_op_names[i]);
        for (j = 0u; j < MZ_STATS_HIST_BUCKETS; j++) {
            fprintf(out, ",%" PRIu64,
                    report->op_totals[i].offset_hist[j]);
        }
        fputc('\n', out);
    }
    fputs("# section,header-size-hist\n"
          "type,1,2,3,4,5,6,7\n", out);
    for (i = 0u; i < MZ_STATS_OPS; i++) {
        size_t j;
        fprintf(out, "%s", mz_op_names[i]);
        for (j = 1u; j <= 7u; j++) {
            fprintf(out, ",%" PRIu64,
                    report->op_totals[i].header_hist[j]);
        }
        fputc('\n', out);
    }
    if (strcmp(report->format, "stream") == 0) {
        fputs("# section,chunk-types\nid,name,count,bytes\n", out);
        for (i = 0u; i < 256u; i++) {
            if (report->stream.chunk_count[i] != 0u) {
                fprintf(out, "0x%02zx,%s,%" PRIu64 ",%" PRIu64 "\n",
                        i, mz_chunk_name((unsigned)i),
                        report->stream.chunk_count[i],
                        report->stream.chunk_bytes[i]);
            }
        }
        fputs("# section,block-size-hist\ntype", out);
        for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
            char label[48];
            mz_bucket_label(label, sizeof(label), (unsigned)i);
            fprintf(out, ",%s", label);
        }
        fputs("\ncompressed", out);
        for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
            fprintf(out, ",%" PRIu64,
                    report->stream.block_size_hist[i]);
        }
        fputs("\nuncompressed", out);
        for (i = 0u; i < MZ_STATS_HIST_BUCKETS; i++) {
            fprintf(out, ",%" PRIu64,
                    report->stream.uncompressed_size_hist[i]);
        }
        fputc('\n', out);
    }
    if (options->per_block) {
        fputs("# section,per-block\n"
              "index,chunkType,compSize,uncompSize,ops,"
              "lit,rep,c1,c2,c2f,c3,c3f,fl\n",
              out);
        for (i = 0u; i < report->block_count; i++) {
            const mz_block_stats *block = &report->blocks[i];
            size_t j;
            fprintf(out, "%zu,0x%02x,%zu,%zu,%" PRIu64,
                    block->index, block->chunk_type,
                    block->compressed_size,
                    block->uncompressed_size,
                    mz_block_op_count(block));
            for (j = 0u; j < MZ_STATS_OPS; j++) {
                fprintf(out, ",%" PRIu64,
                        block->ops[j].count);
            }
            fputc('\n', out);
        }
    }
    fputc('\n', out);
}

static void mz_stats_usage(const char *argv0) {
    fprintf(stderr,
            "Analyze a MinLZ stream (.mz) or raw block (.mzb).\n"
            "Usage: %s stats [options] <input...>\n"
            "Options: -json -csv -per-block -no-hist -q\n",
            argv0);
}

int mz_run_stats(int argc, char **argv) {
    mz_stats_options options;
    mz_file_report *reports;
    char **files = NULL;
    size_t files_cap = 0u;
    size_t report_count = 0u;
    size_t file_count = 0u;
    int i;
    int failures = 0;
    memset(&options, 0, sizeof(options));
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-json") == 0) {
            options.json = 1;
        } else if (strcmp(argv[i], "-csv") == 0) {
            options.csv = 1;
        } else if (strcmp(argv[i], "-per-block") == 0) {
            options.per_block = 1;
        } else if (strcmp(argv[i], "-no-hist") == 0) {
            options.no_hist = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            options.quiet = 1;
        } else if (strcmp(argv[i], "-help") == 0 ||
                   strcmp(argv[i], "--help") == 0) {
            mz_stats_usage(argv[0]);
            mz_stats_free_files(files, file_count);
            return 0;
        } else if (argv[i][0] == '-' &&
                   strcmp(argv[i], "-") != 0) {
            fprintf(stderr, "ERROR: unknown stats option %s\n", argv[i]);
            mz_stats_free_files(files, file_count);
            return 1;
        } else {
            if (!mz_stats_expand_file(
                    &files, &file_count, &files_cap, argv[i])) {
                fprintf(stderr,
                        "ERROR: no files matched %s\n", argv[i]);
                failures = 1;
            }
        }
    }
    if (options.json && options.csv) {
        fprintf(stderr, "ERROR: -json and -csv are mutually exclusive\n");
        mz_stats_free_files(files, file_count);
        return 1;
    }
    if (file_count == 0u) {
        mz_stats_usage(argv[0]);
        mz_stats_free_files(files, file_count);
        return 1;
    }
    reports = (mz_file_report *)calloc(
        file_count, sizeof(*reports));
    if (!reports) {
        mz_stats_free_files(files, file_count);
        return 2;
    }
    for (i = 0; (size_t)i < file_count; i++) {
        char error[256];
        if (!mz_analyze_file(files[i], &reports[report_count],
                             &options, error, sizeof(error))) {
            fprintf(stderr, "%s: %s\n", files[i], error);
            failures++;
            continue;
        }
        report_count++;
    }
    if (options.json) {
        size_t j;
        fputs("{\n  \"files\": [", stdout);
        for (j = 0u; j < report_count; j++) {
            fputs(j == 0u ? "\n    " : ",\n    ", stdout);
            mz_render_json_file(stdout, &reports[j], &options);
        }
        if (report_count != 0u) {
            fputc('\n', stdout);
        }
        fputs("  ]\n}\n", stdout);
    } else {
        size_t j;
        for (j = 0u; j < report_count; j++) {
            if (options.csv) {
                mz_render_csv(stdout, &reports[j], &options);
            } else {
                mz_render_text(stdout, &reports[j], &options);
            }
        }
    }
    for (i = 0; (size_t)i < report_count; i++) {
        free(reports[i].blocks);
    }
    mz_stats_free_files(files, file_count);
    free(reports);
    return failures && report_count == 0u ? 2 : 0;
}
