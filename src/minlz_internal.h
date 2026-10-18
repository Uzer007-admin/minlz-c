/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#ifndef MINLZ_INTERNAL_H
#define MINLZ_INTERNAL_H

#include "minlz.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define MINLZ_TAG_LITERAL 0x00u
#define MINLZ_TAG_REPEAT 0x04u
#define MINLZ_TAG_COPY1 0x01u
#define MINLZ_TAG_COPY2 0x02u
#define MINLZ_TAG_COPY3 0x07u
#define MINLZ_TAG_COPY2_FUSED 0x03u

#define MINLZ_MAX_COPY1_OFFSET 1024u
#define MINLZ_MIN_COPY2_OFFSET 64u
#define MINLZ_MAX_COPY2_OFFSET (MINLZ_MIN_COPY2_OFFSET + 65535u)
#define MINLZ_MAX_COPY2_LITS 4u
#define MINLZ_MIN_COPY3_OFFSET 65536u
#define MINLZ_MAX_COPY3_OFFSET ((2u << 20) + 65535u)
#define MINLZ_MAX_COPY3_LITS 3u
#define MINLZ_MIN_NON_LITERAL_BLOCK_SIZE 16u

#define MINLZ_CHUNK_LEGACY_COMPRESSED 0x00u
#define MINLZ_CHUNK_UNCOMPRESSED 0x01u
#define MINLZ_CHUNK_MINLZ 0x02u
#define MINLZ_CHUNK_MINLZ_COMP_CRC 0x03u
#define MINLZ_CHUNK_EOF 0x20u
#define MINLZ_CHUNK_INDEX 0x40u
#define MINLZ_CHUNK_SEARCH_INFO 0x44u
#define MINLZ_CHUNK_SEARCH_TABLE 0x45u
#define MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED 0x46u
#define MINLZ_CHUNK_REMOTE_BLOCK_REF 0x47u
#define MINLZ_CHUNK_PADDING 0xfeu
#define MINLZ_CHUNK_STREAM_ID 0xffu
#define MINLZ_MAX_NON_SKIPPABLE_CHUNK 0x3fu
#define MINLZ_MIN_USER_SKIPPABLE_CHUNK 0x80u
#define MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK 0xfdu
#define MINLZ_MAX_USER_CHUNK_SIZE ((1u << 24) - 1u)

#define MINLZ_CHECKSUM_SIZE 4u
#define MINLZ_CHUNK_HEADER_SIZE 4u

#if defined(__GNUC__) || defined(__clang__)
#define MINLZ_LIKELY(x) __builtin_expect(!!(x), 1)
#define MINLZ_UNLIKELY(x) __builtin_expect(!!(x), 0)
#else
#define MINLZ_LIKELY(x) (x)
#define MINLZ_UNLIKELY(x) (x)
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MINLZ_ALWAYS_INLINE inline __attribute__((always_inline))
#define MINLZ_NOINLINE __attribute__((noinline, unused))
#else
#define MINLZ_ALWAYS_INLINE inline
#define MINLZ_NOINLINE
#endif

#ifndef MINLZ_ENABLE_INLINE_ASM
#define MINLZ_ENABLE_INLINE_ASM 1
#endif

#if MINLZ_ENABLE_INLINE_ASM && (defined(__GNUC__) || defined(__clang__)) && defined(__x86_64__)
#define MINLZ_HAVE_INLINE_ASM_AMD64 1
#else
#define MINLZ_HAVE_INLINE_ASM_AMD64 0
#endif

int minlz_backend_has_inline_asm_amd64(void);

static inline uint8_t minlz_load8(const uint8_t *p) {
    return p[0];
}

static inline uint16_t minlz_load16(const uint8_t *p) {
    uint16_t v;
    memcpy(&v, p, sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = (uint16_t)((v >> 8) | (v << 8));
#endif
    return v;
}

static inline uint32_t minlz_load24(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16);
}

static inline uint32_t minlz_load32(const uint8_t *p) {
    uint32_t v;
    memcpy(&v, p, sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
        ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
#endif
    return v;
}

static inline uint64_t minlz_load64(const uint8_t *p) {
    uint64_t v;
    memcpy(&v, p, sizeof(v));
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = ((v & 0x00000000000000ffull) << 56) |
        ((v & 0x000000000000ff00ull) << 40) |
        ((v & 0x0000000000ff0000ull) << 24) |
        ((v & 0x00000000ff000000ull) << 8) |
        ((v & 0x000000ff00000000ull) >> 8) |
        ((v & 0x0000ff0000000000ull) >> 24) |
        ((v & 0x00ff000000000000ull) >> 40) |
        ((v & 0xff00000000000000ull) >> 56);
#endif
    return v;
}

static inline void minlz_store16(uint8_t *p, uint16_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = (uint16_t)((v >> 8) | (v << 8));
#endif
    memcpy(p, &v, sizeof(v));
}

static inline void minlz_store32(uint8_t *p, uint32_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = ((v & 0x000000ffu) << 24) | ((v & 0x0000ff00u) << 8) |
        ((v & 0x00ff0000u) >> 8) | ((v & 0xff000000u) >> 24);
#endif
    memcpy(p, &v, sizeof(v));
}

static inline void minlz_store64(uint8_t *p, uint64_t v) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
    v = ((v & 0x00000000000000ffull) << 56) |
        ((v & 0x000000000000ff00ull) << 40) |
        ((v & 0x0000000000ff0000ull) << 24) |
        ((v & 0x00000000ff000000ull) << 8) |
        ((v & 0x000000ff00000000ull) >> 8) |
        ((v & 0x0000ff0000000000ull) >> 24) |
        ((v & 0x00ff000000000000ull) >> 40) |
        ((v & 0xff00000000000000ull) >> 56);
#endif
    memcpy(p, &v, sizeof(v));
}

static inline void minlz_copy_17to64_asm(uint8_t *dst, const uint8_t *src, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (len <= 32) {
        __asm__ volatile(
            "movdqu (%[src]), %%xmm0\n\t"
            "movdqu -16(%[src],%[len]), %%xmm1\n\t"
            "movdqu %%xmm0, (%[dst])\n\t"
            "movdqu %%xmm1, -16(%[dst],%[len])"
            :
            : [dst] "r"(dst), [src] "r"(src), [len] "r"(len)
            : "xmm0", "xmm1", "memory");
    } else {
        __asm__ volatile(
            "movdqu (%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu -32(%[src],%[len]), %%xmm2\n\t"
            "movdqu -16(%[src],%[len]), %%xmm3\n\t"
            "movdqu %%xmm0, (%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, -32(%[dst],%[len])\n\t"
            "movdqu %%xmm3, -16(%[dst],%[len])"
            :
            : [dst] "r"(dst), [src] "r"(src), [len] "r"(len)
            : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
    }
#else
    minlz_store64(dst, minlz_load64(src));
    minlz_store64(dst + 8, minlz_load64(src + 8));
    if (len > 32) {
        minlz_store64(dst + 16, minlz_load64(src + 16));
        minlz_store64(dst + 24, minlz_load64(src + 24));
        if (len > 48) {
            minlz_store64(dst + 32, minlz_load64(src + 32));
            minlz_store64(dst + 40, minlz_load64(src + 40));
        }
    }
    if (len > 16) {
        minlz_store64(dst + len - 16, minlz_load64(src + len - 16));
        minlz_store64(dst + len - 8, minlz_load64(src + len - 8));
    }
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_copy_sse64_loop(uint8_t *dst, const uint8_t *src,
                                                      size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t chunks = len >> 6;
    if (chunks != 0) {
        __asm__ volatile(
            "1:\n\t"
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "addq $64, %[src]\n\t"
            "addq $64, %[dst]\n\t"
            "decq %[chunks]\n\t"
            "jnz 1b"
            : [dst] "+r"(dst), [src] "+r"(src), [chunks] "+r"(chunks)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "cc", "memory");
    }
    len &= 63u;
    if (len != 0) {
        minlz_copy_17to64_asm(dst, src, len);
    }
#else
    memcpy(dst, src, len);
#endif
}

typedef struct minlz_workspace_table {
    uint32_t *pos;
    uint32_t *stamp;
    size_t cap;
    uint32_t generation;
} minlz_workspace_table;

struct minlz_workspace {
    minlz_workspace_table tables[2];
    uint8_t *buffers[3];
    size_t buffer_caps[3];
    uint8_t encode_scratch_uses;
    uint8_t encode_scratch_hint; /* 0: untried, 1: rejected, 2: accepted hint. */
};

void minlz_workspace_cleanup(minlz_workspace *ws);
minlz_status minlz_workspace_prepare_table(minlz_workspace *ws, unsigned slot,
                                           size_t entries, uint32_t **pos,
                                           uint32_t **stamp, uint32_t *generation);
minlz_status minlz_workspace_prepare_buffer(minlz_workspace *ws, unsigned slot,
                                            size_t cap, uint8_t **buf);
minlz_status minlz_workspace_prepare_encode_scratch(minlz_workspace *ws, unsigned slot,
                                                    size_t cap, uint8_t **buf);
void minlz_workspace_release_buffer(uint8_t *buf);

/* Both sides must own buffers allocated by the workspace allocator. */
static MINLZ_ALWAYS_INLINE minlz_status minlz_workspace_exchange_buffer(
    minlz_workspace *ws, unsigned slot, uint8_t **buf, size_t *cap) {
    uint8_t *previous;
    size_t previous_cap;
    if (!ws || slot >= sizeof(ws->buffers) / sizeof(ws->buffers[0]) || !buf || !cap) {
        return MINLZ_CORRUPT;
    }
    previous = ws->buffers[slot];
    previous_cap = ws->buffer_caps[slot];
    ws->buffers[slot] = *buf;
    ws->buffer_caps[slot] = *cap;
    *buf = previous;
    *cap = previous_cap;
    if (slot == 2u) {
        ws->encode_scratch_uses = 0u;
        ws->encode_scratch_hint = 0u;
    }
    return MINLZ_OK;
}

size_t minlz_put_uvarint(uint8_t *dst, uint64_t v);
minlz_status minlz_read_uvarint(const uint8_t *src, size_t src_len, size_t *value, size_t *used);
size_t minlz_put_varint(uint8_t *dst, int64_t v);

uint32_t minlz_crc32c(const uint8_t *src, size_t src_len);
uint32_t minlz_mask_crc32c(uint32_t crc);
uint32_t minlz_crc(const uint8_t *src, size_t src_len);

size_t minlz_emit_literal(uint8_t *dst, size_t dst_cap, const uint8_t *lit, size_t lit_len);
size_t minlz_emit_repeat(uint8_t *dst, size_t dst_cap, size_t length);
size_t minlz_emit_copy(uint8_t *dst, size_t dst_cap, size_t offset, size_t length);
size_t minlz_emit_copy_lits2(uint8_t *dst, size_t dst_cap, const uint8_t *lits,
                             size_t lit_len, size_t offset, size_t length);
size_t minlz_emit_copy_lits3(uint8_t *dst, size_t dst_cap, const uint8_t *lits,
                             size_t lit_len, size_t offset, size_t length);

size_t minlz_encode_payload(uint8_t *dst, size_t dst_cap, const uint8_t *src,
                            size_t src_len, int level);
size_t minlz_encode_payload_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                               const uint8_t *src, size_t src_len, int level);
minlz_status minlz_encode_stream_payload(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                                         const uint8_t *src, size_t src_len, int level);
minlz_status minlz_encode_stream_payload_ws(minlz_workspace *ws, uint8_t *dst,
                                            size_t dst_cap, size_t *dst_len,
                                            const uint8_t *src, size_t src_len, int level);
minlz_status minlz_decode_payload(uint8_t *dst, size_t dst_len,
                                  const uint8_t *src, size_t src_len);
minlz_status minlz_payload_decoded_len(const uint8_t *src, size_t src_len,
                                       size_t *decoded_len, size_t *header_len);

typedef struct minlz_index_entry {
    int64_t compressed;
    int64_t uncompressed;
} minlz_index_entry;

typedef struct minlz_index_builder {
    minlz_index_entry *entries;
    size_t len;
    size_t cap;
    int64_t est_block_uncomp;
} minlz_index_builder;

struct minlz_index {
    minlz_index_entry *entries;
    size_t len;
    size_t cap;
    int64_t total_uncompressed;
    int64_t total_compressed;
    int64_t est_block_uncomp;
};

void minlz_index_init(minlz_index_builder *idx, size_t block_size);
void minlz_index_free(minlz_index_builder *idx);
minlz_status minlz_index_add(minlz_index_builder *idx, int64_t compressed, int64_t uncompressed);
minlz_status minlz_index_append(uint8_t **buf, size_t *len, size_t *cap,
                                const minlz_index_builder *idx,
                                int64_t uncomp_total, int64_t comp_total);

minlz_status minlz_write_all(minlz_write_cb cb, void *ctx, const uint8_t *src, size_t len);
minlz_status minlz_read_exact(minlz_read_cb cb, void *ctx, uint8_t *dst,
                              size_t len, int allow_eof, size_t *got);
minlz_status minlz_skip_exact(minlz_read_cb cb, void *ctx, size_t len);

typedef struct minlz_search_table_view {
    minlz_search_config config;
    uint8_t reductions;
    const uint8_t *table;
    size_t table_len;
    size_t compressed_blocks_total;
    size_t compressed_blocks_raw;
    size_t compressed_blocks_rle;
    size_t compressed_blocks_sparse;
    size_t compressed_tables;
    size_t compressed_bytes_tabled;
    size_t compressed_bytes_raw;
    size_t compressed_bytes_rle;
    size_t compressed_bytes_sparse;
    size_t compressed_bytes_table_headers;
} minlz_search_table_view;

typedef struct minlz_huff0_decoder minlz_huff0_decoder;

typedef struct minlz_huff0_candidate {
    uint8_t *table;
    size_t table_len;
    uint8_t *data;
    size_t data_len;
    unsigned histogram[256];
} minlz_huff0_candidate;

typedef struct minlz_huff0_shared {
    uint8_t *table;
    size_t table_len;
    uint8_t *data[16];
    size_t data_len[16];
    uint8_t valid[16];
} minlz_huff0_shared;

minlz_status minlz_huff0_decoder_create(minlz_huff0_decoder **decoder);
void minlz_huff0_decoder_destroy(minlz_huff0_decoder *decoder);
minlz_status minlz_huff0_decoder_read_table(
    minlz_huff0_decoder *decoder, unsigned table_index,
    const uint8_t *src, size_t src_len, size_t *used);
minlz_status minlz_huff0_decoder_decode(
    minlz_huff0_decoder *decoder, unsigned table_index,
    uint8_t *dst, size_t dst_len,
    const uint8_t *src, size_t src_len);
minlz_status minlz_huff0_encode_own(
    const uint8_t *src, size_t src_len,
    minlz_huff0_candidate *candidate);
minlz_status minlz_huff0_encode_shared(
    const uint8_t *src, size_t block_size, size_t block_count,
    const minlz_huff0_candidate *own,
    minlz_huff0_shared *shared);
void minlz_huff0_candidate_destroy(minlz_huff0_candidate *candidate);
void minlz_huff0_shared_destroy(minlz_huff0_shared *shared);

minlz_status minlz_search_config_resolve(minlz_search_config *dst,
                                         const minlz_search_config *src,
                                         size_t block_size);
size_t minlz_search_overlap_bytes(const minlz_search_config *config);
minlz_status minlz_search_make_info_chunk(const minlz_search_config *config,
                                          uint8_t **dst, size_t *dst_len);
minlz_status minlz_search_make_table_chunk(const minlz_search_config *config,
                                           const uint8_t *block, size_t block_len,
                                           const uint8_t *overlap, size_t overlap_len,
                                           uint8_t **dst, size_t *dst_len);
minlz_status minlz_search_parse_info(const uint8_t *payload, size_t payload_len,
                                     minlz_search_config *config, size_t *used);
minlz_status minlz_search_parse_table(const uint8_t *payload, size_t payload_len,
                                      int ignore_crc,
                                      minlz_search_table_view *view);
minlz_status minlz_search_parse_table_compressed(
    const uint8_t *payload, size_t payload_len, int ignore_crc,
    uint8_t **table_storage, minlz_search_table_view *view);
int minlz_search_pattern_might_match(const minlz_search_table_view *view,
                                     const uint8_t *pattern, size_t pattern_len,
                                     int *can_use);

minlz_status minlz_decode_stream_chunk(minlz_workspace *ws,
                                       const uint8_t *chunk, size_t chunk_len,
                                       uint8_t **decoded, size_t *decoded_len,
                                       int compressed_crc, int ignore_crc,
                                       size_t max_block);

typedef enum minlz_backend {
    MINLZ_BACKEND_PORTABLE = 0,
    MINLZ_BACKEND_SSE42,
    MINLZ_BACKEND_AVX2,
    MINLZ_BACKEND_AVX512,
    MINLZ_BACKEND_ASM_AMD64
} minlz_backend;

typedef struct minlz_cpu_features {
    int x86;
    int sse42;
    int pclmul;
    int avx;
    int avx2;
    int bmi1;
    int bmi2;
    int erms;
    int avx512f;
    int avx512bw;
    int avx512vl;
} minlz_cpu_features;

const minlz_cpu_features *minlz_cpu_features_get(void);
minlz_backend minlz_select_backend(void);
const char *minlz_backend_name(minlz_backend backend);
int minlz_backend_has_sse42(void);
int minlz_backend_has_avx2(void);
int minlz_backend_has_avx512(void);
int minlz_backend_has_asm_amd64(void);

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_sse42_xmm(uint8_t *dst,
                                                                 const uint8_t *src,
                                                                 size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t blocks;
    size_t tail;
    if (!minlz_backend_has_sse42()) {
        return 0;
    }
    if (len < 14u) {
        if (len >= 8u) {
            minlz_store64(dst, minlz_load64(src));
            minlz_store64(dst + len - 8u, minlz_load64(src + len - 8u));
        } else if (len >= 4u) {
            minlz_store32(dst, minlz_load32(src));
            minlz_store32(dst + len - 4u, minlz_load32(src + len - 4u));
        } else if (len != 0u) {
            dst[0] = src[0];
            if (len > 1u) {
                dst[1] = src[1];
                if (len > 2u) {
                    dst[2] = src[2];
                }
            }
        }
        return 1;
    }

    minlz_store64(dst, minlz_load64(src));
    minlz_store64(dst + 6u, minlz_load64(src + 6u));
    dst += 14u;
    src += 14u;
    len -= 14u;

    blocks = len / 96u;
    tail = len - blocks * 96u;
    if (blocks != 0u) {
        __asm__ volatile(
            "1:\n\t"
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu 64(%[src]), %%xmm4\n\t"
            "movdqu 80(%[src]), %%xmm5\n\t"
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "movdqu %%xmm4, 64(%[dst])\n\t"
            "movdqu %%xmm5, 80(%[dst])\n\t"
            "addq $96, %[src]\n\t"
            "addq $96, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc", "memory");
    }
    if (tail > 64u) {
        __asm__ volatile(
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu -32(%[src],%[tail]), %%xmm4\n\t"
            "movdqu -16(%[src],%[tail]), %%xmm5\n\t"
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "movdqu %%xmm4, -32(%[dst],%[tail])\n\t"
            "movdqu %%xmm5, -16(%[dst],%[tail])"
            :
            : [dst] "r"(dst), [src] "r"(src), [tail] "r"(tail)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory");
    } else if (tail > 16u) {
        minlz_copy_17to64_asm(dst, src, tail);
    } else if (tail >= 8u) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + tail - 8u, minlz_load64(src + tail - 8u));
    } else if (tail >= 4u) {
        minlz_store32(dst, minlz_load32(src));
        minlz_store32(dst + tail - 4u, minlz_load32(src + tail - 4u));
    } else if (tail != 0u) {
        dst[0] = src[0];
        if (tail > 1u) {
            dst[1] = src[1];
            if (tail > 2u) {
                dst[2] = src[2];
            }
        }
    }
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_sse42_movsq(uint8_t *dst,
                                                                   const uint8_t *src,
                                                                   size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t qwords;
    size_t tail;
    if (!minlz_backend_has_sse42() || len < 14u) {
        return 0;
    }
    minlz_store64(dst, minlz_load64(src));
    minlz_store64(dst + 6u, minlz_load64(src + 6u));
    dst += 14u;
    src += 14u;
    len -= 14u;
    qwords = len >> 3;
    tail = len & 7u;
    if (qwords != 0u) {
        __asm__ volatile("rep movsq"
                         : "+D"(dst), "+S"(src), "+c"(qwords)
                         :
                         : "memory");
    }
    if (tail >= 4u) {
        minlz_store32(dst, minlz_load32(src));
        minlz_store32(dst + tail - 4u, minlz_load32(src + tail - 4u));
    } else if (tail != 0u) {
        dst[0] = src[0];
        if (tail > 1u) {
            dst[1] = src[1];
            if (tail > 2u) {
                dst[2] = src[2];
            }
        }
    }
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_erms_movsb(uint8_t *dst,
                                                                  const uint8_t *src,
                                                                  size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (!minlz_backend_has_sse42() || !minlz_cpu_features_get()->erms) {
        return 0;
    }
    if (d < s + len && s < d + len) {
        return 0;
    }
    if (len != 0u) {
        __asm__ volatile("rep movsb"
                         : "+D"(dst), "+S"(src), "+c"(len)
                         :
                         : "memory");
    }
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_sse42_qword(uint8_t *dst,
                                                                   const uint8_t *src,
                                                                   size_t len) {
    if (!minlz_backend_has_sse42()) {
        return 0;
    }
    while (len >= 64u) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + 8u, minlz_load64(src + 8u));
        minlz_store64(dst + 16u, minlz_load64(src + 16u));
        minlz_store64(dst + 24u, minlz_load64(src + 24u));
        minlz_store64(dst + 32u, minlz_load64(src + 32u));
        minlz_store64(dst + 40u, minlz_load64(src + 40u));
        minlz_store64(dst + 48u, minlz_load64(src + 48u));
        minlz_store64(dst + 56u, minlz_load64(src + 56u));
        dst += 64u;
        src += 64u;
        len -= 64u;
    }
    if (len > 16u) {
        minlz_copy_17to64_asm(dst, src, len);
    } else if (len >= 8u) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + len - 8u, minlz_load64(src + len - 8u));
    } else if (len >= 4u) {
        minlz_store32(dst, minlz_load32(src));
        minlz_store32(dst + len - 4u, minlz_load32(src + len - 4u));
    } else if (len != 0u) {
        dst[0] = src[0];
        if (len > 1u) {
            dst[1] = src[1];
            if (len > 2u) {
                dst[2] = src[2];
            }
        }
    }
    return 1;
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_sse42_xmm_aligned_store(
    uint8_t *dst, const uint8_t *src, size_t len, int align14) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t blocks;
    size_t tail;
    if (!minlz_backend_has_sse42()) {
        return 0;
    }
    if (align14) {
        if ((((uintptr_t)dst + 14u) & 15u) != 0u || len < 14u) {
            return 0;
        }
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + 6u, minlz_load64(src + 6u));
        dst += 14u;
        src += 14u;
        len -= 14u;
    } else if (((uintptr_t)dst & 15u) != 0u) {
        return 0;
    }
    blocks = len / 96u;
    tail = len - blocks * 96u;
    if (blocks != 0u) {
        __asm__ volatile(
            "1:\n\t"
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu 64(%[src]), %%xmm4\n\t"
            "movdqu 80(%[src]), %%xmm5\n\t"
            "movdqa %%xmm0, 0(%[dst])\n\t"
            "movdqa %%xmm1, 16(%[dst])\n\t"
            "movdqa %%xmm2, 32(%[dst])\n\t"
            "movdqa %%xmm3, 48(%[dst])\n\t"
            "movdqa %%xmm4, 64(%[dst])\n\t"
            "movdqa %%xmm5, 80(%[dst])\n\t"
            "addq $96, %[src]\n\t"
            "addq $96, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "cc", "memory");
    }
    if (tail > 64u) {
        minlz_copy_17to64_asm(dst, src, 64u);
        minlz_copy_17to64_asm(dst + tail - 32u, src + tail - 32u, 32u);
    } else if (tail > 16u) {
        minlz_copy_17to64_asm(dst, src, tail);
    } else if (tail >= 8u) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + tail - 8u, minlz_load64(src + tail - 8u));
    } else if (tail >= 4u) {
        minlz_store32(dst, minlz_load32(src));
        minlz_store32(dst + tail - 4u, minlz_load32(src + tail - 4u));
    } else if (tail != 0u) {
        dst[0] = src[0];
        if (tail > 1u) {
            dst[1] = src[1];
            if (tail > 2u) {
                dst[2] = src[2];
            }
        }
    }
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    (void)align14;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_avx2_aligned_store(uint8_t *dst,
                                                                          const uint8_t *src,
                                                                          size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t align;
    size_t blocks;
    size_t tail;
    if (!minlz_backend_has_avx2() || len < 256u) {
        return 0;
    }
    align = (size_t)(-(uintptr_t)dst) & 31u;
    if (align != 0u) {
        if (align >= len) {
            memcpy(dst, src, len);
            return 1;
        }
        memcpy(dst, src, align);
        dst += align;
        src += align;
        len -= align;
    }
    blocks = len >> 7;
    tail = len & 127u;
    if (blocks != 0u) {
        __asm__ volatile(
            "1:\n\t"
            "vmovdqu 0(%[src]), %%ymm0\n\t"
            "vmovdqu 32(%[src]), %%ymm1\n\t"
            "vmovdqu 64(%[src]), %%ymm2\n\t"
            "vmovdqu 96(%[src]), %%ymm3\n\t"
            "vmovdqa %%ymm0, 0(%[dst])\n\t"
            "vmovdqa %%ymm1, 32(%[dst])\n\t"
            "vmovdqa %%ymm2, 64(%[dst])\n\t"
            "vmovdqa %%ymm3, 96(%[dst])\n\t"
            "addq $128, %[src]\n\t"
            "addq $128, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b\n\t"
            "vzeroupper"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "ymm0", "ymm1", "ymm2", "ymm3", "cc", "memory");
    }
    if (tail != 0u) {
        memcpy(dst, src, tail);
    }
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_avx2_go_fwd_unchecked(
    uint8_t *dst, const uint8_t *src, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (d < s + len && s < d + len) {
        return 0;
    }
    __asm__ volatile(
        "movq %[src], %%r8\n\t"
        "movq %[len], %%r9\n\t"
        "leaq (%%r8,%%r9,1), %%rcx\n\t"
        "movq %[dst], %%r10\n\t"
        "movdqu -128(%%rcx), %%xmm5\n\t"
        "movdqu -112(%%rcx), %%xmm6\n\t"
        "movq %%r10, %%rdi\n\t"
        "andq $-32, %%rdi\n\t"
        "addq $32, %%rdi\n\t"
        "movdqu -96(%%rcx), %%xmm7\n\t"
        "movdqu -80(%%rcx), %%xmm8\n\t"
        "movq %%rdi, %%r11\n\t"
        "subq %%r10, %%r11\n\t"
        "movdqu -64(%%rcx), %%xmm9\n\t"
        "movdqu -48(%%rcx), %%xmm10\n\t"
        "subq %%r11, %%r9\n\t"
        "movdqu -32(%%rcx), %%xmm11\n\t"
        "movdqu -16(%%rcx), %%xmm12\n\t"
        "vmovdqu (%%r8), %%ymm4\n\t"
        "addq %%r11, %%r8\n\t"
        "subq $128, %%r9\n\t"
        "1:\n\t"
        "vmovdqu (%%r8), %%ymm0\n\t"
        "vmovdqu 32(%%r8), %%ymm1\n\t"
        "vmovdqu 64(%%r8), %%ymm2\n\t"
        "vmovdqu 96(%%r8), %%ymm3\n\t"
        "addq $128, %%r8\n\t"
        "vmovdqa %%ymm0, (%%rdi)\n\t"
        "vmovdqa %%ymm1, 32(%%rdi)\n\t"
        "vmovdqa %%ymm2, 64(%%rdi)\n\t"
        "vmovdqa %%ymm3, 96(%%rdi)\n\t"
        "addq $128, %%rdi\n\t"
        "subq $128, %%r9\n\t"
        "ja 1b\n\t"
        "addq $128, %%r9\n\t"
        "addq %%rdi, %%r9\n\t"
        "vmovdqu %%ymm4, (%%r10)\n\t"
        "vzeroupper\n\t"
        "movdqu %%xmm5, -128(%%r9)\n\t"
        "movdqu %%xmm6, -112(%%r9)\n\t"
        "movdqu %%xmm7, -96(%%r9)\n\t"
        "movdqu %%xmm8, -80(%%r9)\n\t"
        "movdqu %%xmm9, -64(%%r9)\n\t"
        "movdqu %%xmm10, -48(%%r9)\n\t"
        "movdqu %%xmm11, -32(%%r9)\n\t"
        "movdqu %%xmm12, -16(%%r9)"
        :
        : [dst] "r"(dst), [src] "r"(src), [len] "r"(len)
        : "r8", "r9", "r10", "r11", "rcx", "rdi",
          "xmm5", "xmm6", "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12",
          "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "cc", "memory");
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_avx2_go_fwd(uint8_t *dst,
                                                                   const uint8_t *src,
                                                                   size_t len) {
    if (!minlz_backend_has_avx2() || len < 256u || len >= (1u << 20)) {
        return 0;
    }
    return minlz_copy_uncompressed_avx2_go_fwd_unchecked(dst, src, len);
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_avx2_go_fwd256(uint8_t *dst,
                                                                      const uint8_t *src,
                                                                      size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (!minlz_backend_has_avx2() || len < 512u || len >= (1u << 20)) {
        return 0;
    }
    if (d < s + len && s < d + len) {
        return 0;
    }
    __asm__ volatile(
        "movq %[src], %%r8\n\t"
        "movq %[len], %%r9\n\t"
        "leaq (%%r8,%%r9,1), %%rcx\n\t"
        "movq %[dst], %%r10\n\t"
        "vmovdqu -256(%%rcx), %%ymm8\n\t"
        "vmovdqu -224(%%rcx), %%ymm9\n\t"
        "vmovdqu -192(%%rcx), %%ymm10\n\t"
        "vmovdqu -160(%%rcx), %%ymm11\n\t"
        "vmovdqu -128(%%rcx), %%ymm12\n\t"
        "vmovdqu -96(%%rcx), %%ymm13\n\t"
        "vmovdqu -64(%%rcx), %%ymm14\n\t"
        "vmovdqu -32(%%rcx), %%ymm15\n\t"
        "movq %%r10, %%rdi\n\t"
        "andq $-32, %%rdi\n\t"
        "addq $32, %%rdi\n\t"
        "movq %%rdi, %%r11\n\t"
        "subq %%r10, %%r11\n\t"
        "vmovdqu (%%r8), %%ymm0\n\t"
        "vmovdqu %%ymm0, (%%r10)\n\t"
        "addq %%r11, %%r8\n\t"
        "subq %%r11, %%r9\n\t"
        "subq $256, %%r9\n\t"
        ".p2align 4\n\t"
        "1:\n\t"
        "vmovdqu 0(%%r8), %%ymm0\n\t"
        "vmovdqu 32(%%r8), %%ymm1\n\t"
        "vmovdqu 64(%%r8), %%ymm2\n\t"
        "vmovdqu 96(%%r8), %%ymm3\n\t"
        "vmovdqu 128(%%r8), %%ymm4\n\t"
        "vmovdqu 160(%%r8), %%ymm5\n\t"
        "vmovdqu 192(%%r8), %%ymm6\n\t"
        "vmovdqu 224(%%r8), %%ymm7\n\t"
        "vmovdqa %%ymm0, 0(%%rdi)\n\t"
        "vmovdqa %%ymm1, 32(%%rdi)\n\t"
        "vmovdqa %%ymm2, 64(%%rdi)\n\t"
        "vmovdqa %%ymm3, 96(%%rdi)\n\t"
        "vmovdqa %%ymm4, 128(%%rdi)\n\t"
        "vmovdqa %%ymm5, 160(%%rdi)\n\t"
        "vmovdqa %%ymm6, 192(%%rdi)\n\t"
        "vmovdqa %%ymm7, 224(%%rdi)\n\t"
        "addq $256, %%r8\n\t"
        "addq $256, %%rdi\n\t"
        "subq $256, %%r9\n\t"
        "ja 1b\n\t"
        "addq $256, %%r9\n\t"
        "addq %%rdi, %%r9\n\t"
        "vmovdqu %%ymm8, -256(%%r9)\n\t"
        "vmovdqu %%ymm9, -224(%%r9)\n\t"
        "vmovdqu %%ymm10, -192(%%r9)\n\t"
        "vmovdqu %%ymm11, -160(%%r9)\n\t"
        "vmovdqu %%ymm12, -128(%%r9)\n\t"
        "vmovdqu %%ymm13, -96(%%r9)\n\t"
        "vmovdqu %%ymm14, -64(%%r9)\n\t"
        "vmovdqu %%ymm15, -32(%%r9)\n\t"
        "vzeroupper"
        :
        : [dst] "r"(dst), [src] "r"(src), [len] "r"(len)
        : "r8", "r9", "r10", "r11", "rcx", "rdi",
          "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
          "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
          "cc", "memory");
    return 1;
#else
    (void)dst;
    (void)src;
    (void)len;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_10000_avx2_mod2(uint8_t *dst,
                                                                       const uint8_t *src) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    if (!minlz_backend_has_avx2() || (d & 31u) != 2u) {
        return 0;
    }
    if (d < s + 10000u && s < d + 10000u) {
        return 0;
    }
    __asm__ volatile(
        "movdqu 9872(%[src]), %%xmm8\n\t"
        "movdqu 9888(%[src]), %%xmm9\n\t"
        "movdqu 9904(%[src]), %%xmm10\n\t"
        "movdqu 9920(%[src]), %%xmm11\n\t"
        "movdqu 9936(%[src]), %%xmm12\n\t"
        "movdqu 9952(%[src]), %%xmm13\n\t"
        "movdqu 9968(%[src]), %%xmm14\n\t"
        "movdqu 9984(%[src]), %%xmm15\n\t"
        "vmovdqu (%[src]), %%ymm0\n\t"
        "vmovdqu %%ymm0, (%[dst])\n\t"
        "leaq 30(%[src]), %%r8\n\t"
        "leaq 30(%[dst]), %%rdi\n\t"
        "movl $77, %%ecx\n\t"
        ".p2align 4\n\t"
        "1:\n\t"
        "vmovdqu 0(%%r8), %%ymm0\n\t"
        "vmovdqu 32(%%r8), %%ymm1\n\t"
        "vmovdqu 64(%%r8), %%ymm2\n\t"
        "vmovdqu 96(%%r8), %%ymm3\n\t"
        "vmovdqa %%ymm0, 0(%%rdi)\n\t"
        "vmovdqa %%ymm1, 32(%%rdi)\n\t"
        "vmovdqa %%ymm2, 64(%%rdi)\n\t"
        "vmovdqa %%ymm3, 96(%%rdi)\n\t"
        "addq $128, %%r8\n\t"
        "addq $128, %%rdi\n\t"
        "decl %%ecx\n\t"
        "jnz 1b\n\t"
        "movdqu %%xmm8, 9872(%[dst])\n\t"
        "movdqu %%xmm9, 9888(%[dst])\n\t"
        "movdqu %%xmm10, 9904(%[dst])\n\t"
        "movdqu %%xmm11, 9920(%[dst])\n\t"
        "movdqu %%xmm12, 9936(%[dst])\n\t"
        "movdqu %%xmm13, 9952(%[dst])\n\t"
        "movdqu %%xmm14, 9968(%[dst])\n\t"
        "movdqu %%xmm15, 9984(%[dst])\n\t"
        "vzeroupper"
        :
        : [dst] "r"(dst), [src] "r"(src)
        : "r8", "rcx", "rdi",
          "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15",
          "ymm0", "ymm1", "ymm2", "ymm3", "cc", "memory");
    return 1;
#else
    (void)dst;
    (void)src;
    return 0;
#endif
}

static MINLZ_NOINLINE int minlz_copy_uncompressed_10000_avx2(uint8_t *dst,
                                                             const uint8_t *src) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    size_t blocks = 39u;
    if (!minlz_backend_has_avx2() || (d & 31u) != 0u) {
        return 0;
    }
    if (d < s + 10000u && s < d + 10000u) {
        return 0;
    }
    __asm__ volatile(
        "1:\n\t"
        "vmovdqu 0(%[src]), %%ymm0\n\t"
        "vmovdqu 32(%[src]), %%ymm1\n\t"
        "vmovdqu 64(%[src]), %%ymm2\n\t"
        "vmovdqu 96(%[src]), %%ymm3\n\t"
        "vmovdqu 128(%[src]), %%ymm4\n\t"
        "vmovdqu 160(%[src]), %%ymm5\n\t"
        "vmovdqu 192(%[src]), %%ymm6\n\t"
        "vmovdqu 224(%[src]), %%ymm7\n\t"
        "vmovdqa %%ymm0, 0(%[dst])\n\t"
        "vmovdqa %%ymm1, 32(%[dst])\n\t"
        "vmovdqa %%ymm2, 64(%[dst])\n\t"
        "vmovdqa %%ymm3, 96(%[dst])\n\t"
        "vmovdqa %%ymm4, 128(%[dst])\n\t"
        "vmovdqa %%ymm5, 160(%[dst])\n\t"
        "vmovdqa %%ymm6, 192(%[dst])\n\t"
        "vmovdqa %%ymm7, 224(%[dst])\n\t"
        "addq $256, %[src]\n\t"
        "addq $256, %[dst]\n\t"
        "decq %[blocks]\n\t"
        "jnz 1b\n\t"
        "vmovdqu 0(%[src]), %%xmm0\n\t"
        "vmovdqa %%xmm0, 0(%[dst])\n\t"
        "vzeroupper"
        : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
        :
        : "xmm0", "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
          "cc", "memory");
    return 1;
#else
    (void)dst;
    (void)src;
    return 0;
#endif
}

void minlz_copy_match_avx2(uint8_t *out, size_t offset, size_t length);
void minlz_copy_match_avx512(uint8_t *out, size_t offset, size_t length);
#if defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
void minlz_copy_forward_asm_amd64(uint8_t *dst, const uint8_t *src, size_t len);
size_t minlz_match_len_asm_amd64(const uint8_t *a, const uint8_t *b, size_t max_len);
#endif

#endif
