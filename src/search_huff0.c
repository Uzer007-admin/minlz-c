/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 */
#include "minlz_internal.h"

#include "third_party/zstd/common/huf.h"
#include "third_party/zstd/compress/hist.h"

#include <stdlib.h>

#define MINLZ_HUFF0_TABLE_COUNT 16u
#define MINLZ_HUFF0_TABLE_LOG 11u
#define MINLZ_HUFF0_DTABLE_WORDS HUF_DTABLE_SIZE(MINLZ_HUFF0_TABLE_LOG)

typedef struct minlz_huff0_table {
    HUF_DTable words[MINLZ_HUFF0_DTABLE_WORDS];
    int valid;
} minlz_huff0_table;

struct minlz_huff0_decoder {
    minlz_huff0_table tables[MINLZ_HUFF0_TABLE_COUNT];
    U32 workspace[HUF_DECOMPRESS_WORKSPACE_SIZE_U32];
};

typedef struct minlz_huff0_ctable_context {
    HUF_CElt table[HUF_CTABLE_SIZE_ST(HUF_SYMBOLVALUE_MAX)];
    U32 workspace[(HUF_WORKSPACE_SIZE + sizeof(U32) - 1u) /
                  sizeof(U32)];
} minlz_huff0_ctable_context;

static minlz_status minlz_huff0_histogram(
    const uint8_t *src, size_t src_len, unsigned histogram[256],
    unsigned *max_symbol, size_t *largest) {
    U32 workspace[HIST_WKSP_SIZE_U32];
    unsigned symbol = HUF_SYMBOLVALUE_MAX;
    size_t n;
    memset(histogram, 0, 256u * sizeof(histogram[0]));
    n = HIST_count_wksp(
        histogram, &symbol, src, src_len,
        workspace, sizeof(workspace));
    if (HIST_isError(n)) {
        return MINLZ_CORRUPT;
    }
    *max_symbol = symbol;
    *largest = n;
    return MINLZ_OK;
}

static minlz_status minlz_huff0_build_ctable(
    minlz_huff0_ctable_context *ctx,
    const unsigned histogram[256], unsigned max_symbol,
    size_t src_len, uint8_t **serialized,
    size_t *serialized_len) {
    unsigned table_log;
    size_t max_bits;
    size_t table_len;
    uint8_t *table_bytes;
    if (!ctx || !serialized || !serialized_len || src_len == 0u) {
        return MINLZ_CORRUPT;
    }
    *serialized = NULL;
    *serialized_len = 0u;
    memset(ctx, 0, sizeof(*ctx));
    /*
     * klauspost/huff0 uses tableLogDefault (11) for the 32/64 KiB
     * sub-blocks selected by the v1.2 search format. Using zstd's newer
     * depth heuristic here changes the serialized tree and can cost bytes.
     */
    table_log = MINLZ_HUFF0_TABLE_LOG;
    max_bits = HUF_buildCTable_wksp(
        ctx->table, histogram, max_symbol, table_log,
        ctx->workspace, sizeof(ctx->workspace));
    if (HUF_isError(max_bits)) {
        return MINLZ_UNSUPPORTED;
    }
    table_bytes = (uint8_t *)malloc(HUF_CTABLEBOUND);
    if (!table_bytes) {
        return MINLZ_IO;
    }
    table_len = HUF_writeCTable_wksp(
        table_bytes, HUF_CTABLEBOUND, ctx->table,
        max_symbol, (unsigned)max_bits,
        ctx->workspace, sizeof(ctx->workspace));
    if (HUF_isError(table_len) || table_len == 0u) {
        free(table_bytes);
        return MINLZ_UNSUPPORTED;
    }
    *serialized = table_bytes;
    *serialized_len = table_len;
    return MINLZ_OK;
}

static minlz_status minlz_huff0_compress_with_table(
    const minlz_huff0_ctable_context *ctx,
    const uint8_t *src, size_t src_len,
    uint8_t **dst, size_t *dst_len) {
    size_t cap;
    size_t n;
    uint8_t *out;
    if (!ctx || !src || !dst || !dst_len || src_len == 0u) {
        return MINLZ_CORRUPT;
    }
    *dst = NULL;
    *dst_len = 0u;
    cap = HUF_compressBound(src_len);
    out = (uint8_t *)malloc(cap);
    if (!out) {
        return MINLZ_IO;
    }
    n = HUF_compress4X_usingCTable(
        out, cap, src, src_len, ctx->table, 0);
    if (HUF_isError(n) || n <= 1u || n >= src_len) {
        free(out);
        return MINLZ_UNSUPPORTED;
    }
    *dst = out;
    *dst_len = n;
    return MINLZ_OK;
}

minlz_status minlz_huff0_decoder_create(minlz_huff0_decoder **decoder) {
    minlz_huff0_decoder *out;
    if (!decoder) {
        return MINLZ_CORRUPT;
    }
    *decoder = NULL;
    out = (minlz_huff0_decoder *)calloc(1u, sizeof(*out));
    if (!out) {
        return MINLZ_IO;
    }
    *decoder = out;
    return MINLZ_OK;
}

void minlz_huff0_decoder_destroy(minlz_huff0_decoder *decoder) {
    free(decoder);
}

minlz_status minlz_huff0_decoder_read_table(
    minlz_huff0_decoder *decoder, unsigned table_index,
    const uint8_t *src, size_t src_len, size_t *used) {
    minlz_huff0_table *table;
    size_t n;
    if (!decoder || !src || !used ||
        table_index >= MINLZ_HUFF0_TABLE_COUNT) {
        return MINLZ_CORRUPT;
    }
    *used = 0u;
    table = &decoder->tables[table_index];
    memset(table, 0, sizeof(*table));
    /*
     * HUF_CREATE_STATIC_DTABLEX1(name, 12) initializes the first word to
     * an X1 table with maximum runtime table log 11. Keep the allocation
     * dynamic here so a 0x46 parser only pays for Huff0 when a table exists.
     */
    table->words[0] =
        (HUF_DTable)(MINLZ_HUFF0_TABLE_LOG * UINT32_C(0x01000001));
    n = HUF_readDTableX1_wksp(
        table->words, src, src_len,
        decoder->workspace, sizeof(decoder->workspace), 0);
    if (HUF_isError(n) || n == 0u || n > src_len) {
        return MINLZ_CORRUPT;
    }
    table->valid = 1;
    *used = n;
    return MINLZ_OK;
}

minlz_status minlz_huff0_decoder_decode(
    minlz_huff0_decoder *decoder, unsigned table_index,
    uint8_t *dst, size_t dst_len,
    const uint8_t *src, size_t src_len) {
    size_t n;
    if (!decoder || !dst || (!src && src_len != 0u) ||
        table_index >= MINLZ_HUFF0_TABLE_COUNT ||
        !decoder->tables[table_index].valid) {
        return MINLZ_CORRUPT;
    }
    n = HUF_decompress4X_usingDTable(
        dst, dst_len, src, src_len,
        decoder->tables[table_index].words, 0);
    if (HUF_isError(n) || n != dst_len) {
        return MINLZ_CORRUPT;
    }
    return MINLZ_OK;
}

minlz_status minlz_huff0_encode_own(
    const uint8_t *src, size_t src_len,
    minlz_huff0_candidate *candidate) {
    minlz_huff0_ctable_context ctx;
    unsigned max_symbol;
    size_t largest;
    minlz_status st;
    if (!src || !candidate || src_len == 0u) {
        return MINLZ_CORRUPT;
    }
    memset(candidate, 0, sizeof(*candidate));
    st = minlz_huff0_histogram(
        src, src_len, candidate->histogram,
        &max_symbol, &largest);
    if (st != MINLZ_OK || largest == src_len) {
        return st == MINLZ_OK ? MINLZ_UNSUPPORTED : st;
    }
    st = minlz_huff0_build_ctable(
        &ctx, candidate->histogram, max_symbol, src_len,
        &candidate->table, &candidate->table_len);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_huff0_compress_with_table(
        &ctx, src, src_len,
        &candidate->data, &candidate->data_len);
    if (st != MINLZ_OK) {
        minlz_huff0_candidate_destroy(candidate);
    }
    return st;
}

minlz_status minlz_huff0_encode_shared(
    const uint8_t *src, size_t block_size, size_t block_count,
    const minlz_huff0_candidate *own,
    minlz_huff0_shared *shared) {
    minlz_huff0_ctable_context ctx;
    unsigned histogram[256];
    unsigned max_symbol = 0u;
    size_t total_len;
    size_t i;
    size_t symbol;
    minlz_status st;
    if (!src || !own || !shared || block_size == 0u ||
        block_count < 2u || block_count > MINLZ_HUFF0_TABLE_COUNT ||
        block_count > SIZE_MAX / block_size) {
        return MINLZ_CORRUPT;
    }
    memset(shared, 0, sizeof(*shared));
    memset(histogram, 0, sizeof(histogram));
    for (i = 0; i < block_count; i++) {
        for (symbol = 0; symbol < 256u; symbol++) {
            histogram[symbol] += own[i].histogram[symbol];
            if (histogram[symbol] != 0u) {
                max_symbol = (unsigned)symbol;
            }
        }
    }
    total_len = block_size * block_count;
    st = minlz_huff0_build_ctable(
        &ctx, histogram, max_symbol, total_len,
        &shared->table, &shared->table_len);
    if (st != MINLZ_OK) {
        return st;
    }
    for (i = 0; i < block_count; i++) {
        unsigned block_max_symbol = 0u;
        for (symbol = 0; symbol < 256u; symbol++) {
            if (own[i].histogram[symbol] != 0u) {
                block_max_symbol = (unsigned)symbol;
            }
        }
        if (!HUF_validateCTable(
                ctx.table, own[i].histogram,
                block_max_symbol)) {
            continue;
        }
        st = minlz_huff0_compress_with_table(
            &ctx, src + i * block_size, block_size,
            &shared->data[i], &shared->data_len[i]);
        if (st == MINLZ_OK) {
            shared->valid[i] = 1u;
        } else if (st != MINLZ_UNSUPPORTED) {
            minlz_huff0_shared_destroy(shared);
            return st;
        }
    }
    return MINLZ_OK;
}

void minlz_huff0_candidate_destroy(
    minlz_huff0_candidate *candidate) {
    if (!candidate) {
        return;
    }
    free(candidate->table);
    free(candidate->data);
    candidate->table = NULL;
    candidate->data = NULL;
    candidate->table_len = 0u;
    candidate->data_len = 0u;
}

void minlz_huff0_shared_destroy(minlz_huff0_shared *shared) {
    size_t i;
    if (!shared) {
        return;
    }
    free(shared->table);
    shared->table = NULL;
    shared->table_len = 0u;
    for (i = 0; i < MINLZ_HUFF0_TABLE_COUNT; i++) {
        free(shared->data[i]);
        shared->data[i] = NULL;
        shared->data_len[i] = 0u;
        shared->valid[i] = 0u;
    }
}
