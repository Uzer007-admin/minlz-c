/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <stdlib.h>

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64
#include <immintrin.h>
#endif

#ifndef MINLZ_ENABLE_BALANCED_512K_BODY
#define MINLZ_ENABLE_BALANCED_512K_BODY 0
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

#ifndef MINLZ_ENABLE_L1_FAST64K_INLINE_ASM
#define MINLZ_ENABLE_L1_FAST64K_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM
#define MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM MINLZ_ENABLE_L1_FAST64K_INLINE_ASM
#endif

#ifndef MINLZ_ENABLE_L1_FAST_SMALL_GOASM_CALL
#define MINLZ_ENABLE_L1_FAST_SMALL_GOASM_CALL 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_GOASM
#define MINLZ_ENABLE_BETTER2MB_GOASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED
#define MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_INLINE_ASM
#define MINLZ_ENABLE_BETTER2MB_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM
#define MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM
#define MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER
#define MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_REGREWRITE_ASM
#define MINLZ_ENABLE_BETTER2MB_REGREWRITE_ASM 0
#endif

#ifndef MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM
#define MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM 1
#endif

#define MINLZ_BETTER2MB_REGREWRITE_ANY \
    (MINLZ_ENABLE_BETTER2MB_REGREWRITE_ASM || MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM)

#ifndef MINLZ_BETTER2MB_ZERO_REP_STOSQ
#define MINLZ_BETTER2MB_ZERO_REP_STOSQ 0
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

#ifndef MINLZ_BETTER2MB_TABLE_RBP
#define MINLZ_BETTER2MB_TABLE_RBP 1
#endif

#ifndef MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH
#define MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH 0
#endif

#define MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE \
    (MINLZ_ENABLE_EXPERIMENTAL_L2_SEARCH && MINLZ_BETTER2MB_TABLE_RBP)

/* Fold the long-hash pre-shift into its multiplier and use the low 32-bit
 * short-hash product. Both retain bit-identical table indices. */
#if MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE
#define MINLZ_BETTER2MB_LONG_HASH_MULT "0xcf1bbcdcbfa56300"
#define MINLZ_BETTER2MB_LONG_HASH_PREP(reg) ""
#define MINLZ_BETTER2MB_SHORT_HASH(reg64, reg32) \
    "imull $-1640531535, " reg32 ", " reg32 "\n" \
    "shrl $0x12, " reg32 "\n"
#else
#define MINLZ_BETTER2MB_LONG_HASH_MULT "0x00cf1bbcdcbfa563"
#define MINLZ_BETTER2MB_LONG_HASH_PREP(reg) "shlq $0x08, " reg "\n"
#define MINLZ_BETTER2MB_SHORT_HASH(reg64, reg32) \
    "shlq $0x20, " reg64 "\n" \
    "imulq $-1640531535, " reg64 ", " reg64 "\n" \
    "shrq $0x32, " reg64 "\n"
#endif

#ifndef MINLZ_BETTER2MB_REG_NEXT_SEARCH
#define MINLZ_BETTER2MB_REG_NEXT_SEARCH 1
#endif

#ifndef MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
#define MINLZ_BETTER2MB_REG_NEXT_EMIT_R14 1
#endif

#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
#define MINLZ_BETTER2MB_NEXT_EMIT_READ(reg) "movl %%r14d, " reg "\n"
#define MINLZ_BETTER2MB_NEXT_EMIT_WRITE(reg) "movl " reg ", %%r14d\n"
#define MINLZ_BETTER2MB_NEXT_EMIT_SUB(reg) "subl %%r14d, " reg "\n"
#else
#define MINLZ_BETTER2MB_NEXT_EMIT_READ(reg) "movl 12(%%rsp), " reg "\n"
#define MINLZ_BETTER2MB_NEXT_EMIT_WRITE(reg) "movl " reg ", 12(%%rsp)\n"
#define MINLZ_BETTER2MB_NEXT_EMIT_SUB(reg) "subl 12(%%rsp), " reg "\n"
#endif

#if MINLZ_BETTER2MB_TABLE_RBP && MINLZ_BETTER2MB_HASH_CONST_RBP
#error "RBP cannot hold both the L2 table base and hash constant"
#endif

#ifndef MINLZ_ENABLE_FAST2MB_GOASM
#define MINLZ_ENABLE_FAST2MB_GOASM 0
#endif

#ifndef MINLZ_ENABLE_SUPERFAST2MB_GOASM
#define MINLZ_ENABLE_SUPERFAST2MB_GOASM 1
#endif

#ifndef MINLZ_ENABLE_L1_2MB_GOASM
#define MINLZ_ENABLE_L1_2MB_GOASM 1
#endif

#ifndef MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
#define MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM 1
#endif

#define MINLZ_ENABLE_L1_2MB_INLINE_ACTIVE \
    (MINLZ_HAVE_INLINE_ASM_AMD64 && MINLZ_ENABLE_L1_2MB_GOASM && MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM)

#ifndef MINLZ_L1_2MB_REG_NEXT_EMIT
#define MINLZ_L1_2MB_REG_NEXT_EMIT 1
#endif

#ifndef MINLZ_L1_2MB_REG_LAST_OFFSET
#define MINLZ_L1_2MB_REG_LAST_OFFSET 1
#endif

#ifndef MINLZ_ENABLE_L1_2MB_JSON_INLINE_ASM
#define MINLZ_ENABLE_L1_2MB_JSON_INLINE_ASM 1
#endif

#ifndef MINLZ_ENABLE_L1_8M_GOASM
#define MINLZ_ENABLE_L1_8M_GOASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER8M_GOASM
#define MINLZ_ENABLE_BETTER8M_GOASM 1
#endif

#ifndef MINLZ_ENABLE_BETTER_SMALL_GOASM
#define MINLZ_ENABLE_BETTER_SMALL_GOASM 1
#endif

#ifndef MINLZ_ENABLE_L2_16K_GENERIC_FAST
#define MINLZ_ENABLE_L2_16K_GENERIC_FAST 0
#endif

#ifndef MINLZ_ENCODE_UNCOMPRESSED_REPMOVSB_MIN
#define MINLZ_ENCODE_UNCOMPRESSED_REPMOVSB_MIN (1u << 30)
#endif

#ifndef MINLZ_ENCODE_TINY_RAW_DIRECT_MEMCPY
#define MINLZ_ENCODE_TINY_RAW_DIRECT_MEMCPY 1
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_XMM
#define MINLZ_ENABLE_RAWCOPY_SSE42_XMM 0
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_MOVSQ
#define MINLZ_ENABLE_RAWCOPY_SSE42_MOVSQ 0
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_QWORD
#define MINLZ_ENABLE_RAWCOPY_SSE42_QWORD 0
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_XMM_ALIGNED_STORE
#define MINLZ_ENABLE_RAWCOPY_SSE42_XMM_ALIGNED_STORE 0
#endif

#ifndef MINLZ_ENCODE_8K16K_DIRECT_MEMMOVE
#define MINLZ_ENCODE_8K16K_DIRECT_MEMMOVE 1
#endif

#ifndef MINLZ_ENCODE_8K16K_AVX2_GO_FWD
#define MINLZ_ENCODE_8K16K_AVX2_GO_FWD 1
#endif

#ifndef MINLZ_ENCODE_10000_AVX2_CONST
#define MINLZ_ENCODE_10000_AVX2_CONST 0
#endif

#ifndef MINLZ_ENCODE_10000_AVX2_FWD256
#define MINLZ_ENCODE_10000_AVX2_FWD256 0
#endif

#ifndef MINLZ_ENCODE_10000_AVX2_MOD2
#define MINLZ_ENCODE_10000_AVX2_MOD2 0
#endif

#ifndef MINLZ_ENCODE_10000_TOPLEVEL_FAST
#define MINLZ_ENCODE_10000_TOPLEVEL_FAST 0
#endif

#ifndef MINLZ_ENCODE_WS_SPLIT_FAST
#define MINLZ_ENCODE_WS_SPLIT_FAST 1
#endif

#ifndef MINLZ_ENCODE_WS_SPLIT_RAW_ALL
#define MINLZ_ENCODE_WS_SPLIT_RAW_ALL 1
#endif

#ifndef MINLZ_ENCODE_WS_SPLIT_BALANCED16K
#define MINLZ_ENCODE_WS_SPLIT_BALANCED16K 1
#endif

#ifndef MINLZ_ENCODE_WS_SPLIT_BALANCED2MB
#define MINLZ_ENCODE_WS_SPLIT_BALANCED2MB 1
#endif

#ifndef MINLZ_ENCODE_8K16K_AVX2_ALIGNED_STORE
#define MINLZ_ENCODE_8K16K_AVX2_ALIGNED_STORE 1
#endif

#ifndef MINLZ_ENCODE_8K16K_SSE42_BULK
#define MINLZ_ENCODE_8K16K_SSE42_BULK 0
#endif

#ifndef MINLZ_ENABLE_L2_BETTER16K_INLINE_ASM
#define MINLZ_ENABLE_L2_BETTER16K_INLINE_ASM 1
#endif

#ifndef MINLZ_BETTER16K_HASH2_32BIT
#define MINLZ_BETTER16K_HASH2_32BIT 1
#endif

#ifndef MINLZ_BETTER16K_REUSE_HASH1
#define MINLZ_BETTER16K_REUSE_HASH1 1
#endif

#ifndef MINLZ_PERIODIC_EQUAL_REP_CMPSQ
#define MINLZ_PERIODIC_EQUAL_REP_CMPSQ 1
#endif

#ifndef MINLZ_PERIODIC_EQUAL_SSE42_XOR_MIN
#define MINLZ_PERIODIC_EQUAL_SSE42_XOR_MIN (1u << 20)
#endif

#ifndef MINLZ_PERIODIC_EQUAL_SSE42_ACCUM
#define MINLZ_PERIODIC_EQUAL_SSE42_ACCUM 0
#endif

#ifndef MINLZ_PERIODIC_EQUAL_MEMCMP_MIN
#define MINLZ_PERIODIC_EQUAL_MEMCMP_MIN (1u << 20)
#endif

#ifndef MINLZ_PERIODIC_EQUAL_AVX2_512
#define MINLZ_PERIODIC_EQUAL_AVX2_512 0
#endif

#ifndef MINLZ_PERIODIC_EQUAL_MATCH16_ASM
#define MINLZ_PERIODIC_EQUAL_MATCH16_ASM 0
#endif

#ifndef MINLZ_PERIODIC_EQUAL_MATCH32_ASM
#define MINLZ_PERIODIC_EQUAL_MATCH32_ASM 0
#endif

#ifndef MINLZ_ENABLE_PERIODIC_REPEAT_SUPERFAST
#define MINLZ_ENABLE_PERIODIC_REPEAT_SUPERFAST 1
#endif

#ifndef MINLZ_ENABLE_PERIODIC_REPEAT_L1
#define MINLZ_ENABLE_PERIODIC_REPEAT_L1 1
#endif

/*
 * The periodic L1 path used to enter the generic 64K encoder even when the
 * detected period fit the already verified 16K inline-ASM bucket.  This is
 * safe to enable independently: a zero return falls back to the generic C
 * encoder and the existing output/compatibility checks remain authoritative.
 */
#ifndef MINLZ_ENABLE_L1_PERIODIC_FAST16K_INLINE
#define MINLZ_ENABLE_L1_PERIODIC_FAST16K_INLINE 1
#endif

#ifndef MINLZ_ENABLE_PERIODIC_REPEAT_L2
#define MINLZ_ENABLE_PERIODIC_REPEAT_L2 0
#endif

#ifndef MINLZ_ENABLE_PERIODIC_SEARCH_MEMCHR
#define MINLZ_ENABLE_PERIODIC_SEARCH_MEMCHR 1
#endif

#ifndef MINLZ_ENABLE_L2_PERIODIC_REPEAT_FAST
#define MINLZ_ENABLE_L2_PERIODIC_REPEAT_FAST 1
#endif

#ifndef MINLZ_ENABLE_BETTER16K_INLINE_ASM
#define MINLZ_ENABLE_BETTER16K_INLINE_ASM MINLZ_ENABLE_L2_BETTER16K_INLINE_ASM
#endif

#ifndef MINLZ_BETTER2MB_CLASSIFY_BYTES
#define MINLZ_BETTER2MB_CLASSIFY_BYTES (4u << 10)
#endif

#ifndef MINLZ_L1_2MB_CLASSIFY_BYTES
#define MINLZ_L1_2MB_CLASSIFY_BYTES (4u << 10)
#endif

#ifndef MINLZ_BALANCED_2MB_SKIP_SHIFT
#define MINLZ_BALANCED_2MB_SKIP_SHIFT 7
#endif

#ifndef MINLZ_BALANCED_2MB_SKIP_CAP
#define MINLZ_BALANCED_2MB_SKIP_CAP 99
#endif

#ifndef MINLZ_BALANCED_2MB_L_BITS
#define MINLZ_BALANCED_2MB_L_BITS 13
#endif

#ifndef MINLZ_BALANCED_2MB_S_BITS
#define MINLZ_BALANCED_2MB_S_BITS 15
#endif

#ifndef MINLZ_FASTEST_FUSE_LITERALS
#define MINLZ_FASTEST_FUSE_LITERALS 1
#endif

#ifndef MINLZ_FASTEST_2MB_SKIP_LOG
#define MINLZ_FASTEST_2MB_SKIP_LOG 6
#endif

#ifndef MINLZ_FASTEST_2MB_TABLE_BITS
#define MINLZ_FASTEST_2MB_TABLE_BITS 13
#endif

#ifndef MINLZ_FASTEST_CHECK_REPEATS
#define MINLZ_FASTEST_CHECK_REPEATS 1
#endif

#ifndef MINLZ_COPY2_LIT_MAX_LEN
#define MINLZ_COPY2_LIT_MAX_LEN 11u
#endif

size_t minlz_encode_better_block_2mb_goasm(uint8_t *dst, size_t dst_cap,
                                           const uint8_t *src, size_t src_len,
                                           uint8_t *tmp);
size_t minlz_encode_fast_block_2mb_goasm(uint8_t *dst, size_t dst_cap,
                                         const uint8_t *src, size_t src_len,
                                         uint8_t *tmp);
size_t minlz_encode_l1_block_2mb_goasm(uint8_t *dst, size_t dst_cap,
                                       const uint8_t *src, size_t src_len,
                                       uint8_t *tmp);
size_t minlz_encode_l1_block_8m_goasm(uint8_t *dst, size_t dst_cap,
                                      const uint8_t *src, size_t src_len,
                                      uint8_t *tmp);
size_t minlz_encode_better_block_8m_goasm(uint8_t *dst, size_t dst_cap,
                                          const uint8_t *src, size_t src_len,
                                          uint8_t *tmp);
size_t minlz_encode_better_block_512k_goasm(uint8_t *dst, size_t dst_cap,
                                            const uint8_t *src, size_t src_len,
                                            uint8_t *tmp);
size_t minlz_encode_better_block_64k_goasm(uint8_t *dst, size_t dst_cap,
                                           const uint8_t *src, size_t src_len,
                                           uint8_t *tmp);
size_t minlz_encode_better_block_16k_goasm(uint8_t *dst, size_t dst_cap,
                                           const uint8_t *src, size_t src_len,
                                           uint8_t *tmp);
size_t minlz_encode_better_block_4k_goasm(uint8_t *dst, size_t dst_cap,
                                          const uint8_t *src, size_t src_len,
                                          uint8_t *tmp);
size_t minlz_encode_better_block_1k_goasm(uint8_t *dst, size_t dst_cap,
                                          const uint8_t *src, size_t src_len,
                                          uint8_t *tmp);

static uint32_t minlz_hash4(uint32_t u, unsigned bits) {
    return (uint32_t)((u * 2654435761u) >> ((32u - bits) & 31u));
}

static uint32_t minlz_hash5(uint64_t u, unsigned bits) {
    const uint64_t prime5bytes = 889523592379ull;
    return (uint32_t)(((u << (64u - 40u)) * prime5bytes) >> ((64u - bits) & 63u));
}

static uint32_t minlz_hash6(uint64_t u, unsigned bits) {
    const uint64_t prime6bytes = 227718039650203ull;
    return (uint32_t)(((u << (64u - 48u)) * prime6bytes) >> ((64u - bits) & 63u));
}

static uint32_t minlz_hash7(uint64_t u, unsigned bits) {
    const uint64_t prime7bytes = 58295818150454627ull;
    return (uint32_t)(((u << (64u - 56u)) * prime7bytes) >> ((64u - bits) & 63u));
}

static uint32_t minlz_hash8(uint64_t u, unsigned bits) {
    const uint64_t prime8bytes = 0xcf1bbcdcb7a56463ull;
    return (uint32_t)((u * prime8bytes) >> ((64u - bits) & 63u));
}

static MINLZ_ALWAYS_INLINE void minlz_fastest64k_probe_inline_asm_amd64(
    uint16_t *table, uint64_t cv, unsigned hash_shift, uint16_t pos,
    int *candidate0, int *candidate1, uint32_t *hash2_out) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uint64_t h0 = cv;
    uint64_t h1 = cv >> 8;
    uint64_t h2 = cv >> 16;
    uint32_t c0;
    uint32_t c1;
    const uint64_t prime5 = 889523592379ull << 24;
    const uint16_t pos1 = (uint16_t)(pos + 1u);
    __asm__ volatile(
        "imulq %[prime5], %[h0]\n\t"
        "imulq %[prime5], %[h1]\n\t"
        "imulq %[prime5], %[h2]\n\t"
        "shrq %b[shift], %[h0]\n\t"
        "shrq %b[shift], %[h1]\n\t"
        "shrq %b[shift], %[h2]\n\t"
        "movzwl (%[table],%[h0],2), %k[c0]\n\t"
        "movzwl (%[table],%[h1],2), %k[c1]\n\t"
        "movw %w[pos], (%[table],%[h0],2)\n\t"
        "movw %w[pos1], (%[table],%[h1],2)"
        : [h0] "+&r"(h0), [h1] "+&r"(h1), [h2] "+&r"(h2),
          [c0] "=&r"(c0), [c1] "=&r"(c1)
        : [table] "r"(table), [prime5] "r"(prime5), [shift] "Jc"(hash_shift),
          [pos] "r"(pos), [pos1] "r"(pos1)
        : "cc", "memory");
    *candidate0 = (int)c0;
    *candidate1 = (int)c1;
    *hash2_out = (uint32_t)h2;
#else
    const unsigned bits = 64u - hash_shift;
    uint32_t hash0 = minlz_hash5(cv, bits);
    uint32_t hash1 = minlz_hash5(cv >> 8, bits);
    *candidate0 = (int)table[hash0];
    *candidate1 = (int)table[hash1];
    table[hash0] = pos;
    table[hash1] = (uint16_t)(pos + 1u);
    *hash2_out = minlz_hash5(cv >> 16, bits);
#endif
}

static unsigned minlz_ctz64(uint64_t v) {
#if defined(__GNUC__) || defined(__clang__)
    return (unsigned)__builtin_ctzll(v);
#else
    unsigned n = 0;
    while ((v & 1u) == 0) {
        n++;
        v >>= 1;
    }
    return n;
#endif
}

static inline size_t minlz_match_len_inline_asm_amd64(const uint8_t *a, const uint8_t *b,
                                                      size_t max_len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t n = 0;
    size_t qwords = max_len >> 3;
    if (qwords != 0) {
        size_t off;
        uint64_t diff;
        __asm__ volatile(
            "xorq %[off], %[off]\n\t"
            "1:\n\t"
            "movq (%[a],%[off]), %[diff]\n\t"
            "xorq (%[b],%[off]), %[diff]\n\t"
            "jnz 2f\n\t"
            "addq $8, %[off]\n\t"
            "decq %[qwords]\n\t"
            "jnz 1b\n\t"
            "xorq %[diff], %[diff]\n\t"
            "2:"
            : [off] "=&r"(off), [diff] "=&r"(diff), [qwords] "+&r"(qwords)
            : [a] "r"(a), [b] "r"(b)
            : "cc", "memory");
        n = off;
        if (diff != 0) {
            size_t bit;
            __asm__ volatile("bsfq %1, %0" : "=r"(bit) : "r"(diff) : "cc");
            return n + (bit >> 3);
        }
    }
    while (n < max_len && a[n] == b[n]) {
        n++;
    }
    return n;
#elif defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
    return minlz_match_len_asm_amd64(a, b, max_len);
#else
    size_t n = 0;
    while (max_len - n >= 8) {
        uint64_t diff = minlz_load64(a + n) ^ minlz_load64(b + n);
        if (diff != 0) {
            return n + (size_t)(minlz_ctz64(diff) >> 3);
        }
        n += 8;
    }
    while (n < max_len && a[n] == b[n]) {
        n++;
    }
    return n;
#endif
}

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64
static MINLZ_ALWAYS_INLINE __m128i minlz_equal_aligned_sse2(const uint8_t *a,
                                                            const uint8_t *b) {
    return _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(const void *)a),
                         _mm_load_si128((const __m128i *)(const void *)b));
}

/* These loops return the matched prefix of complete vectors. The caller
 * handles the short tail with the same bounds as the scalar path. */
static MINLZ_NOINLINE size_t minlz_match_vectors_sse2(const uint8_t *a, const uint8_t *b, size_t len) {
    size_t n = 0;
    /* Reduce complete groups to one mismatch test. The short loop
     * below finds the first different byte in a group that fails. */
    if (len >= (128u << 10)) {
        /* An aligned second operand can be folded into PCMPEQB. */
        const size_t head = (16u - ((uintptr_t)b & 15u)) & 15u;
        if (head != 0u) {
            const __m128i x = _mm_loadu_si128((const __m128i *)(const void *)a);
            const __m128i y = _mm_loadu_si128((const __m128i *)(const void *)b);
            const unsigned diff = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(x, y)) ^ 0xffffu;
            if (diff != 0) {
                return minlz_ctz64(diff);
            }
            n = head;
        }
        while (len - n >= 128u) {
            const __m128i eq0 = minlz_equal_aligned_sse2(a + n, b + n);
            const __m128i eq1 = minlz_equal_aligned_sse2(a + n + 16u, b + n + 16u);
            const __m128i eq2 = minlz_equal_aligned_sse2(a + n + 32u, b + n + 32u);
            const __m128i eq3 = minlz_equal_aligned_sse2(a + n + 48u, b + n + 48u);
            const __m128i eq4 = minlz_equal_aligned_sse2(a + n + 64u, b + n + 64u);
            const __m128i eq5 = minlz_equal_aligned_sse2(a + n + 80u, b + n + 80u);
            const __m128i eq6 = minlz_equal_aligned_sse2(a + n + 96u, b + n + 96u);
            const __m128i eq7 = minlz_equal_aligned_sse2(a + n + 112u, b + n + 112u);
            const __m128i eq01 = _mm_and_si128(eq0, eq1);
            const __m128i eq23 = _mm_and_si128(eq2, eq3);
            const __m128i eq45 = _mm_and_si128(eq4, eq5);
            const __m128i eq67 = _mm_and_si128(eq6, eq7);
            const __m128i eq0123 = _mm_and_si128(eq01, eq23);
            const __m128i eq4567 = _mm_and_si128(eq45, eq67);
            if (_mm_movemask_epi8(_mm_and_si128(eq0123, eq4567)) != 0xffff) {
                break;
            }
            n += 128u;
        }
    }
    while (len - n >= 32u) {
        const __m128i x0 = _mm_loadu_si128((const __m128i *)(const void *)(a + n));
        const __m128i y0 = _mm_loadu_si128((const __m128i *)(const void *)(b + n));
        const __m128i x1 = _mm_loadu_si128((const __m128i *)(const void *)(a + n + 16u));
        const __m128i y1 = _mm_loadu_si128((const __m128i *)(const void *)(b + n + 16u));
        const __m128i eq0 = _mm_cmpeq_epi8(x0, y0);
        const __m128i eq1 = _mm_cmpeq_epi8(x1, y1);
        if (_mm_movemask_epi8(_mm_and_si128(eq0, eq1)) != 0xffff) {
            const unsigned first = (unsigned)_mm_movemask_epi8(eq0) ^ 0xffffu;
            if (first != 0) {
                return n + minlz_ctz64(first);
            }
            return n + 16u + minlz_ctz64((unsigned)_mm_movemask_epi8(eq1) ^ 0xffffu);
        }
        n += 32u;
    }
    if (len - n >= 16u) {
        const __m128i x = _mm_loadu_si128((const __m128i *)(const void *)(a + n));
        const __m128i y = _mm_loadu_si128((const __m128i *)(const void *)(b + n));
        const unsigned diff = (unsigned)_mm_movemask_epi8(_mm_cmpeq_epi8(x, y)) ^ 0xffffu;
        if (diff != 0) {
            return n + minlz_ctz64(diff);
        }
        n += 16u;
    }
    return n;
}

#if defined(MINLZ_HAVE_AVX2) && MINLZ_HAVE_AVX2
static MINLZ_NOINLINE __attribute__((target("avx2"))) size_t
minlz_match_vectors_avx2(const uint8_t *a, const uint8_t *b, size_t len) {
    size_t n = 0;
    while (len - n >= 64u) {
        const __m256i x0 = _mm256_loadu_si256((const __m256i *)(const void *)(a + n));
        const __m256i y0 = _mm256_loadu_si256((const __m256i *)(const void *)(b + n));
        const __m256i x1 = _mm256_loadu_si256((const __m256i *)(const void *)(a + n + 32u));
        const __m256i y1 = _mm256_loadu_si256((const __m256i *)(const void *)(b + n + 32u));
        const __m256i eq0 = _mm256_cmpeq_epi8(x0, y0);
        const __m256i eq1 = _mm256_cmpeq_epi8(x1, y1);
        if (_mm256_movemask_epi8(_mm256_and_si256(eq0, eq1)) != -1) {
            const uint32_t first = ~(uint32_t)_mm256_movemask_epi8(eq0);
            if (first != 0) {
                return n + minlz_ctz64(first);
            }
            return n + 32u + minlz_ctz64(~(uint32_t)_mm256_movemask_epi8(eq1));
        }
        n += 64u;
    }
    if (len - n >= 32u) {
        const __m256i x = _mm256_loadu_si256((const __m256i *)(const void *)(a + n));
        const __m256i y = _mm256_loadu_si256((const __m256i *)(const void *)(b + n));
        const uint32_t diff = ~(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(x, y));
        if (diff != 0) {
            return n + minlz_ctz64(diff);
        }
        n += 32u;
    }
    return n;
}
#endif

#if defined(MINLZ_HAVE_AVX512) && MINLZ_HAVE_AVX512
static MINLZ_NOINLINE __attribute__((target("avx512f,avx512bw"))) size_t
minlz_match_vectors_avx512(const uint8_t *a, const uint8_t *b, size_t len) {
    size_t n = 0;
    while (len - n >= 128u) {
        const __m512i x0 = _mm512_loadu_si512((const void *)(a + n));
        const __m512i y0 = _mm512_loadu_si512((const void *)(b + n));
        const __m512i x1 = _mm512_loadu_si512((const void *)(a + n + 64u));
        const __m512i y1 = _mm512_loadu_si512((const void *)(b + n + 64u));
        const __m512i xor0 = _mm512_xor_si512(x0, y0);
        /* OR of the two independent XOR differences. */
        const __m512i any = _mm512_ternarylogic_epi64(xor0, x1, y1, 0xf6);
        if (_mm512_test_epi64_mask(any, any) != 0) {
            const uint64_t first = _mm512_cmpneq_epi8_mask(x0, y0);
            if (first != 0) {
                return n + minlz_ctz64(first);
            }
            return n + 64u + minlz_ctz64(_mm512_cmpneq_epi8_mask(x1, y1));
        }
        n += 128u;
    }
    if (len - n >= 64u) {
        const __m512i x = _mm512_loadu_si512((const void *)(a + n));
        const __m512i y = _mm512_loadu_si512((const void *)(b + n));
        const uint64_t diff = _mm512_cmpneq_epi8_mask(x, y);
        if (diff != 0) {
            return n + minlz_ctz64(diff);
        }
        n += 64u;
    }
    return n;
}
#endif

static MINLZ_NOINLINE size_t minlz_match_vectors(const uint8_t *a, const uint8_t *b, size_t len) {
#if defined(MINLZ_HAVE_AVX512) && MINLZ_HAVE_AVX512
    if (minlz_backend_has_avx512()) {
        return minlz_match_vectors_avx512(a, b, len);
    }
#endif
#if defined(MINLZ_HAVE_AVX2) && MINLZ_HAVE_AVX2
    if (minlz_backend_has_avx2()) {
        return minlz_match_vectors_avx2(a, b, len);
    }
#endif
    return minlz_match_vectors_sse2(a, b, len);
}
#endif

typedef enum {
    MINLZ_L1_MATCH_SCALAR,
    MINLZ_L1_MATCH_LONG,
    MINLZ_L1_MATCH_LONG_SSE2,
    MINLZ_L1_MATCH_PREFIX_SSE2
} minlz_l1_match_mode;

static MINLZ_ALWAYS_INLINE size_t minlz_match_len_fastest(const uint8_t *a, const uint8_t *b,
                                                         size_t max_len, minlz_l1_match_mode mode) {
    size_t n = 0;
    (void)mode;
    if (max_len >= 32) {
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64
        if (mode == MINLZ_L1_MATCH_PREFIX_SSE2) {
            const __m128i a0 = _mm_loadu_si128((const __m128i *)(const void *)a);
            const __m128i b0 = _mm_loadu_si128((const __m128i *)(const void *)b);
            const __m128i a1 = _mm_loadu_si128((const __m128i *)(const void *)(a + 16u));
            const __m128i b1 = _mm_loadu_si128((const __m128i *)(const void *)(b + 16u));
            const uint32_t lo = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(a0, b0));
            const uint32_t hi = (uint32_t)_mm_movemask_epi8(_mm_cmpeq_epi8(a1, b1));
            const uint32_t diff = ~(lo | (hi << 16));
            if (diff != 0u) {
                return minlz_ctz64(diff);
            }
        } else
#endif
        {
            uint64_t diff = minlz_load64(a) ^ minlz_load64(b);
            if (diff != 0) {
                return (size_t)(minlz_ctz64(diff) >> 3);
            }
            diff = minlz_load64(a + 8) ^ minlz_load64(b + 8);
            if (diff != 0) {
                return 8u + (size_t)(minlz_ctz64(diff) >> 3);
            }
            diff = minlz_load64(a + 16) ^ minlz_load64(b + 16);
            if (diff != 0) {
                return 16u + (size_t)(minlz_ctz64(diff) >> 3);
            }
            diff = minlz_load64(a + 24) ^ minlz_load64(b + 24);
            if (diff != 0) {
                return 24u + (size_t)(minlz_ctz64(diff) >> 3);
            }
        }
        n = 32;
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64
        if (mode == MINLZ_L1_MATCH_LONG_SSE2 && max_len >= 128u &&
            ((minlz_load64(a + 32u) ^ minlz_load64(b + 32u)) |
             (minlz_load64(a + 40u) ^ minlz_load64(b + 40u)) |
             (minlz_load64(a + 48u) ^ minlz_load64(b + 48u)) |
             (minlz_load64(a + 56u) ^ minlz_load64(b + 56u))) == 0) {
            n = 64u + minlz_match_vectors_sse2(a + 64u, b + 64u, max_len - 64u);
        }
        if (mode == MINLZ_L1_MATCH_LONG && max_len >= (4u << 10) &&
            ((minlz_load64(a + 32u) ^ minlz_load64(b + 32u)) |
             (minlz_load64(a + 40u) ^ minlz_load64(b + 40u)) |
             (minlz_load64(a + 48u) ^ minlz_load64(b + 48u)) |
             (minlz_load64(a + 56u) ^ minlz_load64(b + 56u))) == 0) {
            n = 64u + minlz_match_vectors(a + 64u, b + 64u, max_len - 64u);
        }
#endif
    }
    while (max_len - n >= 16) {
        uint64_t diff0 = minlz_load64(a + n) ^ minlz_load64(b + n);
        uint64_t diff1;
        if (diff0 != 0) {
            return n + (size_t)(minlz_ctz64(diff0) >> 3);
        }
        diff1 = minlz_load64(a + n + 8) ^ minlz_load64(b + n + 8);
        if (diff1 != 0) {
            return n + 8u + (size_t)(minlz_ctz64(diff1) >> 3);
        }
        n += 16;
    }
    while (max_len - n >= 8) {
        uint64_t diff = minlz_load64(a + n) ^ minlz_load64(b + n);
        if (diff != 0) {
            return n + (size_t)(minlz_ctz64(diff) >> 3);
        }
        n += 8;
    }
    while (n < max_len && a[n] == b[n]) {
        n++;
    }
    return n;
}

static inline size_t minlz_match_len_fast(const uint8_t *a, const uint8_t *b, size_t max_len) {
    size_t n = 0;
    if (max_len >= 32) {
        uint64_t diff = minlz_load64(a) ^ minlz_load64(b);
        if (diff != 0) {
            return (size_t)(minlz_ctz64(diff) >> 3);
        }
        diff = minlz_load64(a + 8) ^ minlz_load64(b + 8);
        if (diff != 0) {
            return 8u + (size_t)(minlz_ctz64(diff) >> 3);
        }
        diff = minlz_load64(a + 16) ^ minlz_load64(b + 16);
        if (diff != 0) {
            return 16u + (size_t)(minlz_ctz64(diff) >> 3);
        }
        diff = minlz_load64(a + 24) ^ minlz_load64(b + 24);
        if (diff != 0) {
            return 24u + (size_t)(minlz_ctz64(diff) >> 3);
        }
        n = 32;
#if defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
        if (max_len >= 256) {
            minlz_backend backend = minlz_select_backend();
            if (backend == MINLZ_BACKEND_ASM_AMD64 || backend == MINLZ_BACKEND_AVX2 ||
                backend == MINLZ_BACKEND_AVX512) {
                return n + minlz_match_len_inline_asm_amd64(a + n, b + n, max_len - n);
            }
        }
#endif
    }
    while (max_len - n >= 8) {
        uint64_t diff = minlz_load64(a + n) ^ minlz_load64(b + n);
        if (diff != 0) {
            return n + (size_t)(minlz_ctz64(diff) >> 3);
        }
        n += 8;
    }
    while (n < max_len && a[n] == b[n]) {
        n++;
    }
    return n;
}

static inline size_t minlz_match_len_fast_known_inline_asm(const uint8_t *a, const uint8_t *b,
                                                           size_t max_len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    size_t n = 0;
    if (max_len >= 32) {
        uint64_t diff = minlz_load64(a) ^ minlz_load64(b);
        if (diff != 0) {
            return (size_t)(minlz_ctz64(diff) >> 3);
        }
        diff = minlz_load64(a + 8) ^ minlz_load64(b + 8);
        if (diff != 0) {
            return 8u + (size_t)(minlz_ctz64(diff) >> 3);
        }
        diff = minlz_load64(a + 16) ^ minlz_load64(b + 16);
        if (diff != 0) {
            return 16u + (size_t)(minlz_ctz64(diff) >> 3);
        }
        diff = minlz_load64(a + 24) ^ minlz_load64(b + 24);
        if (diff != 0) {
            return 24u + (size_t)(minlz_ctz64(diff) >> 3);
        }
        n = 32;
        if (max_len >= 256) {
            return n + minlz_match_len_inline_asm_amd64(a + n, b + n, max_len - n);
        }
    }
    while (max_len - n >= 8) {
        uint64_t diff = minlz_load64(a + n) ^ minlz_load64(b + n);
        if (diff != 0) {
            return n + (size_t)(minlz_ctz64(diff) >> 3);
        }
        n += 8;
    }
    while (n < max_len && a[n] == b[n]) {
        n++;
    }
    return n;
#else
    return minlz_match_len_fast(a, b, max_len);
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_better_probe_512k_inline_asm_amd64(
    const uint8_t *src, uint32_t *l_table, uint32_t *s_table,
    uint64_t cv, uint32_t pos, int *candidate_l, int *candidate_s,
    uint64_t *val_long, uint64_t *val_short) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uint64_t cl;
    uint64_t cs;
    uint64_t vl;
    uint64_t vs;
    const uint64_t prime7 = 58295818150454627ull;
    __asm__ volatile(
        "movq %[cv], %%r10\n\t"
        "shlq $8, %%r10\n\t"
        "imulq %[prime7], %%r10\n\t"
        "shrq $51, %%r10\n\t"
        "movl (%[lt],%%r10,4), %k[cl]\n\t"
        "movl %[pos], (%[lt],%%r10,4)\n\t"
        "movl %k[cv], %%r11d\n\t"
        "imull $-1640531535, %%r11d\n\t"
        "shrl $17, %%r11d\n\t"
        "movl (%[st],%%r11,4), %k[cs]\n\t"
        "movl %[pos], (%[st],%%r11,4)\n\t"
        "movq (%[src],%[cl],1), %[vl]\n\t"
        "movq (%[src],%[cs],1), %[vs]"
        : [cl] "=&r"(cl), [cs] "=&r"(cs), [vl] "=&r"(vl), [vs] "=&r"(vs)
        : [src] "r"(src), [lt] "r"(l_table), [st] "r"(s_table),
          [cv] "r"(cv), [pos] "r"(pos), [prime7] "r"(prime7)
        : "r10", "r11", "cc", "memory");
    *candidate_l = (int)cl;
    *candidate_s = (int)cs;
    *val_long = vl;
    *val_short = vs;
#else
    (void)src;
    (void)l_table;
    (void)s_table;
    (void)cv;
    (void)pos;
    *candidate_l = 0;
    *candidate_s = 0;
    *val_long = 0;
    *val_short = 0;
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_better_index_pair_512k_inline_asm_amd64(
    const uint8_t *src, uint32_t *l_table, uint32_t *s_table,
    uint32_t index0, uint32_t index1) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uint64_t i0 = index0;
    uint64_t i1 = index1;
    uint32_t i0_next = index0 + 1u;
    uint32_t i1_next = index1 + 1u;
    const uint64_t prime7 = 58295818150454627ull;
    __asm__ volatile(
        "movq (%[src],%[i0],1), %%r10\n\t"
        "movq %%r10, %%r11\n\t"
        "shlq $8, %%r10\n\t"
        "imulq %[prime7], %%r10\n\t"
        "shrq $51, %%r10\n\t"
        "movl %[i0d], (%[lt],%%r10,4)\n\t"
        "shrq $8, %%r11\n\t"
        "imull $-1640531535, %%r11d\n\t"
        "shrl $17, %%r11d\n\t"
        "movl %[i0n], (%[st],%%r11,4)\n\t"
        "movq (%[src],%[i1],1), %%r12\n\t"
        "movq %%r12, %%r13\n\t"
        "shlq $8, %%r12\n\t"
        "imulq %[prime7], %%r12\n\t"
        "shrq $51, %%r12\n\t"
        "movl %[i1d], (%[lt],%%r12,4)\n\t"
        "shrq $8, %%r13\n\t"
        "imull $-1640531535, %%r13d\n\t"
        "shrl $17, %%r13d\n\t"
        "movl %[i1n], (%[st],%%r13,4)"
        :
        : [src] "r"(src), [lt] "r"(l_table), [st] "r"(s_table),
          [i0] "r"(i0), [i1] "r"(i1), [i0d] "r"(index0), [i1d] "r"(index1),
          [i0n] "r"(i0_next), [i1n] "r"(i1_next), [prime7] "r"(prime7)
        : "r10", "r11", "r12", "r13", "cc", "memory");
#else
    (void)src;
    (void)l_table;
    (void)s_table;
    (void)index0;
    (void)index1;
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_better_index_long_pair_512k_inline_asm_amd64(
    const uint8_t *src, uint32_t *l_table, uint32_t index0, uint32_t index1) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uint64_t i0 = index0;
    uint64_t i1 = index1;
    const uint64_t prime7 = 58295818150454627ull;
    __asm__ volatile(
        "movq (%[src],%[i0],1), %%r10\n\t"
        "shlq $8, %%r10\n\t"
        "imulq %[prime7], %%r10\n\t"
        "shrq $51, %%r10\n\t"
        "movl %[i0d], (%[lt],%%r10,4)\n\t"
        "movq (%[src],%[i1],1), %%r11\n\t"
        "shlq $8, %%r11\n\t"
        "imulq %[prime7], %%r11\n\t"
        "shrq $51, %%r11\n\t"
        "movl %[i1d], (%[lt],%%r11,4)"
        :
        : [src] "r"(src), [lt] "r"(l_table), [i0] "r"(i0), [i1] "r"(i1),
          [i0d] "r"(index0), [i1d] "r"(index1), [prime7] "r"(prime7)
        : "r10", "r11", "cc", "memory");
#else
    (void)src;
    (void)l_table;
    (void)index0;
    (void)index1;
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_better_long_lookup_shift1_512k_inline_asm_amd64(
    const uint8_t *src, uint32_t *l_table, uint64_t cv, uint32_t pos,
    int *candidate, uint32_t *candidate_value) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uint64_t c;
    uint32_t v;
    const uint64_t prime7 = 58295818150454627ull;
    __asm__ volatile(
        "movq %[cv], %%r10\n\t"
        "shrq $8, %%r10\n\t"
        "shlq $8, %%r10\n\t"
        "imulq %[prime7], %%r10\n\t"
        "shrq $51, %%r10\n\t"
        "movl (%[lt],%%r10,4), %k[c]\n\t"
        "movl %[pos], (%[lt],%%r10,4)\n\t"
        "movl (%[src],%[c],1), %[v]"
        : [c] "=&r"(c), [v] "=&r"(v)
        : [src] "r"(src), [lt] "r"(l_table), [cv] "r"(cv),
          [pos] "r"(pos), [prime7] "r"(prime7)
        : "r10", "cc", "memory");
    *candidate = (int)c;
    *candidate_value = v;
#else
    (void)src;
    (void)l_table;
    (void)cv;
    (void)pos;
    *candidate = 0;
    *candidate_value = 0;
#endif
}

static inline void minlz_extend_match_int(const uint8_t *src, int *s,
                                          int *candidate, int src_len) {
    size_t matched = minlz_match_len_fast(src + *s, src + *candidate,
                                          (size_t)(src_len - *s));
    *s += (int)matched;
    *candidate += (int)matched;
}

static inline void minlz_extend_match_int_better(const uint8_t *src, int *s,
                                                 int *candidate, int src_len,
                                                 int use_known_inline_asm) {
    size_t matched = use_known_inline_asm ?
        minlz_match_len_fast_known_inline_asm(src + *s, src + *candidate,
                                              (size_t)(src_len - *s)) :
        minlz_match_len_fast(src + *s, src + *candidate, (size_t)(src_len - *s));
    *s += (int)matched;
    *candidate += (int)matched;
}

static MINLZ_ALWAYS_INLINE void minlz_extend_match_int_fastest_impl(
    const uint8_t *src, int *s, int *candidate, int src_len, minlz_l1_match_mode mode) {
    const size_t matched = minlz_match_len_fastest(src + *s, src + *candidate,
                                                  (size_t)(src_len - *s), mode);
    *s += (int)matched;
    *candidate += (int)matched;
}

static inline void minlz_extend_match_int_fastest(const uint8_t *src, int *s,
                                                 int *candidate, int src_len) {
    minlz_extend_match_int_fastest_impl(src, s, candidate, src_len, MINLZ_L1_MATCH_SCALAR);
}
static inline void minlz_extend_match_size(const uint8_t *src, size_t *s,
                                           size_t *candidate, size_t src_len) {
    size_t matched = minlz_match_len_fast(src + *s, src + *candidate, src_len - *s);
    *s += matched;
    *candidate += matched;
}

static int minlz_valid_level(int level) {
    return level >= MINLZ_LEVEL_SUPERFAST && level <= MINLZ_LEVEL_SMALLEST;
}

static MINLZ_ALWAYS_INLINE int minlz_obviously_structured(const uint8_t *src,
                                                          size_t src_len) {
    if (src_len < 2u) {
        return 0;
    }
    return (src[0] == '{' || src[0] == '[') &&
           (src[1] == '"' || src[1] == '{' || src[1] == '[' || src[1] == '\n');
}

static int minlz_likely_incompressible(const uint8_t *src, size_t src_len) {
    uint16_t table[4096];
    size_t limit = src_len < (64u << 10) ? src_len : (64u << 10);
    size_t s;
    unsigned hits = 0;
    if (src_len < 4096) {
        return 0;
    }
    memset(table, 0, sizeof(table));
    for (s = 1; s + 4 <= limit; s += 16) {
        uint32_t v = minlz_load32(src + s);
        uint32_t h = minlz_hash4(v, 12);
        uint16_t prev = table[h];
        table[h] = (uint16_t)(s + 1);
        if (prev != 0 && v == minlz_load32(src + (size_t)prev - 1u)) {
            hits++;
            if (hits >= 4) {
                return 0;
            }
        }
    }
    return 1;
}

static int minlz_balanced_likely_incompressible(const uint8_t *src,
                                                size_t src_len) {
    size_t probe_len = 4096u;
    if (!minlz_likely_incompressible(src, src_len)) {
        return 0;
    }
    if (src_len < (128u << 10)) {
        return 1;
    }
    if (!minlz_likely_incompressible(src + src_len / 3u, probe_len) ||
        !minlz_likely_incompressible(src + src_len * 2u / 3u, probe_len)) {
        return 0;
    }
    return 1;
}

typedef struct {
    int prefer_goasm;
    int structured;
} minlz_better2mb_shape;

static minlz_better2mb_shape minlz_better2mb_classify(const uint8_t *src, size_t src_len) {
    minlz_better2mb_shape shape;
    size_t n = src_len < MINLZ_BETTER2MB_CLASSIFY_BYTES ?
        src_len : MINLZ_BETTER2MB_CLASSIFY_BYTES;
    size_t samples = 0;
    size_t printable = 0;
    size_t letters = 0;
    size_t spaces = 0;
    size_t controls = 0;
    size_t high = 0;
    size_t structured = 0;
    size_t quotes = 0;
    size_t i;
    shape.prefer_goasm = 1;
    shape.structured = 0;
    if (n < 4096u) {
        return shape;
    }
    if ((src[0] == '{' || src[0] == '[') &&
        (src[1] == '"' || src[1] == '{' || src[1] == '[' || src[1] == '\n')) {
        shape.prefer_goasm = 0;
        shape.structured = 1;
        return shape;
    }
    for (i = 0; i < n; i += 16u) {
        uint8_t c = src[i];
        samples++;
        if (c >= 'A' && c <= 'Z') {
            letters++;
        } else if (c >= 'a' && c <= 'z') {
            letters++;
        }
        spaces += (c == ' ');
        high += (c >= 128u);
        controls += (c < 32u && c != '\n' && c != '\r' && c != '\t');
        printable += ((c >= 32u && c <= 126u) || c == '\n' || c == '\r' || c == '\t');
        if (c == '"' || c == ':' || c == ',' || c == '{' || c == '}' ||
            c == '[' || c == ']') {
            structured++;
            quotes += (c == '"');
        }
        if ((samples == 16u || samples == 32u || samples == 64u) &&
            printable * 100u >= samples * 88u &&
            (structured * 100u >= samples * 12u ||
             quotes * 100u >= samples * 8u)) {
            shape.prefer_goasm = 0;
            shape.structured = 1;
            return shape;
        }
        if ((samples == 32u || samples == 64u) &&
            printable * 100u >= samples * 88u &&
            letters * 100u >= samples * 42u &&
            spaces * 100u >= samples * 2u &&
            controls * 100u <= samples * 3u &&
            high * 100u <= samples * 2u &&
            structured * 100u < samples * 15u &&
            quotes * 100u < samples * 10u) {
            shape.prefer_goasm = 1;
            shape.structured = 0;
            return shape;
        }
    }
    /*
     * Keep natural printable/log-like blocks on the exact 2MB asm bucket when
     * they match its profile. Forcing every structured block to the inline
     * candidate regresses logjson-1m L2 on the Linux gate.
     */
    shape.structured = printable * 100u >= samples * 88u &&
                       (structured * 100u >= samples * 12u ||
                        quotes * 100u >= samples * 8u);
    if (printable * 100u >= samples * 88u &&
        letters * 100u >= samples * 42u &&
        spaces * 100u >= samples * 2u &&
        controls * 100u <= samples * 3u &&
        high * 100u <= samples * 2u &&
        structured * 100u < samples * 15u &&
        quotes * 100u < samples * 10u) {
        shape.prefer_goasm = 1;
        return shape;
    }
    shape.prefer_goasm = structured * 100u < samples * 12u && quotes * 100u < samples * 8u;
    return shape;
}

static int minlz_better2mb_prefers_goasm(const uint8_t *src, size_t src_len) {
    return minlz_better2mb_classify(src, src_len).prefer_goasm;
}

#if MINLZ_ENABLE_L1_2MB_INLINE_ACTIVE
static int minlz_fastest_l1_2mb_prefers_goasm(const uint8_t *src, size_t src_len) {
    size_t n = src_len < MINLZ_L1_2MB_CLASSIFY_BYTES ?
        src_len : MINLZ_L1_2MB_CLASSIFY_BYTES;
    size_t samples = 0;
    size_t printable = 0;
    size_t letters = 0;
    size_t spaces = 0;
    size_t controls = 0;
    size_t high = 0;
    size_t structured = 0;
    size_t quotes = 0;
    size_t i;
    if (n < 4096u) {
        return 0;
    }
    if (MINLZ_ENABLE_L1_2MB_JSON_INLINE_ASM &&
        (src[0] == '{' || src[0] == '[') &&
        (src[1] == '"' || src[1] == '{' || src[1] == '[' || src[1] == '\n')) {
        return 1;
    }
    for (i = 0; i < n; i += 16u) {
        uint8_t c = src[i];
        samples++;
        if (c >= 'A' && c <= 'Z') {
            letters++;
        } else if (c >= 'a' && c <= 'z') {
            letters++;
        }
        spaces += (c == ' ');
        high += (c >= 128u);
        controls += (c < 32u && c != '\n' && c != '\r' && c != '\t');
        printable += ((c >= 32u && c <= 126u) || c == '\n' || c == '\r' || c == '\t');
        if (c == '"' || c == ':' || c == ',' || c == '{' || c == '}' ||
            c == '[' || c == ']') {
            structured++;
            quotes += (c == '"');
        }
    }
    /* Other structured data stays on the smaller tuned C encoder. */
    return printable * 100u >= samples * 88u &&
           letters * 100u >= samples * 45u &&
           spaces * 100u >= samples * 2u &&
           controls * 100u <= samples * 3u &&
           high * 100u <= samples * 2u &&
           structured * 100u < samples * 8u &&
           quotes * 100u < samples * 6u;
}
#endif

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
/* Classify exactly the same stride-16 sample as the portable probe. */
static void minlz_l1_sample_counts(const uint8_t *src, size_t n,
                                 size_t *printable, size_t *controls, size_t *high) {
    const __m128i zero = _mm_setzero_si128();
    __m128i p = zero, c = zero, h = zero;
    size_t i;
    for (i = 0; i + 256u <= n; i += 256u) {
        const uint8_t *s = src + i;
        const __m128i v = _mm_setr_epi8(s[0], s[16], s[32], s[48], s[64], s[80],
            s[96], s[112], s[128], s[144], s[160], s[176], s[192], s[208], s[224], s[240]);
        const __m128i whitespace = _mm_or_si128(_mm_cmpeq_epi8(v, _mm_set1_epi8(9)),
            _mm_or_si128(_mm_cmpeq_epi8(v, _mm_set1_epi8(10)), _mm_cmpeq_epi8(v, _mm_set1_epi8(13))));
        const __m128i ascii = _mm_and_si128(_mm_cmpgt_epi8(v, _mm_set1_epi8(31)),
            _mm_cmpgt_epi8(_mm_set1_epi8(127), v));
        const __m128i positive = _mm_cmpeq_epi8(_mm_and_si128(v, _mm_set1_epi8(-128)), zero);
        const __m128i control = _mm_andnot_si128(whitespace,
            _mm_and_si128(positive, _mm_cmpgt_epi8(_mm_set1_epi8(32), v)));
        p = _mm_add_epi64(p, _mm_sad_epu8(_mm_or_si128(ascii, whitespace), zero));
        c = _mm_add_epi64(c, _mm_sad_epu8(control, zero));
        h = _mm_add_epi64(h, _mm_sad_epu8(_mm_cmpgt_epi8(zero, v), zero));
    }
    *printable = ((uint64_t)_mm_cvtsi128_si64(p) + (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(p,8))) / 255u;
    *controls = ((uint64_t)_mm_cvtsi128_si64(c) + (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(c,8))) / 255u;
    *high = ((uint64_t)_mm_cvtsi128_si64(h) + (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(h,8))) / 255u;
    for (; i < n; i += 16u) {
        const uint8_t v = src[i];
        *printable += (v >= 32u && v <= 126u) || v == 9u || v == 10u || v == 13u;
        *controls += v < 32u && v != 9u && v != 10u && v != 13u;
        *high += v >= 128u;
    }
}
#endif

static int minlz_fastest_l1_cheap_binary_probe(const uint8_t *src, size_t src_len) {
    size_t n = src_len < 2048u ? src_len : 2048u;
    size_t samples = 0;
    size_t printable = 0;
    size_t controls = 0;
    size_t high = 0;
#if !(MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32))
    size_t i;
#endif
    if (src_len < (256u << 10) || n < 1024u) {
        return 0;
    }
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    samples = (n + 15u) / 16u;
    minlz_l1_sample_counts(src, n / 2u, &printable, &controls, &high);
    if (controls * 100u > samples * 10u || high * 100u > samples * 24u) {
        return 1;
    }
    {
        size_t p, c, h;
        minlz_l1_sample_counts(src + n / 2u, n / 2u, &p, &c, &h);
        printable += p;
        controls += c;
        high += h;
    }
#else
    for (i = 0; i < n; i += 16u) {
        uint8_t c = src[i];
        samples++;
        high += (c >= 128u);
        controls += (c < 32u && c != '\n' && c != '\r' && c != '\t');
        printable += ((c >= 32u && c <= 126u) || c == '\n' || c == '\r' || c == '\t');
    }
#endif
    return printable * 100u < samples * 58u ||
           controls * 100u > samples * 10u ||
           high * 100u > samples * 24u;
}

static int minlz_fastest_l1_likely_incompressible(const uint8_t *src, size_t src_len) {
    uint16_t table[1024];
    size_t n = src_len < (8u << 10) ? src_len : (8u << 10);
    size_t samples = 0;
    size_t printable = 0;
    size_t controls = 0;
    size_t high = 0;
    size_t i;
    unsigned hits = 0;
    if (src_len < (256u << 10)) {
        return 0;
    }
    if (n < 4096u) {
        return 0;
    }
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    samples = (n + 15u) / 16u;
    minlz_l1_sample_counts(src, n / 2u, &printable, &controls, &high);
    /* These counts only increase. Once either limit is exceeded,
     * the remaining sample cannot make the block look like text. */
    if (controls * 100u <= samples * 8u && high * 100u < samples * 20u) {
        size_t p, c, h;
        minlz_l1_sample_counts(src + n / 2u, n / 2u, &p, &c, &h);
        printable += p;
        controls += c;
        high += h;
    }
#else
    for (i = 0; i < n; i += 16u) {
        uint8_t c = src[i];
        samples++;
        high += (c >= 128u);
        controls += (c < 32u && c != '\n' && c != '\r' && c != '\t');
        printable += ((c >= 32u && c <= 126u) || c == '\n' || c == '\r' || c == '\t');
    }
#endif
    if (printable * 100u >= samples * 70u &&
        controls * 100u <= samples * 8u &&
        high * 100u < samples * 20u) {
        return 0;
    }
    memset(table, 0, sizeof(table));
    for (i = 1; i + 4 <= n; i += 8u) {
        uint32_t v = minlz_load32(src + i);
        uint32_t h = minlz_hash4(v, 10);
        uint16_t prev = table[h];
        table[h] = (uint16_t)(i + 1u);
        if (prev != 0 && v == minlz_load32(src + (size_t)prev - 1u)) {
            hits++;
            if (hits >= 2u) {
                return 0;
            }
        }
    }
    return 1;
}

static int minlz_balanced_tiny_lacks_long_match(const uint8_t *src, size_t src_len) {
    uint16_t table[1024];
    size_t i;
    if (src_len > 1024u || src_len < 8u) {
        return 0;
    }
    memset(table, 0, sizeof(table));
    for (i = 0; i + 8u <= src_len; i++) {
        uint64_t v = minlz_load64(src + i);
        uint32_t h = minlz_hash8(v, 10);
        uint16_t prev = table[h];
        table[h] = (uint16_t)(i + 1u);
        if (prev != 0 && v == minlz_load64(src + (size_t)prev - 1u)) {
            return 0;
        }
    }
    return 1;
}

static MINLZ_ALWAYS_INLINE void minlz_copy64_uncompressed_encode(uint8_t *dst,
                                                                  const uint8_t *src) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    __asm__ volatile(
        "movdqu 0(%[src]), %%xmm0\n\t"
        "movdqu 16(%[src]), %%xmm1\n\t"
        "movdqu 32(%[src]), %%xmm2\n\t"
        "movdqu 48(%[src]), %%xmm3\n\t"
        "movdqu %%xmm0, 0(%[dst])\n\t"
        "movdqu %%xmm1, 16(%[dst])\n\t"
        "movdqu %%xmm2, 32(%[dst])\n\t"
        "movdqu %%xmm3, 48(%[dst])"
        :
        : [dst] "r"(dst), [src] "r"(src)
        : "xmm0", "xmm1", "xmm2", "xmm3", "memory");
#else
    minlz_store64(dst, minlz_load64(src));
    minlz_store64(dst + 8, minlz_load64(src + 8));
    minlz_store64(dst + 16, minlz_load64(src + 16));
    minlz_store64(dst + 24, minlz_load64(src + 24));
    minlz_store64(dst + 32, minlz_load64(src + 32));
    minlz_store64(dst + 40, minlz_load64(src + 40));
    minlz_store64(dst + 48, minlz_load64(src + 48));
    minlz_store64(dst + 56, minlz_load64(src + 56));
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_copy_65to256_uncompressed_encode(uint8_t *dst,
                                                                       const uint8_t *src,
                                                                       size_t len) {
    minlz_copy64_uncompressed_encode(dst, src);
    if (len <= 128u) {
        minlz_copy64_uncompressed_encode(dst + len - 64u, src + len - 64u);
    } else if (len <= 192u) {
        minlz_copy64_uncompressed_encode(dst + 64u, src + 64u);
        minlz_copy64_uncompressed_encode(dst + len - 64u, src + len - 64u);
    } else {
        minlz_copy64_uncompressed_encode(dst + 64u, src + 64u);
        minlz_copy64_uncompressed_encode(dst + 128u, src + 128u);
        minlz_copy64_uncompressed_encode(dst + len - 64u, src + len - 64u);
    }
}

static MINLZ_ALWAYS_INLINE void minlz_copy_257to2048_uncompressed_encode(uint8_t *dst,
                                                                         const uint8_t *src,
                                                                         size_t len) {
    while (len > 256u) {
        minlz_copy_65to256_uncompressed_encode(dst, src, 256u);
        dst += 256u;
        src += 256u;
        len -= 256u;
    }
    if (len > 64u) {
        minlz_copy_65to256_uncompressed_encode(dst, src, len);
    } else if (len != 0u) {
        if (len > 16u) {
            minlz_copy_17to64_asm(dst, src, len);
        } else if (len >= 8u) {
            minlz_store64(dst, minlz_load64(src));
            minlz_store64(dst + len - 8u, minlz_load64(src + len - 8u));
        } else if (len >= 4u) {
            minlz_store32(dst, minlz_load32(src));
            minlz_store32(dst + len - 4u, minlz_load32(src + len - 4u));
        } else {
            dst[0] = src[0];
            if (len > 1u) {
                dst[1] = src[1];
                if (len > 2u) {
                    dst[2] = src[2];
                }
            }
        }
    }
}

static MINLZ_ALWAYS_INLINE void minlz_copy_uncompressed_encode(uint8_t *dst, const uint8_t *src,
                                                               size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (len >= MINLZ_ENCODE_UNCOMPRESSED_REPMOVSB_MIN && len <= (64u << 10) &&
        minlz_backend_has_asm_amd64()) {
        __asm__ volatile("rep movsb"
                         : "+D"(dst), "+S"(src), "+c"(len)
                         :
                         : "memory");
        return;
    }
#endif
#if MINLZ_ENABLE_RAWCOPY_SSE42_MOVSQ
    if (len >= (8u << 10) && len <= (16u << 10) &&
        minlz_copy_uncompressed_sse42_movsq(dst, src, len)) {
        return;
    }
#endif
#if MINLZ_ENABLE_RAWCOPY_SSE42_XMM
    if (len >= (8u << 10) && len <= (16u << 10) &&
        minlz_copy_uncompressed_sse42_xmm(dst, src, len)) {
        return;
    }
#endif
    if (len >= (8u << 10) && len <= (64u << 10)) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + 6u, minlz_load64(src + 6u));
        memcpy(dst + 14u, src + 14u, len - 14u);
        return;
    }
    if (len > 1024u) {
        memcpy(dst, src, len);
    } else if (len > 256u) {
        memcpy(dst, src, len);
    } else if (len > 128u) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
        __asm__ volatile(
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu 64(%[src]), %%xmm4\n\t"
            "movdqu 80(%[src]), %%xmm5\n\t"
            "movdqu 96(%[src]), %%xmm6\n\t"
            "movdqu 112(%[src]), %%xmm7\n\t"
            "movdqu -128(%[src],%[len]), %%xmm8\n\t"
            "movdqu -112(%[src],%[len]), %%xmm9\n\t"
            "movdqu -96(%[src],%[len]), %%xmm10\n\t"
            "movdqu -80(%[src],%[len]), %%xmm11\n\t"
            "movdqu -64(%[src],%[len]), %%xmm12\n\t"
            "movdqu -48(%[src],%[len]), %%xmm13\n\t"
            "movdqu -32(%[src],%[len]), %%xmm14\n\t"
            "movdqu -16(%[src],%[len]), %%xmm15\n\t"
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "movdqu %%xmm4, 64(%[dst])\n\t"
            "movdqu %%xmm5, 80(%[dst])\n\t"
            "movdqu %%xmm6, 96(%[dst])\n\t"
            "movdqu %%xmm7, 112(%[dst])\n\t"
            "movdqu %%xmm8, -128(%[dst],%[len])\n\t"
            "movdqu %%xmm9, -112(%[dst],%[len])\n\t"
            "movdqu %%xmm10, -96(%[dst],%[len])\n\t"
            "movdqu %%xmm11, -80(%[dst],%[len])\n\t"
            "movdqu %%xmm12, -64(%[dst],%[len])\n\t"
            "movdqu %%xmm13, -48(%[dst],%[len])\n\t"
            "movdqu %%xmm14, -32(%[dst],%[len])\n\t"
            "movdqu %%xmm15, -16(%[dst],%[len])"
            :
            : [dst] "r"(dst), [src] "r"(src), [len] "r"(len)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",
              "xmm15", "memory");
#else
        memcpy(dst, src, len);
#endif
    } else if (len > 64u) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
        __asm__ volatile(
            "movdqu 0(%[src]), %%xmm0\n\t"
            "movdqu 16(%[src]), %%xmm1\n\t"
            "movdqu 32(%[src]), %%xmm2\n\t"
            "movdqu 48(%[src]), %%xmm3\n\t"
            "movdqu -64(%[src],%[len]), %%xmm4\n\t"
            "movdqu -48(%[src],%[len]), %%xmm5\n\t"
            "movdqu -32(%[src],%[len]), %%xmm6\n\t"
            "movdqu -16(%[src],%[len]), %%xmm7\n\t"
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "movdqu %%xmm4, -64(%[dst],%[len])\n\t"
            "movdqu %%xmm5, -48(%[dst],%[len])\n\t"
            "movdqu %%xmm6, -32(%[dst],%[len])\n\t"
            "movdqu %%xmm7, -16(%[dst],%[len])"
            :
            : [dst] "r"(dst), [src] "r"(src), [len] "r"(len)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "memory");
#else
        memcpy(dst, src, len);
#endif
    } else if (len > 16u) {
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
}

static MINLZ_ALWAYS_INLINE size_t minlz_encode_uncompressed(uint8_t *dst, size_t dst_cap,
                                                            const uint8_t *src,
                                                            size_t src_len) {
    if (src_len == 0) {
        if (dst_cap < 1) {
            return 0;
        }
        dst[0] = 0;
        return 1;
    }
    if (dst_cap < src_len + 2) {
        return 0;
    }
    minlz_store16(dst, 0);
    minlz_copy_uncompressed_encode(dst + 2, src, src_len);
    return src_len + 2;
}

#if MINLZ_ENCODE_8K16K_SSE42_BULK
static MINLZ_ALWAYS_INLINE int minlz_copy_uncompressed_sse42_bulk_encode(
    uint8_t *dst, const uint8_t *src, size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uintptr_t d = (uintptr_t)dst;
    uintptr_t s = (uintptr_t)src;
    size_t blocks;
    size_t tail;
    if (!minlz_backend_has_sse42() || len < 256u ||
        (d < s + len && s < d + len)) {
        return 0;
    }
    blocks = len >> 7;
    tail = len & 127u;
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
            "movdqu %%xmm0, 0(%[dst])\n\t"
            "movdqu %%xmm1, 16(%[dst])\n\t"
            "movdqu %%xmm2, 32(%[dst])\n\t"
            "movdqu %%xmm3, 48(%[dst])\n\t"
            "movdqu %%xmm4, 64(%[dst])\n\t"
            "movdqu %%xmm5, 80(%[dst])\n\t"
            "movdqu %%xmm6, 96(%[dst])\n\t"
            "movdqu %%xmm7, 112(%[dst])\n\t"
            "addq $128, %[src]\n\t"
            "addq $128, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "cc", "memory");
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
#endif

static MINLZ_ALWAYS_INLINE size_t minlz_uncompressed_encoded_len(size_t src_len) {
    return src_len == 0 ? 1u : src_len + 2u;
}

static MINLZ_ALWAYS_INLINE minlz_status minlz_encode_uncompressed_8k16k(
    uint8_t *dst, size_t dst_cap, size_t *dst_len, const uint8_t *src, size_t src_len) {
    if (dst_cap < src_len + 2u) {
        *dst_len = src_len + 2u;
        return MINLZ_DST_TOO_SMALL;
    }
    dst[0] = 0;
    dst[1] = 0;
    if (src_len == 10000u) {
        if (!minlz_backend_has_avx2()) {
            memmove(dst + 2u, src, 10000u);
            *dst_len = 10002u;
            return MINLZ_OK;
        }
#if MINLZ_ENCODE_10000_AVX2_FWD256
        if (minlz_copy_uncompressed_avx2_go_fwd256(dst + 2u, src, 10000u)) {
            *dst_len = 10002u;
            return MINLZ_OK;
        }
#endif
#if MINLZ_ENCODE_10000_AVX2_MOD2
        if (minlz_copy_uncompressed_10000_avx2_mod2(dst + 2u, src)) {
            *dst_len = 10002u;
            return MINLZ_OK;
        }
#endif
#if MINLZ_ENCODE_10000_AVX2_CONST && MINLZ_ENCODE_8K16K_AVX2_GO_FWD
        if (minlz_copy_uncompressed_avx2_go_fwd_unchecked(dst + 2u, src, 10000u)) {
            *dst_len = 10002u;
            return MINLZ_OK;
        }
#endif
    }
#if MINLZ_ENCODE_8K16K_AVX2_GO_FWD
    if (minlz_copy_uncompressed_avx2_go_fwd(dst + 2u, src, src_len)) {
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
#endif
#if MINLZ_ENCODE_8K16K_AVX2_ALIGNED_STORE
    {
        uintptr_t d = (uintptr_t)(dst + 2u);
        uintptr_t s = (uintptr_t)src;
        if (!(d < s + src_len && s < d + src_len) &&
            minlz_copy_uncompressed_avx2_aligned_store(dst + 2u, src, src_len)) {
            *dst_len = src_len + 2u;
            return MINLZ_OK;
        }
    }
#endif
#if MINLZ_ENCODE_8K16K_SSE42_BULK
    if (minlz_copy_uncompressed_sse42_bulk_encode(dst + 2u, src, src_len)) {
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
#endif
#if MINLZ_ENCODE_8K16K_DIRECT_MEMMOVE
    memmove(dst + 2u, src, src_len);
    *dst_len = src_len + 2u;
    return MINLZ_OK;
#else
#if MINLZ_ENABLE_RAWCOPY_SSE42_QWORD
    if (minlz_copy_uncompressed_sse42_qword(dst + 2u, src, src_len)) {
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
#endif
#if MINLZ_ENABLE_RAWCOPY_SSE42_XMM_ALIGNED_STORE
    if (minlz_copy_uncompressed_sse42_xmm_aligned_store(dst + 2u, src, src_len, 1)) {
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
#endif
    minlz_store64(dst + 2u, minlz_load64(src));
    minlz_store64(dst + 8u, minlz_load64(src + 6u));
    memcpy(dst + 16u, src + 14u, src_len - 14u);
    *dst_len = src_len + 2u;
    return MINLZ_OK;
#endif
}

static inline void minlz_copy_small_fast(uint8_t *dst, const uint8_t *src, size_t len) {
    if (len > 16) {
        minlz_copy_17to64_asm(dst, src, len);
    } else if (len >= 8) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + len - 8, minlz_load64(src + len - 8));
    } else if (len >= 4) {
        minlz_store32(dst, minlz_load32(src));
        minlz_store32(dst + len - 4, minlz_load32(src + len - 4));
    } else if (len != 0) {
        dst[0] = src[0];
        if (len > 1) {
            dst[1] = src[1];
            if (len > 2) {
                dst[2] = src[2];
            }
        }
    }
}

static inline void minlz_copy_literal_fast(uint8_t *dst, const uint8_t *src, size_t len) {
    if (len <= 64) {
        minlz_copy_small_fast(dst, src, len);
    } else {
        memcpy(dst, src, len);
    }
}

static inline size_t minlz_emit_literal_inline_fast(uint8_t *dst, const uint8_t *lit,
                                                    size_t lit_len) {
    size_t n;
    if (lit_len == 0) {
        return 0;
    }
    n = lit_len - 1;
    if (n < 29) {
        dst[0] = (uint8_t)(n << 3);
        minlz_copy_literal_fast(dst + 1, lit, lit_len);
        return lit_len + 1;
    }
    if (n < 29 + (1u << 8)) {
        dst[0] = (uint8_t)(29u << 3);
        dst[1] = (uint8_t)(n - 29);
        minlz_copy_literal_fast(dst + 2, lit, lit_len);
        return lit_len + 2;
    }
    if (n < 29 + (1u << 16)) {
        n -= 29;
        dst[0] = (uint8_t)(30u << 3);
        minlz_store16(dst + 1, (uint16_t)n);
        minlz_copy_literal_fast(dst + 3, lit, lit_len);
        return lit_len + 3;
    }
    n -= 29;
    dst[0] = (uint8_t)(31u << 3);
    dst[1] = (uint8_t)n;
    dst[2] = (uint8_t)(n >> 8);
    dst[3] = (uint8_t)(n >> 16);
    minlz_copy_literal_fast(dst + 4, lit, lit_len);
    return lit_len + 4;
}

static inline size_t minlz_emit_literal_fast(uint8_t *dst, const uint8_t *lit, size_t lit_len) {
    size_t n;
    if (lit_len == 0) {
        return 0;
    }
    n = lit_len - 1;
    if (n < 29) {
        dst[0] = (uint8_t)(n << 3);
        minlz_copy_literal_fast(dst + 1, lit, lit_len);
        return lit_len + 1;
    }
    if (n < 29 + (1u << 8)) {
        dst[0] = (uint8_t)(29u << 3);
        dst[1] = (uint8_t)(n - 29);
        minlz_copy_literal_fast(dst + 2, lit, lit_len);
        return lit_len + 2;
    }
    if (n < 29 + (1u << 16)) {
        n -= 29;
        dst[0] = (uint8_t)(30u << 3);
        minlz_store16(dst + 1, (uint16_t)n);
        minlz_copy_literal_fast(dst + 3, lit, lit_len);
        return lit_len + 3;
    }
    n -= 29;
    dst[0] = (uint8_t)(31u << 3);
    dst[1] = (uint8_t)n;
    dst[2] = (uint8_t)(n >> 8);
    dst[3] = (uint8_t)(n >> 16);
    minlz_copy_literal_fast(dst + 4, lit, lit_len);
    return lit_len + 4;
}

static inline size_t minlz_emit_repeat_fast(uint8_t *dst, size_t length) {
    if (length < 30) {
        dst[0] = (uint8_t)(((length - 1) << 3) | MINLZ_TAG_REPEAT);
        return 1;
    }
    length -= 30;
    if (length < 256) {
        dst[0] = (uint8_t)((29u << 3) | MINLZ_TAG_REPEAT);
        dst[1] = (uint8_t)length;
        return 2;
    }
    if (length < 65536) {
        dst[0] = (uint8_t)((30u << 3) | MINLZ_TAG_REPEAT);
        minlz_store16(dst + 1, (uint16_t)length);
        return 3;
    }
    dst[0] = (uint8_t)((31u << 3) | MINLZ_TAG_REPEAT);
    dst[1] = (uint8_t)length;
    dst[2] = (uint8_t)(length >> 8);
    dst[3] = (uint8_t)(length >> 16);
    return 4;
}

static inline size_t minlz_emit_copy2_fast(uint8_t *dst, size_t offset, size_t length) {
    offset -= MINLZ_MIN_COPY2_OFFSET;
    length -= 4;
    if (length <= 60) {
        dst[0] = (uint8_t)((length << 2) | MINLZ_TAG_COPY2);
        minlz_store16(dst + 1, (uint16_t)offset);
        return 3;
    }
    length -= 60;
    if (length < 256) {
        dst[0] = (uint8_t)((61u << 2) | MINLZ_TAG_COPY2);
        minlz_store16(dst + 1, (uint16_t)offset);
        dst[3] = (uint8_t)length;
        return 4;
    }
    if (length < 65536) {
        dst[0] = (uint8_t)((62u << 2) | MINLZ_TAG_COPY2);
        minlz_store16(dst + 1, (uint16_t)offset);
        minlz_store16(dst + 3, (uint16_t)length);
        return 5;
    }
    dst[0] = (uint8_t)((63u << 2) | MINLZ_TAG_COPY2);
    minlz_store16(dst + 1, (uint16_t)offset);
    dst[3] = (uint8_t)length;
    dst[4] = (uint8_t)(length >> 8);
    dst[5] = (uint8_t)(length >> 16);
    return 6;
}

static inline size_t minlz_emit_copy3_fast(uint8_t *dst, size_t offset,
                                           size_t length, size_t lit_len) {
    uint32_t encoded;
    length -= 4;
    offset -= MINLZ_MIN_COPY3_OFFSET;
    encoded = ((uint32_t)offset << 11) | MINLZ_TAG_COPY3 | ((uint32_t)lit_len << 3);
    if (length <= 60) {
        encoded |= (uint32_t)length << 5;
        minlz_store32(dst, encoded);
        return 4;
    }
    length -= 60;
    if (length < 256) {
        encoded |= 61u << 5;
        minlz_store32(dst, encoded);
        dst[4] = (uint8_t)length;
        return 5;
    }
    if (length < 65536) {
        encoded |= 62u << 5;
        minlz_store32(dst, encoded);
        minlz_store16(dst + 4, (uint16_t)length);
        return 6;
    }
    encoded |= 63u << 5;
    minlz_store32(dst, encoded);
    dst[4] = (uint8_t)length;
    dst[5] = (uint8_t)(length >> 8);
    dst[6] = (uint8_t)(length >> 16);
    return 7;
}

static inline size_t minlz_emit_copy_fast(uint8_t *dst, size_t offset, size_t length) {
    if (offset > MINLZ_MAX_COPY2_OFFSET) {
        return minlz_emit_copy3_fast(dst, offset, length, 0);
    }
    if (offset <= MINLZ_MAX_COPY1_OFFSET) {
        uint16_t enc;
        offset--;
        if (length < 19) {
            enc = (uint16_t)((offset << 6) | ((length - 4) << 2) | MINLZ_TAG_COPY1);
            minlz_store16(dst, enc);
            return 2;
        }
        if (length < 274) {
            enc = (uint16_t)((offset << 6) | (15u << 2) | MINLZ_TAG_COPY1);
            minlz_store16(dst, enc);
            dst[2] = (uint8_t)(length - 18);
            return 3;
        }
        enc = (uint16_t)((offset << 6) | (14u << 2) | MINLZ_TAG_COPY1);
        minlz_store16(dst, enc);
        return 2 + minlz_emit_repeat_fast(dst + 2, length - 18);
    }
    return minlz_emit_copy2_fast(dst, offset, length);
}

static MINLZ_ALWAYS_INLINE size_t minlz_emit_periodic_copy_tail_fast(uint8_t *dst,
                                                                     size_t offset,
                                                                     size_t length) {
    if (offset > MINLZ_MAX_COPY1_OFFSET && offset <= MINLZ_MAX_COPY2_OFFSET &&
        length >= 65536u + 64u) {
        size_t raw_len = length - 64u;
        uint64_t encoded = (uint64_t)((63u << 2) | MINLZ_TAG_COPY2) |
            ((uint64_t)(uint16_t)(offset - MINLZ_MIN_COPY2_OFFSET) << 8) |
            ((uint64_t)(raw_len & 0x00ffffffu) << 24);
        minlz_store64(dst, encoded);
        return 6u;
    }
    return minlz_emit_copy_fast(dst, offset, length);
}

static inline size_t minlz_emit_copy_lits2_fast(uint8_t *dst, const uint8_t *lits,
                                                size_t lit_len, size_t offset,
                                                size_t length) {
    size_t raw_len = length - 4;
    offset -= MINLZ_MIN_COPY2_OFFSET;
    if (raw_len > 7) {
        dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (7u << 5) | ((lit_len - 1) << 3));
        minlz_store16(dst + 1, (uint16_t)offset);
        minlz_copy_small_fast(dst + 3, lits, lit_len);
        return 3 + lit_len + minlz_emit_repeat_fast(dst + 3 + lit_len, raw_len - 7);
    }
    dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (raw_len << 5) | ((lit_len - 1) << 3));
    minlz_store16(dst + 1, (uint16_t)offset);
    minlz_copy_small_fast(dst + 3, lits, lit_len);
    return 3 + lit_len;
}

static inline size_t minlz_emit_copy_lits3_fast(uint8_t *dst, const uint8_t *lits,
                                                size_t lit_len, size_t offset,
                                                size_t length) {
    size_t n = minlz_emit_copy3_fast(dst, offset, length, lit_len);
    minlz_copy_small_fast(dst + n, lits, lit_len);
    return n + lit_len;
}

static MINLZ_ALWAYS_INLINE size_t minlz_emit_copy_lits2_overcopy4_fast(
    uint8_t *dst, const uint8_t *lits, size_t lit_len, size_t offset, size_t length) {
    size_t raw_len = length - 4;
    offset -= MINLZ_MIN_COPY2_OFFSET;
    if (raw_len > 7) {
        dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (7u << 5) | ((lit_len - 1) << 3));
        minlz_store16(dst + 1, (uint16_t)offset);
        minlz_store32(dst + 3, minlz_load32(lits));
        return 3 + lit_len + minlz_emit_repeat_fast(dst + 3 + lit_len, raw_len - 7);
    }
    dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (raw_len << 5) | ((lit_len - 1) << 3));
    minlz_store16(dst + 1, (uint16_t)offset);
    minlz_store32(dst + 3, minlz_load32(lits));
    return 3 + lit_len;
}

static MINLZ_ALWAYS_INLINE size_t minlz_emit_copy_lits3_overcopy4_fast(
    uint8_t *dst, const uint8_t *lits, size_t lit_len, size_t offset, size_t length) {
    size_t n = minlz_emit_copy3_fast(dst, offset, length, lit_len);
    minlz_store32(dst + n, minlz_load32(lits));
    return n + lit_len;
}

static inline size_t minlz_emit_copy_lits2_inline_fast(uint8_t *dst, const uint8_t *lits,
                                                       size_t lit_len, size_t offset,
                                                       size_t length) {
    size_t raw_len = length - 4;
    offset -= MINLZ_MIN_COPY2_OFFSET;
    if (raw_len > 7) {
        dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (7u << 5) | ((lit_len - 1) << 3));
        minlz_store16(dst + 1, (uint16_t)offset);
        minlz_copy_small_fast(dst + 3, lits, lit_len);
        return 3 + lit_len + minlz_emit_repeat_fast(dst + 3 + lit_len, raw_len - 7);
    }
    dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (raw_len << 5) | ((lit_len - 1) << 3));
    minlz_store16(dst + 1, (uint16_t)offset);
    minlz_copy_small_fast(dst + 3, lits, lit_len);
    return 3 + lit_len;
}

typedef enum minlz_encode_bucket {
    MINLZ_BUCKET_NONE = 0,
    MINLZ_BUCKET_1K,
    MINLZ_BUCKET_4K,
    MINLZ_BUCKET_16K,
    MINLZ_BUCKET_64K,
    MINLZ_BUCKET_512K,
    MINLZ_BUCKET_2M,
    MINLZ_BUCKET_8M
} minlz_encode_bucket;

static inline minlz_encode_bucket minlz_fast_go_bucket(size_t src_len, size_t *scratch) {
    if (src_len > (2u << 20)) {
        *scratch = 65536u;
        return MINLZ_BUCKET_8M;
    }
    if (src_len > (512u << 10)) {
        *scratch = 32768u;
        return MINLZ_BUCKET_2M;
    }
    if (src_len > (64u << 10)) {
        *scratch = 32768u;
        return MINLZ_BUCKET_512K;
    }
    if (src_len > (16u << 10)) {
        *scratch = 8192u;
        return MINLZ_BUCKET_64K;
    }
    if (src_len > (4u << 10)) {
        *scratch = 4096u;
        return MINLZ_BUCKET_16K;
    }
    if (src_len > (1u << 10)) {
        *scratch = 2048u;
        return MINLZ_BUCKET_4K;
    }
    if (src_len > 32u) {
        *scratch = 1024u;
        return MINLZ_BUCKET_1K;
    }
    *scratch = 0;
    return MINLZ_BUCKET_NONE;
}

static inline minlz_encode_bucket minlz_better_go_bucket(size_t src_len, size_t *scratch) {
    if (src_len > (2u << 20)) {
        *scratch = 589824u;
        return MINLZ_BUCKET_8M;
    }
    if (src_len > (512u << 10)) {
        *scratch = 589824u;
        return MINLZ_BUCKET_2M;
    }
    if (src_len > (64u << 10)) {
        *scratch = 294912u;
        return MINLZ_BUCKET_512K;
    }
    if (src_len > (16u << 10)) {
        *scratch = 73728u;
        return MINLZ_BUCKET_64K;
    }
    if (src_len > (4u << 10)) {
        *scratch = 36864u;
        return MINLZ_BUCKET_16K;
    }
    if (src_len > (1u << 10)) {
        *scratch = 10240u;
        return MINLZ_BUCKET_4K;
    }
    if (src_len > MINLZ_MIN_NON_LITERAL_BLOCK_SIZE) {
        *scratch = 4608u;
        return MINLZ_BUCKET_1K;
    }
    *scratch = 0;
    return MINLZ_BUCKET_NONE;
}

#if MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM || MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM || MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
static MINLZ_ALWAYS_INLINE size_t minlz_encode_block_16k_inline_asm(uint8_t *dst, const uint8_t *src,
                                                       size_t src_len, uint8_t *tmp) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    size_t ret;
    __asm__ volatile(
        "pushq %%rbx\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "subq $64, %%rsp\n"
        "movq %[dst], 24(%%rsp)\n"
        "movq %[src], 32(%%rsp)\n"
        "movq %[src_len], 40(%%rsp)\n"
        "movq %[tmp], 48(%%rsp)\n"
        "movq 48(%%rsp), %%rdi\n"
        "xorl %%eax, %%eax\n"
        "movl $128, %%ecx\n"
        "pxor %%xmm0, %%xmm0\n"
        ".Lminlz_clear_16k_%=:\n"
        "movdqu %%xmm0, (%%rdi)\n"
        "movdqu %%xmm0, 16(%%rdi)\n"
        "movdqu %%xmm0, 32(%%rdi)\n"
        "movdqu %%xmm0, 48(%%rdi)\n"
        "addq $64, %%rdi\n"
        "decl %%ecx\n"
        "jnz .Lminlz_clear_16k_%=\n"
        "movq 48(%%rsp), %%rax\n"
        "movq 24(%%rsp), %%rcx\n"
        "movl $0x00000000, 12(%%rsp)\n"
        "movq 40(%%rsp), %%rdx\n"
        "leaq -17(%%rdx), %%rbx\n"
        "leaq -17(%%rdx), %%rsi\n"
        "movl %%esi, 8(%%rsp)\n"
        "shrq $0x05, %%rdx\n"
        "subl %%edx, %%ebx\n"
        "leaq (%%rcx,%%rbx,1), %%rbx\n"
        "movq %%rbx, (%%rsp)\n"
        "movl $0x00000001, %%edx\n"
        "movl %%edx, 16(%%rsp)\n"
        "movq 32(%%rsp), %%rbx\n"
        ".Lminlz_block16k_%=_search_loop_encodeBlockAsm16K:\n"
        "movl %%edx, %%esi\n"
        "subl 12(%%rsp), %%esi\n"
        "shrl $0x05, %%esi\n"
        "leal 3(%%rdx,%%rsi,1), %%esi\n"
        "cmpl 8(%%rsp), %%esi\n"
        "jae .Lminlz_block16k_%=_emit_remainder_encodeBlockAsm16K\n"
        "movq (%%rbx,%%rdx,1), %%rdi\n"
        "movl %%esi, 20(%%rsp)\n"
        "movq $0xcf1bbcdcbb000000, %%r9\n"
        "movq %%rdi, %%r10\n"
        "imulq %%r9, %%r10\n"
        "shrq $0x34, %%r10\n"
        "movq %%rdi, %%r11\n"
        "shrq $0x08, %%r11\n"
        "imulq %%r9, %%r11\n"
        "shrq $0x34, %%r11\n"
        "movzwl (%%rax,%%r10,2), %%esi\n"
        "movzwl (%%rax,%%r11,2), %%r8d\n"
        "movw %%dx, (%%rax,%%r10,2)\n"
        "leal 1(%%rdx), %%r10d\n"
        "movw %%r10w, (%%rax,%%r11,2)\n"
        "movq %%rdi, %%r10\n"
        "shrq $0x10, %%r10\n"
        "imulq %%r9, %%r10\n"
        "shrq $0x34, %%r10\n"
        "movl %%edx, %%r9d\n"
        "subl 16(%%rsp), %%r9d\n"
        "movl 1(%%rbx,%%r9,1), %%r11d\n"
        "movq %%rdi, %%r9\n"
        "shrq $0x08, %%r9\n"
        "cmpl %%r11d, %%r9d\n"
        "jne .Lminlz_block16k_%=_no_repeat_found_encodeBlockAsm16K\n"
        "leal 1(%%rdx), %%edi\n"
        "movl 12(%%rsp), %%esi\n"
        "movl %%edi, %%r8d\n"
        "subl 16(%%rsp), %%r8d\n"
        "jz .Lminlz_block16k_%=_repeat_extend_back_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_extend_back_loop_encodeBlockAsm16K:\n"
        "cmpl %%esi, %%edi\n"
        "jbe .Lminlz_block16k_%=_repeat_extend_back_end_encodeBlockAsm16K\n"
        "movb -1(%%rbx,%%r8,1), %%r9b\n"
        "movb -1(%%rbx,%%rdi,1), %%r10b\n"
        "cmpb %%r10b, %%r9b\n"
        "jne .Lminlz_block16k_%=_repeat_extend_back_end_encodeBlockAsm16K\n"
        "leal -1(%%rdi), %%edi\n"
        "decl %%r8d\n"
        "jnz .Lminlz_block16k_%=_repeat_extend_back_loop_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_extend_back_end_encodeBlockAsm16K:\n"
        "movl %%edi, %%esi\n"
        "movl 12(%%rsp), %%r8d\n"
        "subl %%r8d, %%esi\n"
        "leaq 3(%%rcx,%%rsi,1), %%r9\n"
        "cmpq (%%rsp), %%r9\n"
        "jb .Lminlz_block16k_%=_dst_size_check_ok_1\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_dst_size_check_ok_1:\n"
        "leaq (%%rbx,%%r8,1), %%r8\n"
        "leal -1(%%rsi), %%r9d\n"
        "cmpl $0x1d, %%r9d\n"
        "jb .Lminlz_block16k_%=_one_byte_repeat_emit_lits_encodeBlockAsm16K\n"
        "subl $0x1d, %%r9d\n"
        "cmpl $0x00000100, %%r9d\n"
        "jb .Lminlz_block16k_%=_two_bytes_repeat_emit_lits_encodeBlockAsm16K\n"
        "jb .Lminlz_block16k_%=_three_bytes_repeat_emit_lits_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_three_bytes_repeat_emit_lits_encodeBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_block16k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_two_bytes_repeat_emit_lits_encodeBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%r9b, 1(%%rcx)\n"
        "addl $0x1d, %%r9d\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%r9d\n"
        "jb .Lminlz_block16k_%=_memmove_midrepeat_emit_lits_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_one_byte_repeat_emit_lits_encodeBlockAsm16K:\n"
        "shlb $0x03, %%r9b\n"
        "movb %%r9b, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x10, %%rsi\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_block16k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm16K_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm16K_memmove_move_8through16:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_repeat_emit_lits_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_repeat_emit_lits_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_block16k_%=_memmove_end_copy_repeat_emit_lits_encodeBlockAsm16K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block16k_%=_repeat_emit_lits_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_memmove_midrepeat_emit_lits_encodeBlockAsm16K:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_block16k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_mid_end_copy_repeat_emit_lits_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_block16k_%=_memmove_mid_end_copy_repeat_emit_lits_encodeBlockAsm16K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block16k_%=_repeat_emit_lits_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm16K:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movq %%rsi, %%r11\n"
        "shrq $0x05, %%r11\n"
        "movq %%rcx, %%r10\n"
        "andl $0x0000001f, %%r10d\n"
        "movq $0x00000040, %%r12\n"
        "subq %%r10, %%r12\n"
        "decq %%r11\n"
        "ja .Lminlz_block16k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r12,1), %%r10\n"
        "leaq -32(%%rcx,%%r12,1), %%r13\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%r10), %%xmm4\n"
        "movdqu 16(%%r10), %%xmm5\n"
        "movdqa %%xmm4, (%%r13)\n"
        "movdqa %%xmm5, 16(%%r13)\n"
        "addq $0x20, %%r13\n"
        "addq $0x20, %%r10\n"
        "addq $0x20, %%r12\n"
        "decq %%r11\n"
        "jna .Lminlz_block16k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r12,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r12,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r12,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r12,1)\n"
        "addq $0x20, %%r12\n"
        "cmpq %%r12, %%rsi\n"
        "jae .Lminlz_block16k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        "movq %%r9, %%rcx\n"
        ".Lminlz_block16k_%=_repeat_emit_lits_end_encodeBlockAsm16K:\n"
        "addl $0x05, %%edx\n"
        "movl %%edx, %%esi\n"
        "subl 16(%%rsp), %%esi\n"
        "movq 40(%%rsp), %%r8\n"
        "subl %%edx, %%r8d\n"
        "leaq (%%rbx,%%rdx,1), %%r9\n"
        "leaq (%%rbx,%%rsi,1), %%rsi\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_block16k_%=_matchlen_loop_16_entry_repeat_extend_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_loopback_16_repeat_extend_encodeBlockAsm16K:\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "movq 8(%%r9,%%r11,1), %%r12\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_8_repeat_extend_encodeBlockAsm16K\n"
        "xorq 8(%%rsi,%%r11,1), %%r12\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_16repeat_extend_encodeBlockAsm16K\n"
        "leal -16(%%r8), %%r8d\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_block16k_%=_matchlen_loop_16_entry_repeat_extend_encodeBlockAsm16K:\n"
        "cmpl $0x10, %%r8d\n"
        "jae .Lminlz_block16k_%=_matchlen_loopback_16_repeat_extend_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_matchlen_match8_repeat_extend_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_bsf_16repeat_extend_encodeBlockAsm16K:\n"
        "tzcntq %%r12, %%r12\n"
        "sarq $0x03, %%r12\n"
        "leal 8(%%r11,%%r12,1), %%r11d\n"
        "jmp .Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match8_repeat_extend_encodeBlockAsm16K:\n"
        "cmpl $0x08, %%r8d\n"
        "jb .Lminlz_block16k_%=_matchlen_match4_repeat_extend_encodeBlockAsm16K\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_8_repeat_extend_encodeBlockAsm16K\n"
        "leal -8(%%r8), %%r8d\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_block16k_%=_matchlen_match4_repeat_extend_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_bsf_8_repeat_extend_encodeBlockAsm16K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "addl %%r10d, %%r11d\n"
        "jmp .Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match4_repeat_extend_encodeBlockAsm16K:\n"
        "cmpl $0x04, %%r8d\n"
        "jb .Lminlz_block16k_%=_matchlen_match2_repeat_extend_encodeBlockAsm16K\n"
        "movl (%%r9,%%r11,1), %%r10d\n"
        "cmpl %%r10d, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_matchlen_match2_repeat_extend_encodeBlockAsm16K\n"
        "leal -4(%%r8), %%r8d\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_block16k_%=_matchlen_match2_repeat_extend_encodeBlockAsm16K:\n"
        "cmpl $0x01, %%r8d\n"
        "je .Lminlz_block16k_%=_matchlen_match1_repeat_extend_encodeBlockAsm16K\n"
        "jb .Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K\n"
        "movw (%%r9,%%r11,1), %%r10w\n"
        "cmpw %%r10w, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_matchlen_match1_repeat_extend_encodeBlockAsm16K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%r8d\n"
        "jz .Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match1_repeat_extend_encodeBlockAsm16K:\n"
        "movb (%%r9,%%r11,1), %%r10b\n"
        "cmpb %%r10b, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_repeat_extend_forward_end_encodeBlockAsm16K:\n"
        "addl %%r11d, %%edx\n"
        "movl %%edx, %%esi\n"
        "subl %%edi, %%esi\n"
        "movl 16(%%rsp), %%edi\n"
        "leal -1(%%rsi), %%edi\n"
        "cmpl $0x1d, %%esi\n"
        "jbe .Lminlz_block16k_%=_repeat_one_match_repeat_encodeBlockAsm16K\n"
        "leal -30(%%rsi), %%edi\n"
        "cmpl $0x0000011e, %%esi\n"
        "jb .Lminlz_block16k_%=_repeat_two_match_repeat_encodeBlockAsm16K\n"
        "cmpl $0x0001001e, %%esi\n"
        "jb .Lminlz_block16k_%=_repeat_three_match_repeat_encodeBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%edi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block16k_%=_repeat_end_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_three_match_repeat_encodeBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%di, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block16k_%=_repeat_end_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_two_match_repeat_encodeBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%dil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block16k_%=_repeat_end_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_one_match_repeat_encodeBlockAsm16K:\n"
        "xorl %%edi, %%edi\n"
        "leal -4(%%rdi,%%rsi,8), %%edi\n"
        "movb %%dil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        ".Lminlz_block16k_%=_repeat_end_emit_encodeBlockAsm16K:\n"
        "movl %%edx, 12(%%rsp)\n"
        "jmp .Lminlz_block16k_%=_search_loop_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_no_repeat_found_encodeBlockAsm16K:\n"
        "cmpl %%edi, (%%rbx,%%rsi,1)\n"
        "je .Lminlz_block16k_%=_candidate_match_encodeBlockAsm16K\n"
        "shrq $0x08, %%rdi\n"
        "movzwl (%%rax,%%r10,2), %%esi\n"
        "leal 2(%%rdx), %%r9d\n"
        "cmpl %%edi, (%%rbx,%%r8,1)\n"
        "je .Lminlz_block16k_%=_candidate2_match_encodeBlockAsm16K\n"
        "movw %%r9w, (%%rax,%%r10,2)\n"
        "shrq $0x08, %%rdi\n"
        "cmpl %%edi, (%%rbx,%%rsi,1)\n"
        "je .Lminlz_block16k_%=_candidate3_match_encodeBlockAsm16K\n"
        "movl 20(%%rsp), %%edx\n"
        "jmp .Lminlz_block16k_%=_search_loop_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_candidate3_match_encodeBlockAsm16K:\n"
        "addl $0x02, %%edx\n"
        "jmp .Lminlz_block16k_%=_candidate_match_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_candidate2_match_encodeBlockAsm16K:\n"
        "movw %%r9w, (%%rax,%%r10,2)\n"
        "incl %%edx\n"
        "movl %%r8d, %%esi\n"
        ".Lminlz_block16k_%=_candidate_match_encodeBlockAsm16K:\n"
        "movl 12(%%rsp), %%edi\n"
        "testl %%esi, %%esi\n"
        "jz .Lminlz_block16k_%=_match_extend_back_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_match_extend_back_loop_encodeBlockAsm16K:\n"
        "cmpl %%edi, %%edx\n"
        "jbe .Lminlz_block16k_%=_match_extend_back_end_encodeBlockAsm16K\n"
        "movb -1(%%rbx,%%rsi,1), %%r8b\n"
        "movb -1(%%rbx,%%rdx,1), %%r9b\n"
        "cmpb %%r9b, %%r8b\n"
        "jne .Lminlz_block16k_%=_match_extend_back_end_encodeBlockAsm16K\n"
        "leal -1(%%rdx), %%edx\n"
        "decl %%esi\n"
        "jz .Lminlz_block16k_%=_match_extend_back_end_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_match_extend_back_loop_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_match_extend_back_end_encodeBlockAsm16K:\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_block16k_%=_dst_size_check_ok_2\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_dst_size_check_ok_2:\n"
        "movl %%edx, %%r8d\n"
        "movl %%edx, %%edi\n"
        "subl %%esi, %%edi\n"
        "movl %%edi, 16(%%rsp)\n"
        "addl $0x04, %%edx\n"
        "addl $0x04, %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%edx, %%edi\n"
        "leaq (%%rbx,%%rdx,1), %%r9\n"
        "leaq (%%rbx,%%rsi,1), %%rsi\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_block16k_%=_matchlen_loop_16_entry_match_nolit_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_loopback_16_match_nolit_encodeBlockAsm16K:\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "movq 8(%%r9,%%r11,1), %%r12\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_8_match_nolit_encodeBlockAsm16K\n"
        "xorq 8(%%rsi,%%r11,1), %%r12\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_16match_nolit_encodeBlockAsm16K\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_block16k_%=_matchlen_loop_16_entry_match_nolit_encodeBlockAsm16K:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_block16k_%=_matchlen_loopback_16_match_nolit_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_matchlen_match8_match_nolit_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_bsf_16match_nolit_encodeBlockAsm16K:\n"
        "tzcntq %%r12, %%r12\n"
        "sarq $0x03, %%r12\n"
        "leal 8(%%r11,%%r12,1), %%r11d\n"
        "jmp .Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match8_match_nolit_encodeBlockAsm16K:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_block16k_%=_matchlen_match4_match_nolit_encodeBlockAsm16K\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_8_match_nolit_encodeBlockAsm16K\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_block16k_%=_matchlen_match4_match_nolit_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_bsf_8_match_nolit_encodeBlockAsm16K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "addl %%r10d, %%r11d\n"
        "jmp .Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match4_match_nolit_encodeBlockAsm16K:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_block16k_%=_matchlen_match2_match_nolit_encodeBlockAsm16K\n"
        "movl (%%r9,%%r11,1), %%r10d\n"
        "cmpl %%r10d, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_matchlen_match2_match_nolit_encodeBlockAsm16K\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_block16k_%=_matchlen_match2_match_nolit_encodeBlockAsm16K:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_block16k_%=_matchlen_match1_match_nolit_encodeBlockAsm16K\n"
        "jb .Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K\n"
        "movw (%%r9,%%r11,1), %%r10w\n"
        "cmpw %%r10w, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_matchlen_match1_match_nolit_encodeBlockAsm16K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match1_match_nolit_encodeBlockAsm16K:\n"
        "movb (%%r9,%%r11,1), %%r10b\n"
        "cmpb %%r10b, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_match_nolit_end_encodeBlockAsm16K:\n"
        "addl %%r11d, %%edx\n"
        "addl $0x04, %%r11d\n"
        "movl 16(%%rsp), %%esi\n"
        "movl 12(%%rsp), %%edi\n"
        "movl %%edx, 12(%%rsp)\n"
        "subl %%edi, %%r8d\n"
        "jz .Lminlz_block16k_%=_match_nolits_copy_encodeBlockAsm16K\n"
        "leaq (%%rbx,%%rdi,1), %%rdi\n"
        "cmpl $0x03, %%r8d\n"
        "ja .Lminlz_block16k_%=_match_emit_lits_copy_encodeBlockAsm16K\n"
        "cmpl $0x40, %%esi\n"
        "jb .Lminlz_block16k_%=_match_emit_lits_copy_encodeBlockAsm16K\n"
        "movl (%%rdi), %%edi\n"
        "xorq %%r9, %%r9\n"
        "subl $0x40, %%esi\n"
        "leal -11(%%r11), %%r10d\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%si, 1(%%rcx)\n"
        "cmpl $0x07, %%r11d\n"
        "cmovgel %%r10d, %%r9d\n"
        "movq $0x00000007, %%rsi\n"
        "cmovll %%r11d, %%esi\n"
        "leal -1(%%r8,%%rsi,4), %%esi\n"
        "movl $0x00000003, %%r10d\n"
        "leal (%%r10,%%rsi,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "movl %%edi, (%%rcx)\n"
        "addq %%r8, %%rcx\n"
        "testl %%r9d, %%r9d\n"
        "jz .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        "leal -1(%%r9), %%esi\n"
        "cmpl $0x1d, %%r9d\n"
        "jbe .Lminlz_block16k_%=_repeat_one_match_emit_repeat_copy2_encodeBlockAsm16K\n"
        "leal -30(%%r9), %%esi\n"
        "cmpl $0x0000011e, %%r9d\n"
        "jb .Lminlz_block16k_%=_repeat_two_match_emit_repeat_copy2_encodeBlockAsm16K\n"
        "cmpl $0x0001001e, %%r9d\n"
        "jb .Lminlz_block16k_%=_repeat_three_match_emit_repeat_copy2_encodeBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_three_match_emit_repeat_copy2_encodeBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_two_match_emit_repeat_copy2_encodeBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_one_match_emit_repeat_copy2_encodeBlockAsm16K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r9,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_match_emit_lits_copy_encodeBlockAsm16K:\n"
        "leaq 3(%%rcx,%%r8,1), %%r9\n"
        "cmpq (%%rsp), %%r9\n"
        "jb .Lminlz_block16k_%=_dst_size_check_ok_3\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_dst_size_check_ok_3:\n"
        "leal -1(%%r8), %%r9d\n"
        "cmpl $0x1d, %%r9d\n"
        "jb .Lminlz_block16k_%=_one_byte_match_emit_encodeBlockAsm16K\n"
        "subl $0x1d, %%r9d\n"
        "cmpl $0x00000100, %%r9d\n"
        "jb .Lminlz_block16k_%=_two_bytes_match_emit_encodeBlockAsm16K\n"
        "jb .Lminlz_block16k_%=_three_bytes_match_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_three_bytes_match_emit_encodeBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_block16k_%=_memmove_long_match_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_two_bytes_match_emit_encodeBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%r9b, 1(%%rcx)\n"
        "addl $0x1d, %%r9d\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%r9d\n"
        "jb .Lminlz_block16k_%=_memmove_midmatch_emit_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_memmove_long_match_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_one_byte_match_emit_encodeBlockAsm16K:\n"
        "shlb $0x03, %%r9b\n"
        "movb %%r9b, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%r8,1), %%r9\n"
        "cmpq $0x10, %%r8\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_match_emit_encodeBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%r8\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_match_emit_encodeBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_block16k_%=_emit_lit_memmove_match_emit_encodeBlockAsm16K_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_match_emit_encodeBlockAsm16K_memmove_move_8through16:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_match_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_match_emit_encodeBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%r8,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_match_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_match_emit_encodeBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu 16(%%rdi), %%xmm1\n"
        "movdqu -32(%%rdi,%%r8,1), %%xmm2\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%r8,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%r8,1)\n"
        ".Lminlz_block16k_%=_memmove_end_copy_match_emit_encodeBlockAsm16K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolits_copy_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_memmove_midmatch_emit_encodeBlockAsm16K:\n"
        "leaq (%%rcx,%%r8,1), %%r9\n"
        "cmpq $0x20, %%r8\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_block16k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%r8,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_mid_end_copy_match_emit_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu 16(%%rdi), %%xmm1\n"
        "movdqu -32(%%rdi,%%r8,1), %%xmm2\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%r8,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%r8,1)\n"
        ".Lminlz_block16k_%=_memmove_mid_end_copy_match_emit_encodeBlockAsm16K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolits_copy_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_memmove_long_match_emit_encodeBlockAsm16K:\n"
        "leaq (%%rcx,%%r8,1), %%r9\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu 16(%%rdi), %%xmm1\n"
        "movdqu -32(%%rdi,%%r8,1), %%xmm2\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm3\n"
        "movq %%r8, %%r12\n"
        "shrq $0x05, %%r12\n"
        "movq %%rcx, %%r10\n"
        "andl $0x0000001f, %%r10d\n"
        "movq $0x00000040, %%r13\n"
        "subq %%r10, %%r13\n"
        "decq %%r12\n"
        "ja .Lminlz_block16k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%rdi,%%r13,1), %%r10\n"
        "leaq -32(%%rcx,%%r13,1), %%r14\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%r10), %%xmm4\n"
        "movdqu 16(%%r10), %%xmm5\n"
        "movdqa %%xmm4, (%%r14)\n"
        "movdqa %%xmm5, 16(%%r14)\n"
        "addq $0x20, %%r14\n"
        "addq $0x20, %%r10\n"
        "addq $0x20, %%r13\n"
        "decq %%r12\n"
        "jna .Lminlz_block16k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%rdi,%%r13,1), %%xmm4\n"
        "movdqu -16(%%rdi,%%r13,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r13,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r13,1)\n"
        "addq $0x20, %%r13\n"
        "cmpq %%r13, %%r8\n"
        "jae .Lminlz_block16k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%r8,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%r8,1)\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolits_copy_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_match_nolits_copy_encodeBlockAsm16K:\n"
        "cmpl $0x00000400, %%esi\n"
        "ja .Lminlz_block16k_%=_two_byte_match_nolit_encodeBlockAsm16K\n"
        "cmpl $0x00000013, %%r11d\n"
        "jae .Lminlz_block16k_%=_emit_one_longer_match_nolit_encodeBlockAsm16K\n"
        "leal -1(%%rsi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal -15(%%rsi,%%r11,4), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_one_longer_match_nolit_encodeBlockAsm16K:\n"
        "cmpl $0x00000112, %%r11d\n"
        "jae .Lminlz_block16k_%=_emit_copy1_repeat_match_nolit_encodeBlockAsm16K\n"
        "leal -1(%%rsi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 61(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "leal -18(%%r11), %%esi\n"
        "movb %%sil, 2(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_copy1_repeat_match_nolit_encodeBlockAsm16K:\n"
        "leal -1(%%rsi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 57(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "subl $0x12, %%r11d\n"
        "leal -1(%%r11), %%esi\n"
        "cmpl $0x1d, %%r11d\n"
        "jbe .Lminlz_block16k_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBlockAsm16K\n"
        "leal -30(%%r11), %%esi\n"
        "cmpl $0x0000011e, %%r11d\n"
        "jb .Lminlz_block16k_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBlockAsm16K\n"
        "cmpl $0x0001001e, %%r11d\n"
        "jb .Lminlz_block16k_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBlockAsm16K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r11,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_two_byte_match_nolit_encodeBlockAsm16K:\n"
        "leal -64(%%rsi), %%esi\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%si, 1(%%rcx)\n"
        "cmpl $0x3c, %%r11d\n"
        "jbe .Lminlz_block16k_%=_emit_copy2_0_match_nolit_encodeBlockAsm16K_emit2\n"
        "leal -60(%%r11), %%esi\n"
        "cmpl $0x0000013c, %%r11d\n"
        "jb .Lminlz_block16k_%=_emit_copy2_1_match_nolit_encodeBlockAsm16K_emit2\n"
        "cmpl $0x0001003c, %%r11d\n"
        "jb .Lminlz_block16k_%=_emit_copy2_2_match_nolit_encodeBlockAsm16K_emit2\n"
        "movb $0xfe, (%%rcx)\n"
        "movl %%esi, 3(%%rcx)\n"
        "addq $0x06, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_copy2_2_match_nolit_encodeBlockAsm16K_emit2:\n"
        "movb $0xfa, (%%rcx)\n"
        "movw %%si, 3(%%rcx)\n"
        "addq $0x05, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_copy2_1_match_nolit_encodeBlockAsm16K_emit2:\n"
        "movb $0xf6, (%%rcx)\n"
        "movb %%sil, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_copy2_0_match_nolit_encodeBlockAsm16K_emit2:\n"
        "movl $0x00000002, %%esi\n"
        "leal (%%rsi,%%r11,4), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        ".Lminlz_block16k_%=_match_nolit_emitcopy_end_encodeBlockAsm16K:\n"
        "cmpl 8(%%rsp), %%edx\n"
        "jae .Lminlz_block16k_%=_emit_remainder_encodeBlockAsm16K\n"
        "movq -2(%%rbx,%%rdx,1), %%rdi\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_block16k_%=_match_nolit_dst_ok_encodeBlockAsm16K\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_match_nolit_dst_ok_encodeBlockAsm16K:\n"
        "movq $0xcf1bbcdcbb000000, %%rsi\n"
        "movq %%rdi, %%r8\n"
        "shrq $0x10, %%rdi\n"
        "movq %%rdi, %%r9\n"
        "imulq %%rsi, %%r8\n"
        "shrq $0x34, %%r8\n"
        "imulq %%rsi, %%r9\n"
        "shrq $0x34, %%r9\n"
        "leal -2(%%rdx), %%r10d\n"
        "movzwl (%%rax,%%r9,2), %%esi\n"
        "movw %%r10w, (%%rax,%%r8,2)\n"
        "movw %%dx, (%%rax,%%r9,2)\n"
        "movl %%edx, %%r8d\n"
        "incl %%edx\n"
        "cmpl %%edi, (%%rbx,%%rsi,1)\n"
        "jne .Lminlz_block16k_%=_search_loop_encodeBlockAsm16K\n"
        "movl %%r8d, %%edi\n"
        "subl %%esi, %%edi\n"
        "movl %%edi, 16(%%rsp)\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_block16k_%=_dst_size_check_ok_4\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_dst_size_check_ok_4:\n"
        "addl $0x03, %%edx\n"
        "addl $0x04, %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%edx, %%edi\n"
        "leaq (%%rbx,%%rdx,1), %%r8\n"
        "leaq (%%rbx,%%rsi,1), %%rsi\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_block16k_%=_matchlen_loop_16_entry_match_nolit2_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_loopback_16_match_nolit2_encodeBlockAsm16K:\n"
        "movq (%%r8,%%r11,1), %%r9\n"
        "movq 8(%%r8,%%r11,1), %%r10\n"
        "xorq (%%rsi,%%r11,1), %%r9\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_8_match_nolit2_encodeBlockAsm16K\n"
        "xorq 8(%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_16match_nolit2_encodeBlockAsm16K\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_block16k_%=_matchlen_loop_16_entry_match_nolit2_encodeBlockAsm16K:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_block16k_%=_matchlen_loopback_16_match_nolit2_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_matchlen_match8_match_nolit2_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_bsf_16match_nolit2_encodeBlockAsm16K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "leal 8(%%r11,%%r10,1), %%r11d\n"
        "jmp .Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match8_match_nolit2_encodeBlockAsm16K:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_block16k_%=_matchlen_match4_match_nolit2_encodeBlockAsm16K\n"
        "movq (%%r8,%%r11,1), %%r9\n"
        "xorq (%%rsi,%%r11,1), %%r9\n"
        "jnz .Lminlz_block16k_%=_matchlen_bsf_8_match_nolit2_encodeBlockAsm16K\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_block16k_%=_matchlen_match4_match_nolit2_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_matchlen_bsf_8_match_nolit2_encodeBlockAsm16K:\n"
        "tzcntq %%r9, %%r9\n"
        "sarq $0x03, %%r9\n"
        "addl %%r9d, %%r11d\n"
        "jmp .Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match4_match_nolit2_encodeBlockAsm16K:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_block16k_%=_matchlen_match2_match_nolit2_encodeBlockAsm16K\n"
        "movl (%%r8,%%r11,1), %%r9d\n"
        "cmpl %%r9d, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_matchlen_match2_match_nolit2_encodeBlockAsm16K\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_block16k_%=_matchlen_match2_match_nolit2_encodeBlockAsm16K:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_block16k_%=_matchlen_match1_match_nolit2_encodeBlockAsm16K\n"
        "jb .Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K\n"
        "movw (%%r8,%%r11,1), %%r9w\n"
        "cmpw %%r9w, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_matchlen_match1_match_nolit2_encodeBlockAsm16K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_matchlen_match1_match_nolit2_encodeBlockAsm16K:\n"
        "movb (%%r8,%%r11,1), %%r9b\n"
        "cmpb %%r9b, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_match_nolit2_end_encodeBlockAsm16K:\n"
        "addl %%r11d, %%edx\n"
        "addl $0x04, %%r11d\n"
        "movl %%edx, 12(%%rsp)\n"
        "movl 16(%%rsp), %%esi\n"
        "jmp .Lminlz_block16k_%=_match_nolits_copy_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_remainder_encodeBlockAsm16K:\n"
        "movq 40(%%rsp), %%rax\n"
        "movl 12(%%rsp), %%edx\n"
        "subl %%edx, %%eax\n"
        "jz .Lminlz_block16k_%=_emit_remainder_end_encodeBlockAsm16K\n"
        "leaq (%%rbx,%%rdx,1), %%rdx\n"
        "leaq 3(%%rcx,%%rax,1), %%rbx\n"
        "cmpq (%%rsp), %%rbx\n"
        "jb .Lminlz_block16k_%=_dst_size_check_ok_5\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_dst_size_check_ok_5:\n"
        "leal -1(%%rax), %%ebx\n"
        "cmpl $0x1d, %%ebx\n"
        "jb .Lminlz_block16k_%=_one_byte_emit_remainder_encodeBlockAsm16K\n"
        "subl $0x1d, %%ebx\n"
        "cmpl $0x00000100, %%ebx\n"
        "jb .Lminlz_block16k_%=_two_bytes_emit_remainder_encodeBlockAsm16K\n"
        "jb .Lminlz_block16k_%=_three_bytes_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_three_bytes_emit_remainder_encodeBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%bx, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%ebx\n"
        "jmp .Lminlz_block16k_%=_memmove_long_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_two_bytes_emit_remainder_encodeBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%bl, 1(%%rcx)\n"
        "addl $0x1d, %%ebx\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%ebx\n"
        "jb .Lminlz_block16k_%=_memmove_midemit_remainder_encodeBlockAsm16K\n"
        "jmp .Lminlz_block16k_%=_memmove_long_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_one_byte_emit_remainder_encodeBlockAsm16K:\n"
        "shlb $0x03, %%bl\n"
        "movb %%bl, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rax,1), %%rbx\n"
        "cmpq $0x03, %%rax\n"
        "jb .Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_1or2\n"
        "je .Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_3\n"
        "cmpq $0x08, %%rax\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_4through8\n"
        "cmpq $0x10, %%rax\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%rax\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_1or2:\n"
        "movb (%%rdx), %%sil\n"
        "movb -1(%%rdx,%%rax,1), %%dl\n"
        "movb %%sil, (%%rcx)\n"
        "movb %%dl, -1(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_3:\n"
        "movw (%%rdx), %%si\n"
        "movb 2(%%rdx), %%dl\n"
        "movw %%si, (%%rcx)\n"
        "movb %%dl, 2(%%rcx)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_4through8:\n"
        "movl (%%rdx), %%esi\n"
        "movl -4(%%rdx,%%rax,1), %%edx\n"
        "movl %%esi, (%%rcx)\n"
        "movl %%edx, -4(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_8through16:\n"
        "movq (%%rdx), %%rsi\n"
        "movq -8(%%rdx,%%rax,1), %%rdx\n"
        "movq %%rsi, (%%rcx)\n"
        "movq %%rdx, -8(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu 16(%%rdx), %%xmm1\n"
        "movdqu -32(%%rdx,%%rax,1), %%xmm2\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rax,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rax,1)\n"
        ".Lminlz_block16k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm16K:\n"
        "movq %%rbx, %%rcx\n"
        "jmp .Lminlz_block16k_%=_emit_remainder_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_memmove_midemit_remainder_encodeBlockAsm16K:\n"
        "leaq (%%rcx,%%rax,1), %%rbx\n"
        "cmpq $0x20, %%rax\n"
        "jbe .Lminlz_block16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_block16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block16k_%=_memmove_mid_end_copy_emit_remainder_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu 16(%%rdx), %%xmm1\n"
        "movdqu -32(%%rdx,%%rax,1), %%xmm2\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rax,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rax,1)\n"
        ".Lminlz_block16k_%=_memmove_mid_end_copy_emit_remainder_encodeBlockAsm16K:\n"
        "movq %%rbx, %%rcx\n"
        "jmp .Lminlz_block16k_%=_emit_remainder_end_encodeBlockAsm16K\n"
        ".Lminlz_block16k_%=_memmove_long_emit_remainder_encodeBlockAsm16K:\n"
        "leaq (%%rcx,%%rax,1), %%rbx\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu 16(%%rdx), %%xmm1\n"
        "movdqu -32(%%rdx,%%rax,1), %%xmm2\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm3\n"
        "movq %%rax, %%rdi\n"
        "shrq $0x05, %%rdi\n"
        "movq %%rcx, %%rsi\n"
        "andl $0x0000001f, %%esi\n"
        "movq $0x00000040, %%r8\n"
        "subq %%rsi, %%r8\n"
        "decq %%rdi\n"
        "ja .Lminlz_block16k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%rdx,%%r8,1), %%rsi\n"
        "leaq -32(%%rcx,%%r8,1), %%r9\n"
        ".p2align 4\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%rsi), %%xmm4\n"
        "movdqu 16(%%rsi), %%xmm5\n"
        "movdqa %%xmm4, (%%r9)\n"
        "movdqa %%xmm5, 16(%%r9)\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%rsi\n"
        "addq $0x20, %%r8\n"
        "decq %%rdi\n"
        "jna .Lminlz_block16k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_block16k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%rdx,%%r8,1), %%xmm4\n"
        "movdqu -16(%%rdx,%%r8,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r8,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r8,1)\n"
        "addq $0x20, %%r8\n"
        "cmpq %%r8, %%rax\n"
        "jae .Lminlz_block16k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rax,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rax,1)\n"
        "movq %%rbx, %%rcx\n"
        ".Lminlz_block16k_%=_emit_remainder_end_encodeBlockAsm16K:\n"
        "movq 24(%%rsp), %%rax\n"
        "subq %%rax, %%rcx\n"
        "movq %%rcx, %%rax\n"
        "jmp .Lminlz_block16k_%=_epilogue\n"
        ".Lminlz_block16k_%=_epilogue:\n"
        "addq $64, %%rsp\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%rbx\n"
        : "=&a"(ret)
        : [dst] "r"(dst), [src] "r"(src), [src_len] "r"(src_len), [tmp] "r"(tmp)
        : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory", "cc");
    return ret;
#else
    (void)dst; (void)src; (void)src_len; (void)tmp;
    return 0;
#endif
}

static MINLZ_ALWAYS_INLINE size_t minlz_encode_block_64k_inline_asm(uint8_t *dst, const uint8_t *src,
                                                       size_t src_len, uint8_t *tmp) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    size_t ret;
    __asm__ volatile(
        "pushq %%rbx\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "subq $64, %%rsp\n"
        "movq %[dst], 24(%%rsp)\n"
        "movq %[src], 32(%%rsp)\n"
        "movq %[src_len], 40(%%rsp)\n"
        "movq %[tmp], 48(%%rsp)\n"
        "movq 48(%%rsp), %%rdi\n"
        "xorl %%eax, %%eax\n"
        "movl $256, %%ecx\n"
        "pxor %%xmm0, %%xmm0\n"
        ".Lminlz_clear_64k_%=:\n"
        "movdqu %%xmm0, (%%rdi)\n"
        "movdqu %%xmm0, 16(%%rdi)\n"
        "movdqu %%xmm0, 32(%%rdi)\n"
        "movdqu %%xmm0, 48(%%rdi)\n"
        "addq $64, %%rdi\n"
        "decl %%ecx\n"
        "jnz .Lminlz_clear_64k_%=\n"
        "movq 48(%%rsp), %%rax\n"
        "movq 24(%%rsp), %%rcx\n"
        "movl $0x00000000, 12(%%rsp)\n"
        "movq 40(%%rsp), %%rdx\n"
        "leaq -17(%%rdx), %%rbx\n"
        "leaq -17(%%rdx), %%rsi\n"
        "movl %%esi, 8(%%rsp)\n"
        "shrq $0x05, %%rdx\n"
        "subl %%edx, %%ebx\n"
        "leaq (%%rcx,%%rbx,1), %%rbx\n"
        "movq %%rbx, (%%rsp)\n"
        "movl $0x00000001, %%edx\n"
        "movl %%edx, 16(%%rsp)\n"
        "movq 32(%%rsp), %%rbx\n"
        ".Lminlz_block64k_%=_search_loop_encodeBlockAsm64K:\n"
        "movl %%edx, %%esi\n"
        "subl 12(%%rsp), %%esi\n"
        "shrl $0x05, %%esi\n"
        "leal 3(%%rdx,%%rsi,1), %%esi\n"
        "cmpl 8(%%rsp), %%esi\n"
        "jae .Lminlz_block64k_%=_emit_remainder_encodeBlockAsm64K\n"
        "movq (%%rbx,%%rdx,1), %%rdi\n"
        "movl %%esi, 20(%%rsp)\n"
        "movq $0xcf1bbcdcbf9b0000, %%r9\n"
        "movq %%rdi, %%r10\n"
        "imulq %%r9, %%r10\n"
        "shrq $0x33, %%r10\n"
        "movq %%rdi, %%r11\n"
        "shrq $0x08, %%r11\n"
        "imulq %%r9, %%r11\n"
        "shrq $0x33, %%r11\n"
        "movzwl (%%rax,%%r10,2), %%esi\n"
        "movzwl (%%rax,%%r11,2), %%r8d\n"
        "movw %%dx, (%%rax,%%r10,2)\n"
        "leal 1(%%rdx), %%r10d\n"
        "movw %%r10w, (%%rax,%%r11,2)\n"
        "movq %%rdi, %%r10\n"
        "shrq $0x10, %%r10\n"
        "imulq %%r9, %%r10\n"
        "shrq $0x33, %%r10\n"
        "movl %%edx, %%r9d\n"
        "subl 16(%%rsp), %%r9d\n"
        "movl 1(%%rbx,%%r9,1), %%r11d\n"
        "movq %%rdi, %%r9\n"
        "shrq $0x08, %%r9\n"
        "cmpl %%r11d, %%r9d\n"
        "jne .Lminlz_block64k_%=_no_repeat_found_encodeBlockAsm64K\n"
        "leal 1(%%rdx), %%edi\n"
        "movl 12(%%rsp), %%esi\n"
        "movl %%edi, %%r8d\n"
        "subl 16(%%rsp), %%r8d\n"
        "jz .Lminlz_block64k_%=_repeat_extend_back_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_extend_back_loop_encodeBlockAsm64K:\n"
        "cmpl %%esi, %%edi\n"
        "jbe .Lminlz_block64k_%=_repeat_extend_back_end_encodeBlockAsm64K\n"
        "movb -1(%%rbx,%%r8,1), %%r9b\n"
        "movb -1(%%rbx,%%rdi,1), %%r10b\n"
        "cmpb %%r10b, %%r9b\n"
        "jne .Lminlz_block64k_%=_repeat_extend_back_end_encodeBlockAsm64K\n"
        "leal -1(%%rdi), %%edi\n"
        "decl %%r8d\n"
        "jnz .Lminlz_block64k_%=_repeat_extend_back_loop_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_extend_back_end_encodeBlockAsm64K:\n"
        "movl %%edi, %%esi\n"
        "movl 12(%%rsp), %%r8d\n"
        "subl %%r8d, %%esi\n"
        "leaq 4(%%rcx,%%rsi,1), %%r9\n"
        "cmpq (%%rsp), %%r9\n"
        "jb .Lminlz_block64k_%=_dst_size_check_ok_1\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_dst_size_check_ok_1:\n"
        "leaq (%%rbx,%%r8,1), %%r8\n"
        "leal -1(%%rsi), %%r9d\n"
        "cmpl $0x1d, %%r9d\n"
        "jb .Lminlz_block64k_%=_one_byte_repeat_emit_lits_encodeBlockAsm64K\n"
        "subl $0x1d, %%r9d\n"
        "cmpl $0x00000100, %%r9d\n"
        "jb .Lminlz_block64k_%=_two_bytes_repeat_emit_lits_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_three_bytes_repeat_emit_lits_encodeBlockAsm64K\n"
        "movl %%r9d, %%r10d\n"
        "shrl $0x10, %%r10d\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "movb %%r10b, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_block64k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_three_bytes_repeat_emit_lits_encodeBlockAsm64K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_block64k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_two_bytes_repeat_emit_lits_encodeBlockAsm64K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%r9b, 1(%%rcx)\n"
        "addl $0x1d, %%r9d\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%r9d\n"
        "jb .Lminlz_block64k_%=_memmove_midrepeat_emit_lits_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_one_byte_repeat_emit_lits_encodeBlockAsm64K:\n"
        "shlb $0x03, %%r9b\n"
        "movb %%r9b, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x10, %%rsi\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm64K_memmove_move_8through16\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm64K_memmove_move_17through32\n"
        "jmp .Lminlz_block64k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm64K_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm64K_memmove_move_8through16:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_repeat_emit_lits_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm64K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_repeat_emit_lits_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_repeat_emit_lits_encodeBlockAsm64K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_block64k_%=_memmove_end_copy_repeat_emit_lits_encodeBlockAsm64K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block64k_%=_repeat_emit_lits_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_memmove_midrepeat_emit_lits_encodeBlockAsm64K:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm64K_memmove_move_17through32\n"
        "jmp .Lminlz_block64k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm64K_memmove_move_33through64\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm64K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_mid_end_copy_repeat_emit_lits_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_mid_repeat_emit_lits_encodeBlockAsm64K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_block64k_%=_memmove_mid_end_copy_repeat_emit_lits_encodeBlockAsm64K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block64k_%=_repeat_emit_lits_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_memmove_long_repeat_emit_lits_encodeBlockAsm64K:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movq %%rsi, %%r11\n"
        "shrq $0x05, %%r11\n"
        "movq %%rcx, %%r10\n"
        "andl $0x0000001f, %%r10d\n"
        "movq $0x00000040, %%r12\n"
        "subq %%r10, %%r12\n"
        "decq %%r11\n"
        "ja .Lminlz_block64k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm64Klarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r12,1), %%r10\n"
        "leaq -32(%%rcx,%%r12,1), %%r13\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm64Klarge_big_loop_back:\n"
        "movdqu (%%r10), %%xmm4\n"
        "movdqu 16(%%r10), %%xmm5\n"
        "movdqa %%xmm4, (%%r13)\n"
        "movdqa %%xmm5, 16(%%r13)\n"
        "addq $0x20, %%r13\n"
        "addq $0x20, %%r10\n"
        "addq $0x20, %%r12\n"
        "decq %%r11\n"
        "jna .Lminlz_block64k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm64Klarge_big_loop_back\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm64Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r12,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r12,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r12,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r12,1)\n"
        "addq $0x20, %%r12\n"
        "cmpq %%r12, %%rsi\n"
        "jae .Lminlz_block64k_%=_emit_lit_memmove_long_repeat_emit_lits_encodeBlockAsm64Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        "movq %%r9, %%rcx\n"
        ".Lminlz_block64k_%=_repeat_emit_lits_end_encodeBlockAsm64K:\n"
        "addl $0x05, %%edx\n"
        "movl %%edx, %%esi\n"
        "subl 16(%%rsp), %%esi\n"
        "movq 40(%%rsp), %%r8\n"
        "subl %%edx, %%r8d\n"
        "leaq (%%rbx,%%rdx,1), %%r9\n"
        "leaq (%%rbx,%%rsi,1), %%rsi\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_block64k_%=_matchlen_loop_16_entry_repeat_extend_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_loopback_16_repeat_extend_encodeBlockAsm64K:\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "movq 8(%%r9,%%r11,1), %%r12\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_8_repeat_extend_encodeBlockAsm64K\n"
        "xorq 8(%%rsi,%%r11,1), %%r12\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_16repeat_extend_encodeBlockAsm64K\n"
        "leal -16(%%r8), %%r8d\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_block64k_%=_matchlen_loop_16_entry_repeat_extend_encodeBlockAsm64K:\n"
        "cmpl $0x10, %%r8d\n"
        "jae .Lminlz_block64k_%=_matchlen_loopback_16_repeat_extend_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_matchlen_match8_repeat_extend_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_bsf_16repeat_extend_encodeBlockAsm64K:\n"
        "tzcntq %%r12, %%r12\n"
        "sarq $0x03, %%r12\n"
        "leal 8(%%r11,%%r12,1), %%r11d\n"
        "jmp .Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match8_repeat_extend_encodeBlockAsm64K:\n"
        "cmpl $0x08, %%r8d\n"
        "jb .Lminlz_block64k_%=_matchlen_match4_repeat_extend_encodeBlockAsm64K\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_8_repeat_extend_encodeBlockAsm64K\n"
        "leal -8(%%r8), %%r8d\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_block64k_%=_matchlen_match4_repeat_extend_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_bsf_8_repeat_extend_encodeBlockAsm64K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "addl %%r10d, %%r11d\n"
        "jmp .Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match4_repeat_extend_encodeBlockAsm64K:\n"
        "cmpl $0x04, %%r8d\n"
        "jb .Lminlz_block64k_%=_matchlen_match2_repeat_extend_encodeBlockAsm64K\n"
        "movl (%%r9,%%r11,1), %%r10d\n"
        "cmpl %%r10d, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_matchlen_match2_repeat_extend_encodeBlockAsm64K\n"
        "leal -4(%%r8), %%r8d\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_block64k_%=_matchlen_match2_repeat_extend_encodeBlockAsm64K:\n"
        "cmpl $0x01, %%r8d\n"
        "je .Lminlz_block64k_%=_matchlen_match1_repeat_extend_encodeBlockAsm64K\n"
        "jb .Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K\n"
        "movw (%%r9,%%r11,1), %%r10w\n"
        "cmpw %%r10w, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_matchlen_match1_repeat_extend_encodeBlockAsm64K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%r8d\n"
        "jz .Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match1_repeat_extend_encodeBlockAsm64K:\n"
        "movb (%%r9,%%r11,1), %%r10b\n"
        "cmpb %%r10b, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_repeat_extend_forward_end_encodeBlockAsm64K:\n"
        "addl %%r11d, %%edx\n"
        "movl %%edx, %%esi\n"
        "subl %%edi, %%esi\n"
        "movl 16(%%rsp), %%edi\n"
        "leal -1(%%rsi), %%edi\n"
        "cmpl $0x1d, %%esi\n"
        "jbe .Lminlz_block64k_%=_repeat_one_match_repeat_encodeBlockAsm64K\n"
        "leal -30(%%rsi), %%edi\n"
        "cmpl $0x0000011e, %%esi\n"
        "jb .Lminlz_block64k_%=_repeat_two_match_repeat_encodeBlockAsm64K\n"
        "cmpl $0x0001001e, %%esi\n"
        "jb .Lminlz_block64k_%=_repeat_three_match_repeat_encodeBlockAsm64K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%edi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block64k_%=_repeat_end_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_three_match_repeat_encodeBlockAsm64K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%di, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block64k_%=_repeat_end_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_two_match_repeat_encodeBlockAsm64K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%dil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block64k_%=_repeat_end_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_one_match_repeat_encodeBlockAsm64K:\n"
        "xorl %%edi, %%edi\n"
        "leal -4(%%rdi,%%rsi,8), %%edi\n"
        "movb %%dil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        ".Lminlz_block64k_%=_repeat_end_emit_encodeBlockAsm64K:\n"
        "movl %%edx, 12(%%rsp)\n"
        "jmp .Lminlz_block64k_%=_search_loop_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_no_repeat_found_encodeBlockAsm64K:\n"
        "cmpl %%edi, (%%rbx,%%rsi,1)\n"
        "je .Lminlz_block64k_%=_candidate_match_encodeBlockAsm64K\n"
        "shrq $0x08, %%rdi\n"
        "movzwl (%%rax,%%r10,2), %%esi\n"
        "leal 2(%%rdx), %%r9d\n"
        "cmpl %%edi, (%%rbx,%%r8,1)\n"
        "je .Lminlz_block64k_%=_candidate2_match_encodeBlockAsm64K\n"
        "movw %%r9w, (%%rax,%%r10,2)\n"
        "shrq $0x08, %%rdi\n"
        "cmpl %%edi, (%%rbx,%%rsi,1)\n"
        "je .Lminlz_block64k_%=_candidate3_match_encodeBlockAsm64K\n"
        "movl 20(%%rsp), %%edx\n"
        "jmp .Lminlz_block64k_%=_search_loop_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_candidate3_match_encodeBlockAsm64K:\n"
        "addl $0x02, %%edx\n"
        "jmp .Lminlz_block64k_%=_candidate_match_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_candidate2_match_encodeBlockAsm64K:\n"
        "movw %%r9w, (%%rax,%%r10,2)\n"
        "incl %%edx\n"
        "movl %%r8d, %%esi\n"
        ".Lminlz_block64k_%=_candidate_match_encodeBlockAsm64K:\n"
        "movl 12(%%rsp), %%edi\n"
        "testl %%esi, %%esi\n"
        "jz .Lminlz_block64k_%=_match_extend_back_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_match_extend_back_loop_encodeBlockAsm64K:\n"
        "cmpl %%edi, %%edx\n"
        "jbe .Lminlz_block64k_%=_match_extend_back_end_encodeBlockAsm64K\n"
        "movb -1(%%rbx,%%rsi,1), %%r8b\n"
        "movb -1(%%rbx,%%rdx,1), %%r9b\n"
        "cmpb %%r9b, %%r8b\n"
        "jne .Lminlz_block64k_%=_match_extend_back_end_encodeBlockAsm64K\n"
        "leal -1(%%rdx), %%edx\n"
        "decl %%esi\n"
        "jz .Lminlz_block64k_%=_match_extend_back_end_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_match_extend_back_loop_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_match_extend_back_end_encodeBlockAsm64K:\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_block64k_%=_dst_size_check_ok_2\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_dst_size_check_ok_2:\n"
        "movl %%edx, %%r8d\n"
        "movl %%edx, %%edi\n"
        "subl %%esi, %%edi\n"
        "movl %%edi, 16(%%rsp)\n"
        "addl $0x04, %%edx\n"
        "addl $0x04, %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%edx, %%edi\n"
        "leaq (%%rbx,%%rdx,1), %%r9\n"
        "leaq (%%rbx,%%rsi,1), %%rsi\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_block64k_%=_matchlen_loop_16_entry_match_nolit_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_loopback_16_match_nolit_encodeBlockAsm64K:\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "movq 8(%%r9,%%r11,1), %%r12\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_8_match_nolit_encodeBlockAsm64K\n"
        "xorq 8(%%rsi,%%r11,1), %%r12\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_16match_nolit_encodeBlockAsm64K\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_block64k_%=_matchlen_loop_16_entry_match_nolit_encodeBlockAsm64K:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_block64k_%=_matchlen_loopback_16_match_nolit_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_matchlen_match8_match_nolit_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_bsf_16match_nolit_encodeBlockAsm64K:\n"
        "tzcntq %%r12, %%r12\n"
        "sarq $0x03, %%r12\n"
        "leal 8(%%r11,%%r12,1), %%r11d\n"
        "jmp .Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match8_match_nolit_encodeBlockAsm64K:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_block64k_%=_matchlen_match4_match_nolit_encodeBlockAsm64K\n"
        "movq (%%r9,%%r11,1), %%r10\n"
        "xorq (%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_8_match_nolit_encodeBlockAsm64K\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_block64k_%=_matchlen_match4_match_nolit_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_bsf_8_match_nolit_encodeBlockAsm64K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "addl %%r10d, %%r11d\n"
        "jmp .Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match4_match_nolit_encodeBlockAsm64K:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_block64k_%=_matchlen_match2_match_nolit_encodeBlockAsm64K\n"
        "movl (%%r9,%%r11,1), %%r10d\n"
        "cmpl %%r10d, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_matchlen_match2_match_nolit_encodeBlockAsm64K\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_block64k_%=_matchlen_match2_match_nolit_encodeBlockAsm64K:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_block64k_%=_matchlen_match1_match_nolit_encodeBlockAsm64K\n"
        "jb .Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K\n"
        "movw (%%r9,%%r11,1), %%r10w\n"
        "cmpw %%r10w, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_matchlen_match1_match_nolit_encodeBlockAsm64K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match1_match_nolit_encodeBlockAsm64K:\n"
        "movb (%%r9,%%r11,1), %%r10b\n"
        "cmpb %%r10b, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_match_nolit_end_encodeBlockAsm64K:\n"
        "addl %%r11d, %%edx\n"
        "addl $0x04, %%r11d\n"
        "movl 16(%%rsp), %%esi\n"
        "movl 12(%%rsp), %%edi\n"
        "movl %%edx, 12(%%rsp)\n"
        "subl %%edi, %%r8d\n"
        "jz .Lminlz_block64k_%=_match_nolits_copy_encodeBlockAsm64K\n"
        "leaq (%%rbx,%%rdi,1), %%rdi\n"
        "cmpl $0x03, %%r8d\n"
        "ja .Lminlz_block64k_%=_match_emit_lits_copy_encodeBlockAsm64K\n"
        "cmpl $0x40, %%esi\n"
        "jb .Lminlz_block64k_%=_match_emit_lits_copy_encodeBlockAsm64K\n"
        "movl (%%rdi), %%edi\n"
        "xorq %%r9, %%r9\n"
        "subl $0x40, %%esi\n"
        "leal -11(%%r11), %%r10d\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%si, 1(%%rcx)\n"
        "cmpl $0x07, %%r11d\n"
        "cmovgel %%r10d, %%r9d\n"
        "movq $0x00000007, %%rsi\n"
        "cmovll %%r11d, %%esi\n"
        "leal -1(%%r8,%%rsi,4), %%esi\n"
        "movl $0x00000003, %%r10d\n"
        "leal (%%r10,%%rsi,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "movl %%edi, (%%rcx)\n"
        "addq %%r8, %%rcx\n"
        "testl %%r9d, %%r9d\n"
        "jz .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        "leal -1(%%r9), %%esi\n"
        "cmpl $0x1d, %%r9d\n"
        "jbe .Lminlz_block64k_%=_repeat_one_match_emit_repeat_copy2_encodeBlockAsm64K\n"
        "leal -30(%%r9), %%esi\n"
        "cmpl $0x0000011e, %%r9d\n"
        "jb .Lminlz_block64k_%=_repeat_two_match_emit_repeat_copy2_encodeBlockAsm64K\n"
        "cmpl $0x0001001e, %%r9d\n"
        "jb .Lminlz_block64k_%=_repeat_three_match_emit_repeat_copy2_encodeBlockAsm64K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_three_match_emit_repeat_copy2_encodeBlockAsm64K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_two_match_emit_repeat_copy2_encodeBlockAsm64K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_one_match_emit_repeat_copy2_encodeBlockAsm64K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r9,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_match_emit_lits_copy_encodeBlockAsm64K:\n"
        "leaq 4(%%rcx,%%r8,1), %%r9\n"
        "cmpq (%%rsp), %%r9\n"
        "jb .Lminlz_block64k_%=_dst_size_check_ok_3\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_dst_size_check_ok_3:\n"
        "leal -1(%%r8), %%r9d\n"
        "cmpl $0x1d, %%r9d\n"
        "jb .Lminlz_block64k_%=_one_byte_match_emit_encodeBlockAsm64K\n"
        "subl $0x1d, %%r9d\n"
        "cmpl $0x00000100, %%r9d\n"
        "jb .Lminlz_block64k_%=_two_bytes_match_emit_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_three_bytes_match_emit_encodeBlockAsm64K\n"
        "movl %%r9d, %%r10d\n"
        "shrl $0x10, %%r10d\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "movb %%r10b, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_block64k_%=_memmove_long_match_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_three_bytes_match_emit_encodeBlockAsm64K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_block64k_%=_memmove_long_match_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_two_bytes_match_emit_encodeBlockAsm64K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%r9b, 1(%%rcx)\n"
        "addl $0x1d, %%r9d\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%r9d\n"
        "jb .Lminlz_block64k_%=_memmove_midmatch_emit_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_memmove_long_match_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_one_byte_match_emit_encodeBlockAsm64K:\n"
        "shlb $0x03, %%r9b\n"
        "movb %%r9b, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%r8,1), %%r9\n"
        "cmpq $0x10, %%r8\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_match_emit_encodeBlockAsm64K_memmove_move_8through16\n"
        "cmpq $0x20, %%r8\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_match_emit_encodeBlockAsm64K_memmove_move_17through32\n"
        "jmp .Lminlz_block64k_%=_emit_lit_memmove_match_emit_encodeBlockAsm64K_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_match_emit_encodeBlockAsm64K_memmove_move_8through16:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_match_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_match_emit_encodeBlockAsm64K_memmove_move_17through32:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%r8,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_match_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_match_emit_encodeBlockAsm64K_memmove_move_33through64:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu 16(%%rdi), %%xmm1\n"
        "movdqu -32(%%rdi,%%r8,1), %%xmm2\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%r8,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%r8,1)\n"
        ".Lminlz_block64k_%=_memmove_end_copy_match_emit_encodeBlockAsm64K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolits_copy_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_memmove_midmatch_emit_encodeBlockAsm64K:\n"
        "leaq (%%rcx,%%r8,1), %%r9\n"
        "cmpq $0x20, %%r8\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm64K_memmove_move_17through32\n"
        "jmp .Lminlz_block64k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm64K_memmove_move_33through64\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm64K_memmove_move_17through32:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%r8,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_mid_end_copy_match_emit_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_mid_match_emit_encodeBlockAsm64K_memmove_move_33through64:\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu 16(%%rdi), %%xmm1\n"
        "movdqu -32(%%rdi,%%r8,1), %%xmm2\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%r8,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%r8,1)\n"
        ".Lminlz_block64k_%=_memmove_mid_end_copy_match_emit_encodeBlockAsm64K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolits_copy_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_memmove_long_match_emit_encodeBlockAsm64K:\n"
        "leaq (%%rcx,%%r8,1), %%r9\n"
        "movdqu (%%rdi), %%xmm0\n"
        "movdqu 16(%%rdi), %%xmm1\n"
        "movdqu -32(%%rdi,%%r8,1), %%xmm2\n"
        "movdqu -16(%%rdi,%%r8,1), %%xmm3\n"
        "movq %%r8, %%r12\n"
        "shrq $0x05, %%r12\n"
        "movq %%rcx, %%r10\n"
        "andl $0x0000001f, %%r10d\n"
        "movq $0x00000040, %%r13\n"
        "subq %%r10, %%r13\n"
        "decq %%r12\n"
        "ja .Lminlz_block64k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm64Klarge_forward_sse_loop_32\n"
        "leaq -32(%%rdi,%%r13,1), %%r10\n"
        "leaq -32(%%rcx,%%r13,1), %%r14\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm64Klarge_big_loop_back:\n"
        "movdqu (%%r10), %%xmm4\n"
        "movdqu 16(%%r10), %%xmm5\n"
        "movdqa %%xmm4, (%%r14)\n"
        "movdqa %%xmm5, 16(%%r14)\n"
        "addq $0x20, %%r14\n"
        "addq $0x20, %%r10\n"
        "addq $0x20, %%r13\n"
        "decq %%r12\n"
        "jna .Lminlz_block64k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm64Klarge_big_loop_back\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm64Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%rdi,%%r13,1), %%xmm4\n"
        "movdqu -16(%%rdi,%%r13,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r13,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r13,1)\n"
        "addq $0x20, %%r13\n"
        "cmpq %%r13, %%r8\n"
        "jae .Lminlz_block64k_%=_emit_lit_memmove_long_match_emit_encodeBlockAsm64Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%r8,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%r8,1)\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolits_copy_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_match_nolits_copy_encodeBlockAsm64K:\n"
        "cmpl $0x00000400, %%esi\n"
        "ja .Lminlz_block64k_%=_two_byte_match_nolit_encodeBlockAsm64K\n"
        "cmpl $0x00000013, %%r11d\n"
        "jae .Lminlz_block64k_%=_emit_one_longer_match_nolit_encodeBlockAsm64K\n"
        "leal -1(%%rsi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal -15(%%rsi,%%r11,4), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_one_longer_match_nolit_encodeBlockAsm64K:\n"
        "cmpl $0x00000112, %%r11d\n"
        "jae .Lminlz_block64k_%=_emit_copy1_repeat_match_nolit_encodeBlockAsm64K\n"
        "leal -1(%%rsi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 61(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "leal -18(%%r11), %%esi\n"
        "movb %%sil, 2(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_copy1_repeat_match_nolit_encodeBlockAsm64K:\n"
        "leal -1(%%rsi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 57(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "subl $0x12, %%r11d\n"
        "leal -1(%%r11), %%esi\n"
        "cmpl $0x1d, %%r11d\n"
        "jbe .Lminlz_block64k_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBlockAsm64K\n"
        "leal -30(%%r11), %%esi\n"
        "cmpl $0x0000011e, %%r11d\n"
        "jb .Lminlz_block64k_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBlockAsm64K\n"
        "cmpl $0x0001001e, %%r11d\n"
        "jb .Lminlz_block64k_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBlockAsm64K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBlockAsm64K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBlockAsm64K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBlockAsm64K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r11,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_two_byte_match_nolit_encodeBlockAsm64K:\n"
        "leal -64(%%rsi), %%esi\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%si, 1(%%rcx)\n"
        "cmpl $0x3c, %%r11d\n"
        "jbe .Lminlz_block64k_%=_emit_copy2_0_match_nolit_encodeBlockAsm64K_emit2\n"
        "leal -60(%%r11), %%esi\n"
        "cmpl $0x0000013c, %%r11d\n"
        "jb .Lminlz_block64k_%=_emit_copy2_1_match_nolit_encodeBlockAsm64K_emit2\n"
        "cmpl $0x0001003c, %%r11d\n"
        "jb .Lminlz_block64k_%=_emit_copy2_2_match_nolit_encodeBlockAsm64K_emit2\n"
        "movb $0xfe, (%%rcx)\n"
        "movl %%esi, 3(%%rcx)\n"
        "addq $0x06, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_copy2_2_match_nolit_encodeBlockAsm64K_emit2:\n"
        "movb $0xfa, (%%rcx)\n"
        "movw %%si, 3(%%rcx)\n"
        "addq $0x05, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_copy2_1_match_nolit_encodeBlockAsm64K_emit2:\n"
        "movb $0xf6, (%%rcx)\n"
        "movb %%sil, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_copy2_0_match_nolit_encodeBlockAsm64K_emit2:\n"
        "movl $0x00000002, %%esi\n"
        "leal (%%rsi,%%r11,4), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        ".Lminlz_block64k_%=_match_nolit_emitcopy_end_encodeBlockAsm64K:\n"
        "cmpl 8(%%rsp), %%edx\n"
        "jae .Lminlz_block64k_%=_emit_remainder_encodeBlockAsm64K\n"
        "movq -2(%%rbx,%%rdx,1), %%rdi\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_block64k_%=_match_nolit_dst_ok_encodeBlockAsm64K\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_match_nolit_dst_ok_encodeBlockAsm64K:\n"
        "movq $0xcf1bbcdcbf9b0000, %%rsi\n"
        "movq %%rdi, %%r8\n"
        "shrq $0x10, %%rdi\n"
        "movq %%rdi, %%r9\n"
        "imulq %%rsi, %%r8\n"
        "shrq $0x33, %%r8\n"
        "imulq %%rsi, %%r9\n"
        "shrq $0x33, %%r9\n"
        "leal -2(%%rdx), %%r10d\n"
        "movzwl (%%rax,%%r9,2), %%esi\n"
        "movw %%r10w, (%%rax,%%r8,2)\n"
        "movw %%dx, (%%rax,%%r9,2)\n"
        "movl %%edx, %%r8d\n"
        "incl %%edx\n"
        "cmpl %%edi, (%%rbx,%%rsi,1)\n"
        "jne .Lminlz_block64k_%=_search_loop_encodeBlockAsm64K\n"
        "movl %%r8d, %%edi\n"
        "subl %%esi, %%edi\n"
        "movl %%edi, 16(%%rsp)\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_block64k_%=_dst_size_check_ok_4\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_dst_size_check_ok_4:\n"
        "addl $0x03, %%edx\n"
        "addl $0x04, %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%edx, %%edi\n"
        "leaq (%%rbx,%%rdx,1), %%r8\n"
        "leaq (%%rbx,%%rsi,1), %%rsi\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_block64k_%=_matchlen_loop_16_entry_match_nolit2_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_loopback_16_match_nolit2_encodeBlockAsm64K:\n"
        "movq (%%r8,%%r11,1), %%r9\n"
        "movq 8(%%r8,%%r11,1), %%r10\n"
        "xorq (%%rsi,%%r11,1), %%r9\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_8_match_nolit2_encodeBlockAsm64K\n"
        "xorq 8(%%rsi,%%r11,1), %%r10\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_16match_nolit2_encodeBlockAsm64K\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_block64k_%=_matchlen_loop_16_entry_match_nolit2_encodeBlockAsm64K:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_block64k_%=_matchlen_loopback_16_match_nolit2_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_matchlen_match8_match_nolit2_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_bsf_16match_nolit2_encodeBlockAsm64K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "leal 8(%%r11,%%r10,1), %%r11d\n"
        "jmp .Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match8_match_nolit2_encodeBlockAsm64K:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_block64k_%=_matchlen_match4_match_nolit2_encodeBlockAsm64K\n"
        "movq (%%r8,%%r11,1), %%r9\n"
        "xorq (%%rsi,%%r11,1), %%r9\n"
        "jnz .Lminlz_block64k_%=_matchlen_bsf_8_match_nolit2_encodeBlockAsm64K\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_block64k_%=_matchlen_match4_match_nolit2_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_matchlen_bsf_8_match_nolit2_encodeBlockAsm64K:\n"
        "tzcntq %%r9, %%r9\n"
        "sarq $0x03, %%r9\n"
        "addl %%r9d, %%r11d\n"
        "jmp .Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match4_match_nolit2_encodeBlockAsm64K:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_block64k_%=_matchlen_match2_match_nolit2_encodeBlockAsm64K\n"
        "movl (%%r8,%%r11,1), %%r9d\n"
        "cmpl %%r9d, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_matchlen_match2_match_nolit2_encodeBlockAsm64K\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_block64k_%=_matchlen_match2_match_nolit2_encodeBlockAsm64K:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_block64k_%=_matchlen_match1_match_nolit2_encodeBlockAsm64K\n"
        "jb .Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K\n"
        "movw (%%r8,%%r11,1), %%r9w\n"
        "cmpw %%r9w, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_matchlen_match1_match_nolit2_encodeBlockAsm64K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_matchlen_match1_match_nolit2_encodeBlockAsm64K:\n"
        "movb (%%r8,%%r11,1), %%r9b\n"
        "cmpb %%r9b, (%%rsi,%%r11,1)\n"
        "jne .Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_match_nolit2_end_encodeBlockAsm64K:\n"
        "addl %%r11d, %%edx\n"
        "addl $0x04, %%r11d\n"
        "movl %%edx, 12(%%rsp)\n"
        "movl 16(%%rsp), %%esi\n"
        "jmp .Lminlz_block64k_%=_match_nolits_copy_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_remainder_encodeBlockAsm64K:\n"
        "movq 40(%%rsp), %%rax\n"
        "movl 12(%%rsp), %%edx\n"
        "subl %%edx, %%eax\n"
        "jz .Lminlz_block64k_%=_emit_remainder_end_encodeBlockAsm64K\n"
        "leaq (%%rbx,%%rdx,1), %%rdx\n"
        "leaq 4(%%rcx,%%rax,1), %%rbx\n"
        "cmpq (%%rsp), %%rbx\n"
        "jb .Lminlz_block64k_%=_dst_size_check_ok_5\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_dst_size_check_ok_5:\n"
        "leal -1(%%rax), %%ebx\n"
        "cmpl $0x1d, %%ebx\n"
        "jb .Lminlz_block64k_%=_one_byte_emit_remainder_encodeBlockAsm64K\n"
        "subl $0x1d, %%ebx\n"
        "cmpl $0x00000100, %%ebx\n"
        "jb .Lminlz_block64k_%=_two_bytes_emit_remainder_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_three_bytes_emit_remainder_encodeBlockAsm64K\n"
        "movl %%ebx, %%esi\n"
        "shrl $0x10, %%esi\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%bx, 1(%%rcx)\n"
        "movb %%sil, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%ebx\n"
        "jmp .Lminlz_block64k_%=_memmove_long_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_three_bytes_emit_remainder_encodeBlockAsm64K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%bx, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%ebx\n"
        "jmp .Lminlz_block64k_%=_memmove_long_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_two_bytes_emit_remainder_encodeBlockAsm64K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%bl, 1(%%rcx)\n"
        "addl $0x1d, %%ebx\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%ebx\n"
        "jb .Lminlz_block64k_%=_memmove_midemit_remainder_encodeBlockAsm64K\n"
        "jmp .Lminlz_block64k_%=_memmove_long_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_one_byte_emit_remainder_encodeBlockAsm64K:\n"
        "shlb $0x03, %%bl\n"
        "movb %%bl, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rax,1), %%rbx\n"
        "cmpq $0x03, %%rax\n"
        "jb .Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_1or2\n"
        "je .Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_3\n"
        "cmpq $0x08, %%rax\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_4through8\n"
        "cmpq $0x10, %%rax\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_8through16\n"
        "cmpq $0x20, %%rax\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_17through32\n"
        "jmp .Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_33through64\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_1or2:\n"
        "movb (%%rdx), %%sil\n"
        "movb -1(%%rdx,%%rax,1), %%dl\n"
        "movb %%sil, (%%rcx)\n"
        "movb %%dl, -1(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_3:\n"
        "movw (%%rdx), %%si\n"
        "movb 2(%%rdx), %%dl\n"
        "movw %%si, (%%rcx)\n"
        "movb %%dl, 2(%%rcx)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_4through8:\n"
        "movl (%%rdx), %%esi\n"
        "movl -4(%%rdx,%%rax,1), %%edx\n"
        "movl %%esi, (%%rcx)\n"
        "movl %%edx, -4(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm64K\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_8through16:\n"
        "movq (%%rdx), %%rsi\n"
        "movq -8(%%rdx,%%rax,1), %%rdx\n"
        "movq %%rsi, (%%rcx)\n"
        "movq %%rdx, -8(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_17through32:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_emit_remainder_encodeBlockAsm64K_memmove_move_33through64:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu 16(%%rdx), %%xmm1\n"
        "movdqu -32(%%rdx,%%rax,1), %%xmm2\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rax,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rax,1)\n"
        ".Lminlz_block64k_%=_memmove_end_copy_emit_remainder_encodeBlockAsm64K:\n"
        "movq %%rbx, %%rcx\n"
        "jmp .Lminlz_block64k_%=_emit_remainder_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_memmove_midemit_remainder_encodeBlockAsm64K:\n"
        "leaq (%%rcx,%%rax,1), %%rbx\n"
        "cmpq $0x20, %%rax\n"
        "jbe .Lminlz_block64k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm64K_memmove_move_17through32\n"
        "jmp .Lminlz_block64k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm64K_memmove_move_33through64\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm64K_memmove_move_17through32:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rax,1)\n"
        "jmp .Lminlz_block64k_%=_memmove_mid_end_copy_emit_remainder_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_mid_emit_remainder_encodeBlockAsm64K_memmove_move_33through64:\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu 16(%%rdx), %%xmm1\n"
        "movdqu -32(%%rdx,%%rax,1), %%xmm2\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rax,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rax,1)\n"
        ".Lminlz_block64k_%=_memmove_mid_end_copy_emit_remainder_encodeBlockAsm64K:\n"
        "movq %%rbx, %%rcx\n"
        "jmp .Lminlz_block64k_%=_emit_remainder_end_encodeBlockAsm64K\n"
        ".Lminlz_block64k_%=_memmove_long_emit_remainder_encodeBlockAsm64K:\n"
        "leaq (%%rcx,%%rax,1), %%rbx\n"
        "movdqu (%%rdx), %%xmm0\n"
        "movdqu 16(%%rdx), %%xmm1\n"
        "movdqu -32(%%rdx,%%rax,1), %%xmm2\n"
        "movdqu -16(%%rdx,%%rax,1), %%xmm3\n"
        "movq %%rax, %%rdi\n"
        "shrq $0x05, %%rdi\n"
        "movq %%rcx, %%rsi\n"
        "andl $0x0000001f, %%esi\n"
        "movq $0x00000040, %%r8\n"
        "subq %%rsi, %%r8\n"
        "decq %%rdi\n"
        "ja .Lminlz_block64k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm64Klarge_forward_sse_loop_32\n"
        "leaq -32(%%rdx,%%r8,1), %%rsi\n"
        "leaq -32(%%rcx,%%r8,1), %%r9\n"
        ".p2align 4\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm64Klarge_big_loop_back:\n"
        "movdqu (%%rsi), %%xmm4\n"
        "movdqu 16(%%rsi), %%xmm5\n"
        "movdqa %%xmm4, (%%r9)\n"
        "movdqa %%xmm5, 16(%%r9)\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%rsi\n"
        "addq $0x20, %%r8\n"
        "decq %%rdi\n"
        "jna .Lminlz_block64k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm64Klarge_big_loop_back\n"
        ".Lminlz_block64k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm64Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%rdx,%%r8,1), %%xmm4\n"
        "movdqu -16(%%rdx,%%r8,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r8,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r8,1)\n"
        "addq $0x20, %%r8\n"
        "cmpq %%r8, %%rax\n"
        "jae .Lminlz_block64k_%=_emit_lit_memmove_long_emit_remainder_encodeBlockAsm64Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rax,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rax,1)\n"
        "movq %%rbx, %%rcx\n"
        ".Lminlz_block64k_%=_emit_remainder_end_encodeBlockAsm64K:\n"
        "movq 24(%%rsp), %%rax\n"
        "subq %%rax, %%rcx\n"
        "movq %%rcx, %%rax\n"
        "jmp .Lminlz_block64k_%=_epilogue\n"
        ".Lminlz_block64k_%=_epilogue:\n"
        "addq $64, %%rsp\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%rbx\n"
        : "=&a"(ret)
        : [dst] "r"(dst), [src] "r"(src), [src_len] "r"(src_len), [tmp] "r"(tmp)
        : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory", "cc");
    return ret;
#else
    (void)dst; (void)src; (void)src_len; (void)tmp;
    return 0;
#endif
}
#if MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
#if 1
#define MINLZ_L1_ASM_NAME minlz_encode_block_2mb_inline_asm_sse2
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
#define MINLZ_L1_ASM_ATTR MINLZ_NOINLINE
#else
#define MINLZ_L1_ASM_ATTR MINLZ_ALWAYS_INLINE
#endif
#define MINLZ_L1_COMPARE_BYTES 16
#include "block_encode_l1_2mb_amd64.inc"
#undef MINLZ_L1_COMPARE_BYTES
#undef MINLZ_L1_ASM_ATTR
#undef MINLZ_L1_ASM_NAME
#endif
#if MINLZ_HAVE_AVX2 && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
#define MINLZ_L1_ASM_NAME minlz_encode_block_2mb_inline_asm_avx2
#define MINLZ_L1_ASM_ATTR MINLZ_NOINLINE __attribute__((target("avx2")))
#define MINLZ_L1_COMPARE_BYTES 32
#include "block_encode_l1_2mb_amd64.inc"
#undef MINLZ_L1_COMPARE_BYTES
#undef MINLZ_L1_ASM_ATTR
#undef MINLZ_L1_ASM_NAME
#endif
#if MINLZ_HAVE_AVX2 && MINLZ_HAVE_AVX512 && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
#define MINLZ_L1_ASM_NAME minlz_encode_block_2mb_inline_asm_avx512
#define MINLZ_L1_ASM_ATTR MINLZ_NOINLINE __attribute__((target("avx512f,avx512bw,avx512vl")))
#define MINLZ_L1_COMPARE_BYTES 64
#include "block_encode_l1_2mb_amd64.inc"
#undef MINLZ_L1_COMPARE_BYTES
#undef MINLZ_L1_ASM_ATTR
#undef MINLZ_L1_ASM_NAME
#endif
static MINLZ_ALWAYS_INLINE size_t minlz_encode_block_2mb_inline_asm(uint8_t *dst, const uint8_t *src,
                                                       size_t src_len, uint8_t *tmp) {
#if MINLZ_HAVE_AVX2 && MINLZ_HAVE_AVX512 && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    if (minlz_backend_has_avx512() && minlz_backend_has_avx2()) {
        return minlz_encode_block_2mb_inline_asm_avx512(dst, src, src_len, tmp);
    }
#endif
#if MINLZ_HAVE_AVX2 && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    if (minlz_backend_has_avx2()) {
        return minlz_encode_block_2mb_inline_asm_avx2(dst, src, src_len, tmp);
    }
#endif
    return minlz_encode_block_2mb_inline_asm_sse2(dst, src, src_len, tmp);
}

#endif

#endif

#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM
static MINLZ_ALWAYS_INLINE size_t minlz_encode_better_block_2mb_inline_asm(uint8_t *dst, const uint8_t *src,
                                                                 size_t src_len, uint8_t *tmp) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    size_t ret;
#if MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM
    const struct {
        uint8_t *dst;
        const uint8_t *src;
        size_t src_len;
        uint8_t *tmp;
    } ctx = { dst, src, src_len, tmp };
#endif
#if MINLZ_BETTER2MB_EXTERNAL_MEMSET
    memset(tmp, 0, 589824u);
#endif
    __asm__ volatile(
#if !MINLZ_BETTER2MB_REGREWRITE_ANY
        "pushq %%rbx\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
#endif
        "pushq %%r15\n"
#if MINLZ_BETTER2MB_TABLE_RBP
        "pushq %%rbp\n"
#endif
        "subq $64, %%rsp\n"
#if MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM
        "movq 0(%[ctx]), %%rax\n"
        "movq %%rax, 24(%%rsp)\n"
        "movq 8(%[ctx]), %%rax\n"
        "movq %%rax, 32(%%rsp)\n"
        "movq 16(%[ctx]), %%rax\n"
        "movq %%rax, 40(%%rsp)\n"
        "movq 24(%[ctx]), %%rax\n"
        "movq %%rax, 48(%%rsp)\n"
#else
        "movq %[dst], 24(%%rsp)\n"
        "movq %[src], 32(%%rsp)\n"
        "movq %[src_len], 40(%%rsp)\n"
        "movq %[tmp], 48(%%rsp)\n"
#endif
#if MINLZ_BETTER2MB_TABLE_RBP
        "movq 48(%%rsp), %%rbp\n"
#endif
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "movq $" MINLZ_BETTER2MB_LONG_HASH_MULT ", %%rax\n"
        "movq %%rax, 56(%%rsp)\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "movq $" MINLZ_BETTER2MB_LONG_HASH_MULT ", %%rbp\n"
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
        "movq $" MINLZ_BETTER2MB_LONG_HASH_MULT ", %%rax\n"
        "movq %%rax, %%xmm6\n"
#endif
        "movq 48(%%rsp), %%rdi\n"
#if !MINLZ_BETTER2MB_EXTERNAL_MEMSET
#if MINLZ_BETTER2MB_ZERO_REP_STOSQ
        "xorl %%eax, %%eax\n"
        "movl $0x00012000, %%ecx\n"
        "rep stosq\n"
#else
        "pxor %%xmm0, %%xmm0\n"
        "movl $0x00001200, %%ecx\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_zero_loop_encodeBetterBlockAsm2MB:\n"
        "movdqa %%xmm0, 0(%%rdi)\n"
        "movdqa %%xmm0, 16(%%rdi)\n"
        "movdqa %%xmm0, 32(%%rdi)\n"
        "movdqa %%xmm0, 48(%%rdi)\n"
        "movdqa %%xmm0, 64(%%rdi)\n"
        "movdqa %%xmm0, 80(%%rdi)\n"
        "movdqa %%xmm0, 96(%%rdi)\n"
        "movdqa %%xmm0, 112(%%rdi)\n"
        "addq $128, %%rdi\n"
        "decl %%ecx\n"
        "jnz .Lminlz_better2mb_inline_%=_zero_loop_encodeBetterBlockAsm2MB\n"
#endif
#endif
        "movq 24(%%rsp), %%rcx\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "xorl %%r14d, %%r14d\n"
#else
        "movl $0x00000000, 12(%%rsp)\n"
#endif
        "movq 40(%%rsp), %%rax\n"
        "leaq -17(%%rax), %%rdx\n"
        "leaq -17(%%rax), %%rbx\n"
        "movl %%ebx, 8(%%rsp)\n"
        "shrq $0x05, %%rax\n"
        "subl %%eax, %%edx\n"
        "leaq (%%rcx,%%rdx,1), %%rdx\n"
        "movq %%rdx, (%%rsp)\n"
        "movl $0x00000001, %%eax\n"
        "movl %%eax, 16(%%rsp)\n"
        "movq 32(%%rsp), %%rdx\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_search_loop_encodeBetterBlockAsm2MB:\n"
#if !MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE
#if MINLZ_BETTER2MB_TABLE_RBP
        "movq %%rbp, %%rbx\n"
#else
        "movq 48(%%rsp), %%rbx\n"
#endif
#endif
        "movl %%eax, %%esi\n"
        MINLZ_BETTER2MB_NEXT_EMIT_SUB("%%esi")
        "shrl $0x07, %%esi\n"
        "cmpl $0x63, %%esi\n"
        "jbe .Lminlz_better2mb_inline_%=_check_maxskip_ok_encodeBetterBlockAsm2MB\n"
        "leal 100(%%rax), %%esi\n"
        "jmp .Lminlz_better2mb_inline_%=_check_maxskip_cont_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_check_maxskip_ok_encodeBetterBlockAsm2MB:\n"
        "leal 1(%%rax,%%rsi,1), %%esi\n"
        ".Lminlz_better2mb_inline_%=_check_maxskip_cont_encodeBetterBlockAsm2MB:\n"
        "cmpl 8(%%rsp), %%esi\n"
        "jae .Lminlz_better2mb_inline_%=_emit_remainder_encodeBetterBlockAsm2MB\n"
        "movq (%%rdx,%%rax,1), %%rdi\n"
#if MINLZ_BETTER2MB_REG_NEXT_SEARCH
        "movl %%esi, %%r15d\n"
#else
        "movl %%esi, 20(%%rsp)\n"
#endif
        "movq %%rdi, %%r10\n"
        "movq %%rdi, %%r11\n"
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r10")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
        "movq %%xmm6, %%r9\n"
        "imulq %%r9, %%r10\n"
#else
        "movq $" MINLZ_BETTER2MB_LONG_HASH_MULT ", %%r9\n"
        "imulq %%r9, %%r10\n"
#endif
        "shrq $0x2f, %%r10\n"
        MINLZ_BETTER2MB_SHORT_HASH("%%r11", "%%r11d")
#if MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE
        "movl (%%rbp,%%r10,4), %%esi\n"
        "movq (%%rdx,%%rsi,1), %%rbx\n"
        "movl 524288(%%rbp,%%r11,4), %%r8d\n"
        "movl %%eax, (%%rbp,%%r10,4)\n"
        "movl %%eax, 524288(%%rbp,%%r11,4)\n"
        "cmpq %%rdi, %%rbx\n"
#else
        "movl (%%rbx,%%r10,4), %%esi\n"
        "movl 524288(%%rbx,%%r11,4), %%r8d\n"
        "movl %%eax, (%%rbx,%%r10,4)\n"
        "movl %%eax, 524288(%%rbx,%%r11,4)\n"
        "movq (%%rdx,%%rsi,1), %%r10\n"
        "cmpq %%rdi, %%r10\n"
#endif
        "je .Lminlz_better2mb_inline_%=_candidate_match_encodeBetterBlockAsm2MB\n"
        "movq (%%rdx,%%r8,1), %%r11\n"
#if MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE
        "movl %%eax, %%r12d\n"
        "subl 16(%%rsp), %%r12d\n"
        "movq %%rdi, %%r13\n"
        "shrq $0x08, %%r13\n"
        "cmpl %%r13d, 1(%%rdx,%%r12,1)\n"
#else
        "cmpq %%rdi, %%r11\n"
        "movl %%eax, %%r12d\n"
        "subl 16(%%rsp), %%r12d\n"
        "movq (%%rdx,%%r12,1), %%r12\n"
        "movq $0x000000ffffffff00, %%r13\n"
        "xorq %%rdi, %%r12\n"
        "testq %%r13, %%r12\n"
#endif
        "jne .Lminlz_better2mb_inline_%=_no_repeat_found_encodeBetterBlockAsm2MB\n"
        "leal 1(%%rax), %%ebx\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%esi")
        "movl %%ebx, %%edi\n"
        "subl 16(%%rsp), %%edi\n"
        "jz .Lminlz_better2mb_inline_%=_repeat_extend_back_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_extend_back_loop_encodeBetterBlockAsm2MB:\n"
        "cmpl %%esi, %%ebx\n"
        "jbe .Lminlz_better2mb_inline_%=_repeat_extend_back_end_encodeBetterBlockAsm2MB\n"
        "movb -1(%%rdx,%%rdi,1), %%r8b\n"
        "movb -1(%%rdx,%%rbx,1), %%r9b\n"
        "cmpb %%r9b, %%r8b\n"
        "jne .Lminlz_better2mb_inline_%=_repeat_extend_back_end_encodeBetterBlockAsm2MB\n"
        "leal -1(%%rbx), %%ebx\n"
        "decl %%edi\n"
        "jnz .Lminlz_better2mb_inline_%=_repeat_extend_back_loop_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_extend_back_end_encodeBetterBlockAsm2MB:\n"
        "movl %%ebx, %%esi\n"
        MINLZ_BETTER2MB_NEXT_EMIT_SUB("%%esi")
        "leaq 4(%%rcx,%%rsi,1), %%rsi\n"
        "cmpq (%%rsp), %%rsi\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_dst_size_check_encodeBetterBlockAsm2MB\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better2mb_inline_%=_epilogue\n"
        ".Lminlz_better2mb_inline_%=_repeat_dst_size_check_encodeBetterBlockAsm2MB:\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%esi")
        "cmpl %%ebx, %%esi\n"
        "je .Lminlz_better2mb_inline_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm2MB\n"
        "movl %%ebx, %%edi\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%ebx")
        "leaq (%%rdx,%%rsi,1), %%r8\n"
        "subl %%esi, %%edi\n"
        "leal -1(%%rdi), %%esi\n"
        "cmpl $0x1d, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_one_byte_repeat_emit_encodeBetterBlockAsm2MB\n"
        "subl $0x1d, %%esi\n"
        "cmpl $0x00000100, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_two_bytes_repeat_emit_encodeBetterBlockAsm2MB\n"
        "cmpl $0x00010000, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_three_bytes_repeat_emit_encodeBetterBlockAsm2MB\n"
        "movl %%esi, %%r9d\n"
        "shrl $0x10, %%r9d\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "movb %%r9b, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%esi\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_three_bytes_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%esi\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_two_bytes_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addl $0x1d, %%esi\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_memmove_midrepeat_emit_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_one_byte_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "shlb $0x03, %%sil\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x10, %%rdi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_8through16\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_8through16:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_midrepeat_emit_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_mid_end_copy_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_mid_end_copy_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_long_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movq %%rdi, %%r10\n"
        "shrq $0x05, %%r10\n"
        "movq %%rcx, %%r9\n"
        "andl $0x0000001f, %%r9d\n"
        "movq $0x00000040, %%r11\n"
        "subq %%r9, %%r11\n"
        "decq %%r10\n"
        "ja .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r11,1), %%r9\n"
        "leaq -32(%%rcx,%%r11,1), %%r12\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm2MBlarge_big_loop_back:\n"
        "movdqu (%%r9), %%xmm4\n"
        "movdqu 16(%%r9), %%xmm5\n"
        "movdqa %%xmm4, (%%r12)\n"
        "movdqa %%xmm5, 16(%%r12)\n"
        "addq $0x20, %%r12\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%r11\n"
        "decq %%r10\n"
        "jna .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm2MBlarge_big_loop_back\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r11,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r11,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r11,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r11,1)\n"
        "addq $0x20, %%r11\n"
        "cmpq %%r11, %%rdi\n"
        "jae .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        "movq %%rsi, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm2MB:\n"
        "addl $0x05, %%eax\n"
        "movl %%eax, %%esi\n"
        "subl 16(%%rsp), %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%eax, %%edi\n"
        "leaq (%%rdx,%%rax,1), %%r8\n"
        "leaq (%%rdx,%%rsi,1), %%rsi\n"
        "xorl %%r10d, %%r10d\n"
        "jmp .Lminlz_better2mb_inline_%=_matchlen_loop_16_entry_repeat_extend_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_matchlen_loopback_16_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "movq (%%r8,%%r10,1), %%r9\n"
        "movq 8(%%r8,%%r10,1), %%r11\n"
        "xorq (%%rsi,%%r10,1), %%r9\n"
        "jnz .Lminlz_better2mb_inline_%=_matchlen_bsf_8_repeat_extend_encodeBetterBlockAsm2MB\n"
        "xorq 8(%%rsi,%%r10,1), %%r11\n"
        "jnz .Lminlz_better2mb_inline_%=_matchlen_bsf_16repeat_extend_encodeBetterBlockAsm2MB\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r10), %%r10d\n"
        ".Lminlz_better2mb_inline_%=_matchlen_loop_16_entry_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_better2mb_inline_%=_matchlen_loopback_16_repeat_extend_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_matchlen_match8_repeat_extend_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_matchlen_bsf_16repeat_extend_encodeBetterBlockAsm2MB:\n"
        "tzcntq %%r11, %%r11\n"
        "sarq $0x03, %%r11\n"
        "leal 8(%%r10,%%r11,1), %%r10d\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match8_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_better2mb_inline_%=_matchlen_match4_repeat_extend_encodeBetterBlockAsm2MB\n"
        "movq (%%r8,%%r10,1), %%r9\n"
        "xorq (%%rsi,%%r10,1), %%r9\n"
        "jnz .Lminlz_better2mb_inline_%=_matchlen_bsf_8_repeat_extend_encodeBetterBlockAsm2MB\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r10), %%r10d\n"
        "jmp .Lminlz_better2mb_inline_%=_matchlen_match4_repeat_extend_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_matchlen_bsf_8_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "tzcntq %%r9, %%r9\n"
        "sarq $0x03, %%r9\n"
        "addl %%r9d, %%r10d\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match4_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_better2mb_inline_%=_matchlen_match2_repeat_extend_encodeBetterBlockAsm2MB\n"
        "movl (%%r8,%%r10,1), %%r9d\n"
        "cmpl %%r9d, (%%rsi,%%r10,1)\n"
        "jne .Lminlz_better2mb_inline_%=_matchlen_match2_repeat_extend_encodeBetterBlockAsm2MB\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r10), %%r10d\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match2_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_better2mb_inline_%=_matchlen_match1_repeat_extend_encodeBetterBlockAsm2MB\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB\n"
        "movw (%%r8,%%r10,1), %%r9w\n"
        "cmpw %%r9w, (%%rsi,%%r10,1)\n"
        "jne .Lminlz_better2mb_inline_%=_matchlen_match1_repeat_extend_encodeBetterBlockAsm2MB\n"
        "leal 2(%%r10), %%r10d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match1_repeat_extend_encodeBetterBlockAsm2MB:\n"
        "movb (%%r8,%%r10,1), %%r9b\n"
        "cmpb %%r9b, (%%rsi,%%r10,1)\n"
        "jne .Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB\n"
        "leal 1(%%r10), %%r10d\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_repeat_extend_forward_end_encodeBetterBlockAsm2MB:\n"
        "addl %%r10d, %%eax\n"
        "movl %%eax, %%esi\n"
        "subl %%ebx, %%esi\n"
        "leal -1(%%rsi), %%edi\n"
        "cmpl $0x1d, %%esi\n"
        "jbe .Lminlz_better2mb_inline_%=_repeat_one_match_repeat_encodeBetterBlockAsm2MB\n"
        "leal -30(%%rsi), %%edi\n"
        "cmpl $0x0000011e, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_two_match_repeat_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001001e, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_three_match_repeat_encodeBetterBlockAsm2MB\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%edi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_end_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_three_match_repeat_encodeBetterBlockAsm2MB:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%di, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_end_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_two_match_repeat_encodeBetterBlockAsm2MB:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%dil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_end_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_one_match_repeat_encodeBetterBlockAsm2MB:\n"
        "xorl %%edi, %%edi\n"
        "leal -4(%%rdi,%%rsi,8), %%edi\n"
        "movb %%dil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_repeat_end_emit_encodeBetterBlockAsm2MB:\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%eax")
        "cmpl 8(%%rsp), %%eax\n"
        "jae .Lminlz_better2mb_inline_%=_emit_remainder_encodeBetterBlockAsm2MB\n"
        "movq 48(%%rsp), %%rsi\n"
#if !MINLZ_BETTER2MB_HASH_CONST_STACK && !MINLZ_BETTER2MB_HASH_CONST_RBP && \
    !MINLZ_BETTER2MB_HASH_CONST_XMM
        "movq $" MINLZ_BETTER2MB_LONG_HASH_MULT ", %%rdi\n"
#endif
        "movq $0x9e3779b1, %%r8\n"
        "leaq 1(%%rbx), %%rbx\n"
        "leaq -2(%%rax), %%r9\n"
        ".Lminlz_better2mb_inline_%=_repeat_index_loop_encodeBetterBlockAsm2MB:\n"
        "cmpq %%r9, %%rbx\n"
        "jae .Lminlz_better2mb_inline_%=_search_loop_encodeBetterBlockAsm2MB\n"
        "movq (%%rdx,%%rbx,1), %%r10\n"
        "movq 1(%%rdx,%%rbx,1), %%r11\n"
        "movq (%%rdx,%%r9,1), %%r12\n"
        "movq 1(%%rdx,%%r9,1), %%r13\n"
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r10")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "movq %%xmm6, %%r15\n"
        "imulq %%r15, %%r10\n"
#else
        "movq %%xmm6, %%r14\n"
        "imulq %%r14, %%r10\n"
#endif
#else
        "imulq %%rdi, %%r10\n"
#endif
        "shrq $0x2f, %%r10\n"
        MINLZ_BETTER2MB_SHORT_HASH("%%r11", "%%r11d")
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r12")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r12\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r12\n"
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "movq %%xmm6, %%r15\n"
        "imulq %%r15, %%r12\n"
#else
        "movq %%xmm6, %%r14\n"
        "imulq %%r14, %%r12\n"
#endif
#else
        "imulq %%rdi, %%r12\n"
#endif
        "shrq $0x2f, %%r12\n"
        MINLZ_BETTER2MB_SHORT_HASH("%%r13", "%%r13d")
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "leaq 1(%%rbx), %%r15\n"
#else
        "leaq 1(%%rbx), %%r14\n"
#endif
#if !MINLZ_BETTER2MB_REG_NEXT_SEARCH && !MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "leaq 1(%%r9), %%r15\n"
#endif
        "movl %%ebx, (%%rsi,%%r10,4)\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "movl %%r15d, 524288(%%rsi,%%r11,4)\n"
#else
        "movl %%r14d, 524288(%%rsi,%%r11,4)\n"
#endif
        "movl %%r9d, (%%rsi,%%r12,4)\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "leaq 1(%%r9), %%r15\n"
        "movl %%r15d, 524288(%%rsi,%%r13,4)\n"
#elif MINLZ_BETTER2MB_REG_NEXT_SEARCH
        "leaq 1(%%r9), %%r14\n"
        "movl %%r14d, 524288(%%rsi,%%r13,4)\n"
#else
        "movl %%r15d, 524288(%%rsi,%%r13,4)\n"
#endif
        "addq $0x02, %%rbx\n"
        "subq $0x02, %%r9\n"
        "jmp .Lminlz_better2mb_inline_%=_repeat_index_loop_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_no_repeat_found_encodeBetterBlockAsm2MB:\n"
#if MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE
        "cmpl %%edi, %%ebx\n"
#else
        "cmpl %%edi, %%r10d\n"
#endif
        "je .Lminlz_better2mb_inline_%=_candidate_match_encodeBetterBlockAsm2MB\n"
        "cmpl %%edi, %%r11d\n"
        "je .Lminlz_better2mb_inline_%=_candidateS_match_encodeBetterBlockAsm2MB\n"
#if MINLZ_BETTER2MB_REG_NEXT_SEARCH
        "movl %%r15d, %%eax\n"
#else
        "movl 20(%%rsp), %%eax\n"
#endif
        "jmp .Lminlz_better2mb_inline_%=_search_loop_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_candidateS_match_encodeBetterBlockAsm2MB:\n"
#if MINLZ_BETTER2MB_FOLDED_SEARCH_ACTIVE
        "movq %%rbp, %%rbx\n"
#endif
        "shrq $0x08, %%rdi\n"
        "movq %%rdi, %%r10\n"
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r10")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
        "movq %%xmm6, %%r9\n"
        "imulq %%r9, %%r10\n"
#else
        "imulq %%r9, %%r10\n"
#endif
        "shrq $0x2f, %%r10\n"
        "movl (%%rbx,%%r10,4), %%esi\n"
        "incl %%eax\n"
        "movl %%eax, (%%rbx,%%r10,4)\n"
        "cmpl %%edi, (%%rdx,%%rsi,1)\n"
        "je .Lminlz_better2mb_inline_%=_candidate_match_encodeBetterBlockAsm2MB\n"
        "decl %%eax\n"
        "movl %%r8d, %%esi\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_candidate_match_encodeBetterBlockAsm2MB:\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%ebx")
        "testl %%esi, %%esi\n"
        "jz .Lminlz_better2mb_inline_%=_match_extend_back_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_match_extend_back_loop_encodeBetterBlockAsm2MB:\n"
        "cmpl %%ebx, %%eax\n"
        "jbe .Lminlz_better2mb_inline_%=_match_extend_back_end_encodeBetterBlockAsm2MB\n"
        "movb -1(%%rdx,%%rsi,1), %%dil\n"
        "movb -1(%%rdx,%%rax,1), %%r8b\n"
        "cmpb %%r8b, %%dil\n"
        "jne .Lminlz_better2mb_inline_%=_match_extend_back_end_encodeBetterBlockAsm2MB\n"
        "leal -1(%%rax), %%eax\n"
        "decl %%esi\n"
        "jz .Lminlz_better2mb_inline_%=_match_extend_back_end_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_match_extend_back_loop_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_match_extend_back_end_encodeBetterBlockAsm2MB:\n"
        "movl %%eax, %%ebx\n"
        MINLZ_BETTER2MB_NEXT_EMIT_SUB("%%ebx")
        "leaq 4(%%rcx,%%rbx,1), %%rbx\n"
        "cmpq (%%rsp), %%rbx\n"
        "jb .Lminlz_better2mb_inline_%=_match_dst_size_check_encodeBetterBlockAsm2MB\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better2mb_inline_%=_epilogue\n"
        ".Lminlz_better2mb_inline_%=_match_dst_size_check_encodeBetterBlockAsm2MB:\n"
        "movl %%eax, %%ebx\n"
        "addl $0x04, %%eax\n"
        "addl $0x04, %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%eax, %%edi\n"
        "leaq (%%rdx,%%rax,1), %%r8\n"
        "leaq (%%rdx,%%rsi,1), %%r9\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_better2mb_inline_%=_matchlen_loop_16_entry_match_nolit_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_matchlen_loopback_16_match_nolit_encodeBetterBlockAsm2MB:\n"
        "movq (%%r8,%%r11,1), %%r10\n"
        "movq 8(%%r8,%%r11,1), %%r12\n"
        "xorq (%%r9,%%r11,1), %%r10\n"
        "jnz .Lminlz_better2mb_inline_%=_matchlen_bsf_8_match_nolit_encodeBetterBlockAsm2MB\n"
        "xorq 8(%%r9,%%r11,1), %%r12\n"
        "jnz .Lminlz_better2mb_inline_%=_matchlen_bsf_16match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_better2mb_inline_%=_matchlen_loop_16_entry_match_nolit_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_better2mb_inline_%=_matchlen_loopback_16_match_nolit_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_matchlen_match8_match_nolit_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_matchlen_bsf_16match_nolit_encodeBetterBlockAsm2MB:\n"
        "tzcntq %%r12, %%r12\n"
        "sarq $0x03, %%r12\n"
        "leal 8(%%r11,%%r12,1), %%r11d\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match8_match_nolit_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_better2mb_inline_%=_matchlen_match4_match_nolit_encodeBetterBlockAsm2MB\n"
        "movq (%%r8,%%r11,1), %%r10\n"
        "xorq (%%r9,%%r11,1), %%r10\n"
        "jnz .Lminlz_better2mb_inline_%=_matchlen_bsf_8_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_better2mb_inline_%=_matchlen_match4_match_nolit_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_matchlen_bsf_8_match_nolit_encodeBetterBlockAsm2MB:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "addl %%r10d, %%r11d\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match4_match_nolit_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_better2mb_inline_%=_matchlen_match2_match_nolit_encodeBetterBlockAsm2MB\n"
        "movl (%%r8,%%r11,1), %%r10d\n"
        "cmpl %%r10d, (%%r9,%%r11,1)\n"
        "jne .Lminlz_better2mb_inline_%=_matchlen_match2_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match2_match_nolit_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_better2mb_inline_%=_matchlen_match1_match_nolit_encodeBetterBlockAsm2MB\n"
        "jb .Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB\n"
        "movw (%%r8,%%r11,1), %%r10w\n"
        "cmpw %%r10w, (%%r9,%%r11,1)\n"
        "jne .Lminlz_better2mb_inline_%=_matchlen_match1_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_matchlen_match1_match_nolit_encodeBetterBlockAsm2MB:\n"
        "movb (%%r8,%%r11,1), %%r10b\n"
        "cmpb %%r10b, (%%r9,%%r11,1)\n"
        "jne .Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_match_nolit_end_encodeBetterBlockAsm2MB:\n"
        "movl %%eax, %%edi\n"
        "subl %%esi, %%edi\n"
        "cmpl $0x00, %%r11d\n"
        "ja .Lminlz_better2mb_inline_%=_match_length_ok_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001003f, %%edi\n"
        "jbe .Lminlz_better2mb_inline_%=_match_length_ok_encodeBetterBlockAsm2MB\n"
        "cmpl 16(%%rsp), %%edi\n"
        "je .Lminlz_better2mb_inline_%=_match_length_ok_encodeBetterBlockAsm2MB\n"
#if MINLZ_BETTER2MB_REG_NEXT_SEARCH
        "movl %%r15d, %%eax\n"
#else
        "movl 20(%%rsp), %%eax\n"
#endif
        "incl %%eax\n"
        "jmp .Lminlz_better2mb_inline_%=_search_loop_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_match_length_ok_encodeBetterBlockAsm2MB:\n"
        "movl %%edi, 16(%%rsp)\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%r8d")
        "movl %%ebx, %%esi\n"
        "subl %%r8d, %%esi\n"
        "jz .Lminlz_better2mb_inline_%=_match_emit_nolits_encodeBetterBlockAsm2MB\n"
        "cmpl $0x00000040, %%edi\n"
        "jl .Lminlz_better2mb_inline_%=_match_emit_lits_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001003f, %%edi\n"
        "ja .Lminlz_better2mb_inline_%=_match_emit_copy3_encodeBetterBlockAsm2MB\n"
        "cmpl $0x04, %%esi\n"
        "ja .Lminlz_better2mb_inline_%=_match_emit_lits_encodeBetterBlockAsm2MB\n"
        "movl (%%rdx,%%r8,1), %%r8d\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%eax")
        "xorq %%r9, %%r9\n"
        "subl $0x40, %%edi\n"
        "leal -11(%%r11), %%r10d\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%di, 1(%%rcx)\n"
        "cmpl $0x07, %%r11d\n"
        "cmovgel %%r10d, %%r9d\n"
        "movq $0x00000007, %%rdi\n"
        "cmovll %%r11d, %%edi\n"
        "leal -1(%%rsi,%%rdi,4), %%edi\n"
        "movl $0x00000003, %%r10d\n"
        "leal (%%r10,%%rdi,8), %%edi\n"
        "movb %%dil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "movl %%r8d, (%%rcx)\n"
        "addq %%rsi, %%rcx\n"
        "testl %%r9d, %%r9d\n"
        "jz .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        "leal -1(%%r9), %%esi\n"
        "cmpl $0x1d, %%r9d\n"
        "jbe .Lminlz_better2mb_inline_%=_repeat_one_match_emit_repeat_copy2_encodeBetterBlockAsm2MB\n"
        "leal -30(%%r9), %%esi\n"
        "cmpl $0x0000011e, %%r9d\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_two_match_emit_repeat_copy2_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001001e, %%r9d\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_three_match_emit_repeat_copy2_encodeBetterBlockAsm2MB\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_three_match_emit_repeat_copy2_encodeBetterBlockAsm2MB:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_two_match_emit_repeat_copy2_encodeBetterBlockAsm2MB:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_one_match_emit_repeat_copy2_encodeBetterBlockAsm2MB:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r9,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_match_emit_copy3_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x03, %%esi\n"
        "ja .Lminlz_better2mb_inline_%=_match_emit_lits_encodeBetterBlockAsm2MB\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%r8d")
        "movl (%%rdx,%%r8,1), %%r8d\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%eax")
        "leal -4(%%r11), %%r11d\n"
        "leal -65536(%%rdi), %%edi\n"
        "shll $0x0b, %%edi\n"
        "leal 7(%%rdi,%%rsi,8), %%edi\n"
        "cmpl $0x3c, %%r11d\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_copy3_0_match_emit_lits_encodeBetterBlockAsm2MB\n"
        "leal -60(%%r11), %%r9d\n"
        "cmpl $0x0000013c, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_emit_copy3_1_match_emit_lits_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001003c, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_emit_copy3_2_match_emit_lits_encodeBetterBlockAsm2MB\n"
        "addl $0x000007e0, %%edi\n"
        "movl %%edi, (%%rcx)\n"
        "movl %%r9d, 4(%%rcx)\n"
        "addq $0x07, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_emit_copy_litsencodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy3_2_match_emit_lits_encodeBetterBlockAsm2MB:\n"
        "addl $0x000007c0, %%edi\n"
        "movl %%edi, (%%rcx)\n"
        "movw %%r9w, 4(%%rcx)\n"
        "addq $0x06, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_emit_copy_litsencodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy3_1_match_emit_lits_encodeBetterBlockAsm2MB:\n"
        "addl $0x000007a0, %%edi\n"
        "movl %%edi, (%%rcx)\n"
        "movb %%r9b, 4(%%rcx)\n"
        "addq $0x05, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_emit_copy_litsencodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy3_0_match_emit_lits_encodeBetterBlockAsm2MB:\n"
        "shll $0x05, %%r11d\n"
        "orl %%r11d, %%edi\n"
        "movl %%edi, (%%rcx)\n"
        "addq $0x04, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_match_emit_copy_litsencodeBetterBlockAsm2MB:\n"
        "movl %%r8d, (%%rcx)\n"
        "addq %%rsi, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_match_emit_lits_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rdx,%%r8,1), %%r8\n"
        "leal -1(%%rsi), %%r9d\n"
        "cmpl $0x1d, %%r9d\n"
        "jb .Lminlz_better2mb_inline_%=_one_byte_match_emit_encodeBetterBlockAsm2MB\n"
        "subl $0x1d, %%r9d\n"
        "cmpl $0x00000100, %%r9d\n"
        "jb .Lminlz_better2mb_inline_%=_two_bytes_match_emit_encodeBetterBlockAsm2MB\n"
        "cmpl $0x00010000, %%r9d\n"
        "jb .Lminlz_better2mb_inline_%=_three_bytes_match_emit_encodeBetterBlockAsm2MB\n"
        "movl %%r9d, %%r10d\n"
        "shrl $0x10, %%r10d\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "movb %%r10b, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_match_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_three_bytes_match_emit_encodeBetterBlockAsm2MB:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_match_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_two_bytes_match_emit_encodeBetterBlockAsm2MB:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%r9b, 1(%%rcx)\n"
        "addl $0x1d, %%r9d\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%r9d\n"
        "jb .Lminlz_better2mb_inline_%=_memmove_midmatch_emit_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_match_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_one_byte_match_emit_encodeBetterBlockAsm2MB:\n"
        "shlb $0x03, %%r9b\n"
        "movb %%r9b, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x10, %%rsi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm2MB_memmove_move_8through16\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm2MB_memmove_move_8through16:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm2MB:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_emit_nolits_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_midmatch_emit_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_mid_end_copy_match_emit_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_mid_end_copy_match_emit_encodeBetterBlockAsm2MB:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_emit_nolits_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_long_match_emit_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movq %%rsi, %%r12\n"
        "shrq $0x05, %%r12\n"
        "movq %%rcx, %%r10\n"
        "andl $0x0000001f, %%r10d\n"
        "movq $0x00000040, %%r13\n"
        "subq %%r10, %%r13\n"
        "decq %%r12\n"
        "ja .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r13,1), %%r10\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "leaq -32(%%rcx,%%r13,1), %%r15\n"
#else
        "leaq -32(%%rcx,%%r13,1), %%r14\n"
#endif
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm2MBlarge_big_loop_back:\n"
        "movdqu (%%r10), %%xmm4\n"
        "movdqu 16(%%r10), %%xmm5\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "movdqa %%xmm4, (%%r15)\n"
        "movdqa %%xmm5, 16(%%r15)\n"
        "addq $0x20, %%r15\n"
#else
        "movdqa %%xmm4, (%%r14)\n"
        "movdqa %%xmm5, 16(%%r14)\n"
        "addq $0x20, %%r14\n"
#endif
        "addq $0x20, %%r10\n"
        "addq $0x20, %%r13\n"
        "decq %%r12\n"
        "jna .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm2MBlarge_big_loop_back\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r13,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r13,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r13,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r13,1)\n"
        "addq $0x20, %%r13\n"
        "cmpq %%r13, %%rsi\n"
        "jae .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        "movq %%r9, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_match_emit_nolits_encodeBetterBlockAsm2MB:\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%eax")
        "cmpl $0x0001003f, %%edi\n"
        "jbe .Lminlz_better2mb_inline_%=_two_byte_offset_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -4(%%r11), %%r11d\n"
        "leal -65536(%%rdi), %%esi\n"
        "shll $0x0b, %%esi\n"
        "addl $0x07, %%esi\n"
        "cmpl $0x3c, %%r11d\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_copy3_0_match_nolit_encodeBetterBlockAsm2MB_emit3\n"
        "leal -60(%%r11), %%edi\n"
        "cmpl $0x0000013c, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_emit_copy3_1_match_nolit_encodeBetterBlockAsm2MB_emit3\n"
        "cmpl $0x0001003c, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_emit_copy3_2_match_nolit_encodeBetterBlockAsm2MB_emit3\n"
        "addl $0x000007e0, %%esi\n"
        "movl %%esi, (%%rcx)\n"
        "movl %%edi, 4(%%rcx)\n"
        "addq $0x07, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy3_2_match_nolit_encodeBetterBlockAsm2MB_emit3:\n"
        "addl $0x000007c0, %%esi\n"
        "movl %%esi, (%%rcx)\n"
        "movw %%di, 4(%%rcx)\n"
        "addq $0x06, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy3_1_match_nolit_encodeBetterBlockAsm2MB_emit3:\n"
        "addl $0x000007a0, %%esi\n"
        "movl %%esi, (%%rcx)\n"
        "movb %%dil, 4(%%rcx)\n"
        "addq $0x05, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy3_0_match_nolit_encodeBetterBlockAsm2MB_emit3:\n"
        "shll $0x05, %%r11d\n"
        "orl %%r11d, %%esi\n"
        "movl %%esi, (%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_two_byte_offset_match_nolit_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x00000400, %%edi\n"
        "ja .Lminlz_better2mb_inline_%=_two_byte_match_nolit_encodeBetterBlockAsm2MB\n"
        "cmpl $0x00000013, %%r11d\n"
        "jae .Lminlz_better2mb_inline_%=_emit_one_longer_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -1(%%rdi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal -15(%%rsi,%%r11,4), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_one_longer_match_nolit_encodeBetterBlockAsm2MB:\n"
        "cmpl $0x00000112, %%r11d\n"
        "jae .Lminlz_better2mb_inline_%=_emit_copy1_repeat_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -1(%%rdi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 61(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "leal -18(%%r11), %%esi\n"
        "movb %%sil, 2(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy1_repeat_match_nolit_encodeBetterBlockAsm2MB:\n"
        "leal -1(%%rdi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 57(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "subl $0x12, %%r11d\n"
        "leal -1(%%r11), %%esi\n"
        "cmpl $0x1d, %%r11d\n"
        "jbe .Lminlz_better2mb_inline_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm2MB\n"
        "leal -30(%%r11), %%esi\n"
        "cmpl $0x0000011e, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001001e, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm2MB\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm2MB:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm2MB:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm2MB:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r11,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_two_byte_match_nolit_encodeBetterBlockAsm2MB:\n"
        "leal -64(%%rdi), %%edi\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%di, 1(%%rcx)\n"
        "cmpl $0x3c, %%r11d\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_copy2_0_match_nolit_encodeBetterBlockAsm2MB_emit2\n"
        "leal -60(%%r11), %%esi\n"
        "cmpl $0x0000013c, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_emit_copy2_1_match_nolit_encodeBetterBlockAsm2MB_emit2\n"
        "cmpl $0x0001003c, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_emit_copy2_2_match_nolit_encodeBetterBlockAsm2MB_emit2\n"
        "movb $0xfe, (%%rcx)\n"
        "movl %%esi, 3(%%rcx)\n"
        "addq $0x06, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy2_2_match_nolit_encodeBetterBlockAsm2MB_emit2:\n"
        "movb $0xfa, (%%rcx)\n"
        "movw %%si, 3(%%rcx)\n"
        "addq $0x05, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy2_1_match_nolit_encodeBetterBlockAsm2MB_emit2:\n"
        "movb $0xf6, (%%rcx)\n"
        "movb %%sil, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_copy2_0_match_nolit_encodeBetterBlockAsm2MB_emit2:\n"
        "movl $0x00000002, %%esi\n"
        "leal (%%rsi,%%r11,4), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%esi")
        "cmpl %%ebx, %%esi\n"
        "je .Lminlz_better2mb_inline_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        "movl %%ebx, %%edi\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%ebx")
        "leaq (%%rdx,%%rsi,1), %%r8\n"
        "subl %%esi, %%edi\n"
        "leal -1(%%rdi), %%esi\n"
        "cmpl $0x1d, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_one_byte_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        "subl $0x1d, %%esi\n"
        "cmpl $0x00000100, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_two_bytes_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        "cmpl $0x00010000, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_three_bytes_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        "movl %%esi, %%r9d\n"
        "shrl $0x10, %%r9d\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "movb %%r9b, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%esi\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_three_bytes_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%esi\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_two_bytes_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addl $0x1d, %%esi\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%esi\n"
        "jb .Lminlz_better2mb_inline_%=_memmove_midmatch_emit_repeat_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_one_byte_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "shlb $0x03, %%sil\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x10, %%rdi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_8through16\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_8through16:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_midmatch_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_mid_end_copy_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_mid_end_copy_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movq %%rdi, %%r10\n"
        "shrq $0x05, %%r10\n"
        "movq %%rcx, %%r9\n"
        "andl $0x0000001f, %%r9d\n"
        "movq $0x00000040, %%r12\n"
        "subq %%r9, %%r12\n"
        "decq %%r10\n"
        "ja .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r12,1), %%r9\n"
        "leaq -32(%%rcx,%%r12,1), %%r13\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MBlarge_big_loop_back:\n"
        "movdqu (%%r9), %%xmm4\n"
        "movdqu 16(%%r9), %%xmm5\n"
        "movdqa %%xmm4, (%%r13)\n"
        "movdqa %%xmm5, 16(%%r13)\n"
        "addq $0x20, %%r13\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%r12\n"
        "decq %%r10\n"
        "jna .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MBlarge_big_loop_back\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r12,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r12,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r12,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r12,1)\n"
        "addq $0x20, %%r12\n"
        "cmpq %%r12, %%rdi\n"
        "jae .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        "movq %%rsi, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm2MB:\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%eax")
        "leal -1(%%r11), %%esi\n"
        "cmpl $0x1d, %%r11d\n"
        "jbe .Lminlz_better2mb_inline_%=_repeat_one_match_nolit_repeat_encodeBetterBlockAsm2MB\n"
        "leal -30(%%r11), %%esi\n"
        "cmpl $0x0000011e, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_two_match_nolit_repeat_encodeBetterBlockAsm2MB\n"
        "cmpl $0x0001001e, %%r11d\n"
        "jb .Lminlz_better2mb_inline_%=_repeat_three_match_nolit_repeat_encodeBetterBlockAsm2MB\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_three_match_nolit_repeat_encodeBetterBlockAsm2MB:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_two_match_nolit_repeat_encodeBetterBlockAsm2MB:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_repeat_one_match_nolit_repeat_encodeBetterBlockAsm2MB:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r11,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm2MB:\n"
        "cmpl 8(%%rsp), %%eax\n"
        "jae .Lminlz_better2mb_inline_%=_emit_remainder_encodeBetterBlockAsm2MB\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_better2mb_inline_%=_match_nolit_dst_ok_encodeBetterBlockAsm2MB\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better2mb_inline_%=_epilogue\n"
        ".Lminlz_better2mb_inline_%=_match_nolit_dst_ok_encodeBetterBlockAsm2MB:\n"
        "movq 48(%%rsp), %%rsi\n"
#if MINLZ_BETTER2MB_HASH_CONST_STACK
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
#elif MINLZ_BETTER2MB_HASH_CONST_XMM
        "movq %%xmm6, %%rdi\n"
#else
        "movq $" MINLZ_BETTER2MB_LONG_HASH_MULT ", %%rdi\n"
#endif
        "leaq 1(%%rbx), %%rbx\n"
        "leaq -2(%%rax), %%r9\n"
        "movq (%%rdx,%%rbx,1), %%r10\n"
        "movq 1(%%rdx,%%rbx,1), %%r11\n"
        "movq (%%rdx,%%r9,1), %%r12\n"
        "movq 1(%%rdx,%%r9,1), %%r13\n"
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r10")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r10\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r10\n"
#else
        "imulq %%rdi, %%r10\n"
#endif
        "shrq $0x2f, %%r10\n"
        MINLZ_BETTER2MB_SHORT_HASH("%%r11", "%%r11d")
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r12")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r12\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r12\n"
#else
        "imulq %%rdi, %%r12\n"
#endif
        "shrq $0x2f, %%r12\n"
        MINLZ_BETTER2MB_SHORT_HASH("%%r13", "%%r13d")
        "leaq 1(%%rbx), %%r8\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "leaq 1(%%r9), %%r15\n"
#else
        "leaq 1(%%r9), %%r14\n"
#endif
        "movl %%ebx, (%%rsi,%%r10,4)\n"
        "movl %%r9d, (%%rsi,%%r12,4)\n"
        "leaq 1(%%r9,%%rbx,1), %%r10\n"
        "shrq $0x01, %%r10\n"
        "addq $0x01, %%rbx\n"
        "subq $0x01, %%r9\n"
        "movl %%r8d, 524288(%%rsi,%%r11,4)\n"
#if MINLZ_BETTER2MB_REG_NEXT_EMIT_R14
        "movl %%r15d, 524288(%%rsi,%%r13,4)\n"
#else
        "movl %%r14d, 524288(%%rsi,%%r13,4)\n"
#endif
        ".Lminlz_better2mb_inline_%=_index_loop_encodeBetterBlockAsm2MB:\n"
        "cmpq %%r9, %%r10\n"
        "jae .Lminlz_better2mb_inline_%=_search_loop_encodeBetterBlockAsm2MB\n"
        "movq (%%rdx,%%rbx,1), %%r8\n"
        "movq (%%rdx,%%r10,1), %%r11\n"
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r8")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r8\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r8\n"
#else
        "imulq %%rdi, %%r8\n"
#endif
        "shrq $0x2f, %%r8\n"
        MINLZ_BETTER2MB_LONG_HASH_PREP("%%r11")
#if MINLZ_BETTER2MB_HASH_CONST_STACK
        "imulq 56(%%rsp), %%r11\n"
#elif MINLZ_BETTER2MB_HASH_CONST_RBP
        "imulq %%rbp, %%r11\n"
#else
        "imulq %%rdi, %%r11\n"
#endif
        "shrq $0x2f, %%r11\n"
        "movl %%ebx, (%%rsi,%%r8,4)\n"
        "movl %%r10d, (%%rsi,%%r11,4)\n"
        "addq $0x02, %%rbx\n"
        "addq $0x02, %%r10\n"
        "jmp .Lminlz_better2mb_inline_%=_index_loop_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "movq 40(%%rsp), %%rax\n"
        MINLZ_BETTER2MB_NEXT_EMIT_SUB("%%eax")
        "leaq 4(%%rcx,%%rax,1), %%rax\n"
        "cmpq (%%rsp), %%rax\n"
        "jb .Lminlz_better2mb_inline_%=_emit_remainder_ok_encodeBetterBlockAsm2MB\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better2mb_inline_%=_epilogue\n"
        ".Lminlz_better2mb_inline_%=_emit_remainder_ok_encodeBetterBlockAsm2MB:\n"
        "movq 40(%%rsp), %%rax\n"
        MINLZ_BETTER2MB_NEXT_EMIT_READ("%%ebx")
        "cmpl %%eax, %%ebx\n"
        "je .Lminlz_better2mb_inline_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm2MB\n"
        "movl %%eax, %%esi\n"
        MINLZ_BETTER2MB_NEXT_EMIT_WRITE("%%eax")
        "leaq (%%rdx,%%rbx,1), %%rax\n"
        "subl %%ebx, %%esi\n"
        "leal -1(%%rsi), %%edx\n"
        "cmpl $0x1d, %%edx\n"
        "jb .Lminlz_better2mb_inline_%=_one_byte_emit_remainder_encodeBetterBlockAsm2MB\n"
        "subl $0x1d, %%edx\n"
        "cmpl $0x00000100, %%edx\n"
        "jb .Lminlz_better2mb_inline_%=_two_bytes_emit_remainder_encodeBetterBlockAsm2MB\n"
        "cmpl $0x00010000, %%edx\n"
        "jb .Lminlz_better2mb_inline_%=_three_bytes_emit_remainder_encodeBetterBlockAsm2MB\n"
        "movl %%edx, %%ebx\n"
        "shrl $0x10, %%ebx\n"
        "movb $0xf8, (%%rcx)\n"
        "movw %%dx, 1(%%rcx)\n"
        "movb %%bl, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "addl $0x1d, %%edx\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_three_bytes_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%dx, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%edx\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_two_bytes_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%dl, 1(%%rcx)\n"
        "addl $0x1d, %%edx\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%edx\n"
        "jb .Lminlz_better2mb_inline_%=_memmove_midemit_remainder_encodeBetterBlockAsm2MB\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_long_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_one_byte_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "shlb $0x03, %%dl\n"
        "movb %%dl, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rsi,1), %%rdx\n"
        "movl %%esi, %%ebx\n"
        "cmpq $0x03, %%rbx\n"
        "jb .Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_1or2\n"
        "je .Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_3\n"
        "cmpq $0x08, %%rbx\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_4through8\n"
        "cmpq $0x10, %%rbx\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_8through16\n"
        "cmpq $0x20, %%rbx\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_1or2:\n"
        "movb (%%rax), %%sil\n"
        "movb -1(%%rax,%%rbx,1), %%al\n"
        "movb %%sil, (%%rcx)\n"
        "movb %%al, -1(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_3:\n"
        "movw (%%rax), %%si\n"
        "movb 2(%%rax), %%al\n"
        "movw %%si, (%%rcx)\n"
        "movb %%al, 2(%%rcx)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_4through8:\n"
        "movl (%%rax), %%esi\n"
        "movl -4(%%rax,%%rbx,1), %%eax\n"
        "movl %%esi, (%%rcx)\n"
        "movl %%eax, -4(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_8through16:\n"
        "movq (%%rax), %%rsi\n"
        "movq -8(%%rax,%%rbx,1), %%rax\n"
        "movq %%rsi, (%%rcx)\n"
        "movq %%rax, -8(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu 16(%%rax), %%xmm1\n"
        "movdqu -32(%%rax,%%rbx,1), %%xmm2\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rbx,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rbx,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "movq %%rdx, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_midemit_remainder_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rsi,1), %%rdx\n"
        "movl %%esi, %%ebx\n"
        "cmpq $0x20, %%rbx\n"
        "jbe .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_17through32\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_33through64\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_17through32:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better2mb_inline_%=_memmove_mid_end_copy_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm2MB_memmove_move_33through64:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu 16(%%rax), %%xmm1\n"
        "movdqu -32(%%rax,%%rbx,1), %%xmm2\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rbx,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rbx,1)\n"
        ".Lminlz_better2mb_inline_%=_memmove_mid_end_copy_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "movq %%rdx, %%rcx\n"
        "jmp .Lminlz_better2mb_inline_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm2MB\n"
        ".Lminlz_better2mb_inline_%=_memmove_long_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "leaq (%%rcx,%%rsi,1), %%rdx\n"
        "movl %%esi, %%ebx\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu 16(%%rax), %%xmm1\n"
        "movdqu -32(%%rax,%%rbx,1), %%xmm2\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm3\n"
        "movq %%rbx, %%rdi\n"
        "shrq $0x05, %%rdi\n"
        "movq %%rcx, %%rsi\n"
        "andl $0x0000001f, %%esi\n"
        "movq $0x00000040, %%r8\n"
        "subq %%rsi, %%r8\n"
        "decq %%rdi\n"
        "ja .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "leaq -32(%%rax,%%r8,1), %%rsi\n"
        "leaq -32(%%rcx,%%r8,1), %%r9\n"
        ".p2align 4\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm2MBlarge_big_loop_back:\n"
        "movdqu (%%rsi), %%xmm4\n"
        "movdqu 16(%%rsi), %%xmm5\n"
        "movdqa %%xmm4, (%%r9)\n"
        "movdqa %%xmm5, 16(%%r9)\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%rsi\n"
        "addq $0x20, %%r8\n"
        "decq %%rdi\n"
        "jna .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm2MBlarge_big_loop_back\n"
        ".Lminlz_better2mb_inline_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32:\n"
        "movdqu -32(%%rax,%%r8,1), %%xmm4\n"
        "movdqu -16(%%rax,%%r8,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r8,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r8,1)\n"
        "addq $0x20, %%r8\n"
        "cmpq %%r8, %%rbx\n"
        "jae .Lminlz_better2mb_inline_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm2MBlarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rbx,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rbx,1)\n"
        "movq %%rdx, %%rcx\n"
        ".Lminlz_better2mb_inline_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm2MB:\n"
        "movq 24(%%rsp), %%rax\n"
        "subq %%rax, %%rcx\n"
        "movq %%rcx, %%rax\n"
        "jmp .Lminlz_better2mb_inline_%=_epilogue\n"
        ".Lminlz_better2mb_inline_%=_epilogue:\n"
        "addq $64, %%rsp\n"
#if MINLZ_BETTER2MB_TABLE_RBP
        "popq %%rbp\n"
#endif
        "popq %%r15\n"
#if !MINLZ_BETTER2MB_REGREWRITE_ANY
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%rbx\n"
#endif
        : "=&a"(ret)
#if MINLZ_ENABLE_BETTER2MB_REGREWRITE_V2_ASM
        : [ctx] "r"(&ctx)
#else
        : [dst] "r"(dst), [src] "r"(src), [src_len] "r"(src_len), [tmp] "r"(tmp)
#endif
        :
#if MINLZ_BETTER2MB_REGREWRITE_ANY
          "rbx",
#if MINLZ_BETTER2MB_HASH_CONST_RBP
          "rbp",
#endif
          "r12", "r13", "r14",
#endif
          "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "memory", "cc");
    return ret;
#else
    (void)dst; (void)src; (void)src_len; (void)tmp;
    return 0;
#endif
}
#endif

#if MINLZ_ENABLE_BETTER16K_INLINE_ASM
static MINLZ_ALWAYS_INLINE size_t minlz_encode_better_block_16k_inline_asm(uint8_t *dst,
                                                                          const uint8_t *src,
                                                                          size_t src_len,
                                                                          uint8_t *tmp,
                                                                          size_t direct_limit) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    size_t ret;
    /* The repeat-table loop writes r15. Expose it to the compiler. */
    register size_t repeat_index __asm__("r15");
    __asm__ volatile(
        "pushq %%rbx\n"
        "pushq %%r12\n"
        "pushq %%r13\n"
        "pushq %%r14\n"
        "subq $64, %%rsp\n"
        "movq %[dst], 24(%%rsp)\n"
        "movq %[src], 32(%%rsp)\n"
        "movq %[src_len], 40(%%rsp)\n"
        "movq %[tmp], 48(%%rsp)\n"
        "xorl %k[repeat_index], %k[repeat_index]\n"
        "movq 48(%%rsp), %%rdi\n"
        "xorl %%eax, %%eax\n"
        "movl $0x00001200, %%ecx\n"
        "rep stosq\n"
        "movq 24(%%rsp), %%rcx\n"
        "movl $0x00000000, 12(%%rsp)\n"
        "movq 40(%%rsp), %%rax\n"
        "leaq -11(%%rax), %%rdx\n"
        "leaq -8(%%rax), %%rbx\n"
        "movl %%ebx, 8(%%rsp)\n"
        "shrq $0x05, %%rax\n"
        "subl %%eax, %%edx\n"
        "leaq (%%rcx,%%rdx,1), %%rdx\n"
        "movq %%rdx, (%%rsp)\n"
        "movl $0x00000001, %%eax\n"
        "movl %%eax, 16(%%rsp)\n"
        "movq 32(%%rsp), %%rdx\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_search_loop_encodeBetterBlockAsm16K:\n"
#if MINLZ_BETTER16K_REUSE_HASH1
        "movq $0x0000cf1bbcdcbf9b, %%r9\n"
        ".Lminlz_better16k_%=_search_reuse_hash1:\n"
#endif
        "movq 48(%%rsp), %%rbx\n"
        "movl %%eax, %%esi\n"
        "subl 12(%%rsp), %%esi\n"
        "shrl $0x06, %%esi\n"
        "leal 1(%%rax,%%rsi,1), %%esi\n"
        "cmpl 8(%%rsp), %%esi\n"
        "jae .Lminlz_better16k_%=_emit_remainder_encodeBetterBlockAsm16K\n"
        "movq (%%rdx,%%rax,1), %%rdi\n"
        "movl %%esi, 20(%%rsp)\n"
#if !MINLZ_BETTER16K_REUSE_HASH1
        "movq $0x0000cf1bbcdcbf9b, %%r9\n"
#endif
#if !MINLZ_BETTER16K_HASH2_32BIT
        "movq $0x9e3779b1, %%rsi\n"
#endif
        "movq %%rdi, %%r10\n"
        "movq %%rdi, %%r11\n"
        "shlq $0x10, %%r10\n"
        "imulq %%r9, %%r10\n"
        "shrq $0x32, %%r10\n"
#if MINLZ_BETTER16K_HASH2_32BIT
        "imull $-1640531535, %%r11d, %%r11d\n"
        "shrl $0x15, %%r11d\n"
#else
        "shlq $0x20, %%r11\n"
        "imulq %%rsi, %%r11\n"
        "shrq $0x35, %%r11\n"
#endif
        "movzwl (%%rbx,%%r10,2), %%esi\n"
        "movzwl 32768(%%rbx,%%r11,2), %%r8d\n"
        "movw %%ax, (%%rbx,%%r10,2)\n"
        "movw %%ax, 32768(%%rbx,%%r11,2)\n"
        "movq (%%rdx,%%rsi,1), %%r10\n"
        "cmpq %%rdi, %%r10\n"
        "je .Lminlz_better16k_%=_candidate_match_encodeBetterBlockAsm16K\n"
        "movq (%%rdx,%%r8,1), %%r11\n"
        "cmpq %%rdi, %%r11\n"
        "movl %%eax, %%r12d\n"
        "subl 16(%%rsp), %%r12d\n"
        "movq (%%rdx,%%r12,1), %%r12\n"
        "movq $0x000000ffffffff00, %%r13\n"
        "xorq %%rdi, %%r12\n"
        "testq %%r13, %%r12\n"
        "jne .Lminlz_better16k_%=_no_repeat_found_encodeBetterBlockAsm16K\n"
        "leal 1(%%rax), %%ebx\n"
        "movl 12(%%rsp), %%esi\n"
        "movl %%ebx, %%edi\n"
        "subl 16(%%rsp), %%edi\n"
        "jz .Lminlz_better16k_%=_repeat_extend_back_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_extend_back_loop_encodeBetterBlockAsm16K:\n"
        "cmpl %%esi, %%ebx\n"
        "jbe .Lminlz_better16k_%=_repeat_extend_back_end_encodeBetterBlockAsm16K\n"
        "movb -1(%%rdx,%%rdi,1), %%r8b\n"
        "movb -1(%%rdx,%%rbx,1), %%r9b\n"
        "cmpb %%r9b, %%r8b\n"
        "jne .Lminlz_better16k_%=_repeat_extend_back_end_encodeBetterBlockAsm16K\n"
        "leal -1(%%rbx), %%ebx\n"
        "decl %%edi\n"
        "jnz .Lminlz_better16k_%=_repeat_extend_back_loop_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_extend_back_end_encodeBetterBlockAsm16K:\n"
        "movl %%ebx, %%esi\n"
        "subl 12(%%rsp), %%esi\n"
        "leaq 3(%%rcx,%%rsi,1), %%rsi\n"
        "cmpq (%%rsp), %%rsi\n"
        "jb .Lminlz_better16k_%=_repeat_dst_size_check_encodeBetterBlockAsm16K\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        ".Lminlz_better16k_%=_repeat_dst_size_check_encodeBetterBlockAsm16K:\n"
        "movl 12(%%rsp), %%esi\n"
        "cmpl %%ebx, %%esi\n"
        "je .Lminlz_better16k_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm16K\n"
        "movl %%ebx, %%edi\n"
        "movl %%ebx, 12(%%rsp)\n"
        "leaq (%%rdx,%%rsi,1), %%r8\n"
        "subl %%esi, %%edi\n"
        "leal -1(%%rdi), %%esi\n"
        "cmpl $0x1d, %%esi\n"
        "jb .Lminlz_better16k_%=_one_byte_repeat_emit_encodeBetterBlockAsm16K\n"
        "subl $0x1d, %%esi\n"
        "cmpl $0x00000100, %%esi\n"
        "jb .Lminlz_better16k_%=_two_bytes_repeat_emit_encodeBetterBlockAsm16K\n"
        "jb .Lminlz_better16k_%=_three_bytes_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_three_bytes_repeat_emit_encodeBetterBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%esi\n"
        "jmp .Lminlz_better16k_%=_memmove_long_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_two_bytes_repeat_emit_encodeBetterBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addl $0x1d, %%esi\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%esi\n"
        "jb .Lminlz_better16k_%=_memmove_midrepeat_emit_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_memmove_long_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_one_byte_repeat_emit_encodeBetterBlockAsm16K:\n"
        "shlb $0x03, %%sil\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x08, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_8\n"
        "cmpq $0x10, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_8:\n"
        "movq (%%r8), %%r9\n"
        "movq %%r9, (%%rcx)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_8through16:\n"
        "movq (%%r8), %%r9\n"
        "movq -8(%%r8,%%rdi,1), %%r8\n"
        "movq %%r9, (%%rcx)\n"
        "movq %%r8, -8(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_repeat_emit_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better16k_%=_memmove_end_copy_repeat_emit_encodeBetterBlockAsm16K:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better16k_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_midrepeat_emit_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_mid_end_copy_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_repeat_emit_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better16k_%=_memmove_mid_end_copy_repeat_emit_encodeBetterBlockAsm16K:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better16k_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_long_repeat_emit_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movq %%rdi, %%r10\n"
        "shrq $0x05, %%r10\n"
        "movq %%rcx, %%r9\n"
        "andl $0x0000001f, %%r9d\n"
        "movq $0x00000040, %%r11\n"
        "subq %%r9, %%r11\n"
        "decq %%r10\n"
        "ja .Lminlz_better16k_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r11,1), %%r9\n"
        "leaq -32(%%rcx,%%r11,1), %%r12\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%r9), %%xmm4\n"
        "movdqu 16(%%r9), %%xmm5\n"
        "movdqa %%xmm4, (%%r12)\n"
        "movdqa %%xmm5, 16(%%r12)\n"
        "addq $0x20, %%r12\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%r11\n"
        "decq %%r10\n"
        "jna .Lminlz_better16k_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r11,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r11,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r11,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r11,1)\n"
        "addq $0x20, %%r11\n"
        "cmpq %%r11, %%rdi\n"
        "jae .Lminlz_better16k_%=_emit_lit_memmove_long_repeat_emit_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        "movq %%rsi, %%rcx\n"
        ".Lminlz_better16k_%=_emit_literal_done_repeat_emit_encodeBetterBlockAsm16K:\n"
        "addl $0x05, %%eax\n"
        "movl %%eax, %%esi\n"
        "subl 16(%%rsp), %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%eax, %%edi\n"
        "leaq (%%rdx,%%rax,1), %%r8\n"
        "leaq (%%rdx,%%rsi,1), %%rsi\n"
        "xorl %%r10d, %%r10d\n"
        "jmp .Lminlz_better16k_%=_matchlen_loop_16_entry_repeat_extend_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_matchlen_loopback_16_repeat_extend_encodeBetterBlockAsm16K:\n"
        "movq (%%r8,%%r10,1), %%r9\n"
        "movq 8(%%r8,%%r10,1), %%r11\n"
        "xorq (%%rsi,%%r10,1), %%r9\n"
        "jnz .Lminlz_better16k_%=_matchlen_bsf_8_repeat_extend_encodeBetterBlockAsm16K\n"
        "xorq 8(%%rsi,%%r10,1), %%r11\n"
        "jnz .Lminlz_better16k_%=_matchlen_bsf_16repeat_extend_encodeBetterBlockAsm16K\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r10), %%r10d\n"
        ".Lminlz_better16k_%=_matchlen_loop_16_entry_repeat_extend_encodeBetterBlockAsm16K:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_better16k_%=_matchlen_loopback_16_repeat_extend_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_matchlen_match8_repeat_extend_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_matchlen_bsf_16repeat_extend_encodeBetterBlockAsm16K:\n"
        "tzcntq %%r11, %%r11\n"
        "sarq $0x03, %%r11\n"
        "leal 8(%%r10,%%r11,1), %%r10d\n"
        "jmp .Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_matchlen_match8_repeat_extend_encodeBetterBlockAsm16K:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_better16k_%=_matchlen_match4_repeat_extend_encodeBetterBlockAsm16K\n"
        "movq (%%r8,%%r10,1), %%r9\n"
        "xorq (%%rsi,%%r10,1), %%r9\n"
        "jnz .Lminlz_better16k_%=_matchlen_bsf_8_repeat_extend_encodeBetterBlockAsm16K\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r10), %%r10d\n"
        "jmp .Lminlz_better16k_%=_matchlen_match4_repeat_extend_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_matchlen_bsf_8_repeat_extend_encodeBetterBlockAsm16K:\n"
        "tzcntq %%r9, %%r9\n"
        "sarq $0x03, %%r9\n"
        "addl %%r9d, %%r10d\n"
        "jmp .Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_matchlen_match4_repeat_extend_encodeBetterBlockAsm16K:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_better16k_%=_matchlen_match2_repeat_extend_encodeBetterBlockAsm16K\n"
        "movl (%%r8,%%r10,1), %%r9d\n"
        "cmpl %%r9d, (%%rsi,%%r10,1)\n"
        "jne .Lminlz_better16k_%=_matchlen_match2_repeat_extend_encodeBetterBlockAsm16K\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r10), %%r10d\n"
        ".Lminlz_better16k_%=_matchlen_match2_repeat_extend_encodeBetterBlockAsm16K:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_better16k_%=_matchlen_match1_repeat_extend_encodeBetterBlockAsm16K\n"
        "jb .Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K\n"
        "movw (%%r8,%%r10,1), %%r9w\n"
        "cmpw %%r9w, (%%rsi,%%r10,1)\n"
        "jne .Lminlz_better16k_%=_matchlen_match1_repeat_extend_encodeBetterBlockAsm16K\n"
        "leal 2(%%r10), %%r10d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_matchlen_match1_repeat_extend_encodeBetterBlockAsm16K:\n"
        "movb (%%r8,%%r10,1), %%r9b\n"
        "cmpb %%r9b, (%%rsi,%%r10,1)\n"
        "jne .Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K\n"
        "leal 1(%%r10), %%r10d\n"
        "jmp .Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_repeat_extend_forward_end_encodeBetterBlockAsm16K:\n"
        "addl %%r10d, %%eax\n"
        "movl %%eax, %%esi\n"
        "subl %%ebx, %%esi\n"
        "movl 16(%%rsp), %%edi\n"
        "leal -1(%%rsi), %%edi\n"
        "cmpl $0x1d, %%esi\n"
        "jbe .Lminlz_better16k_%=_repeat_one_match_repeat_encodeBetterBlockAsm16K\n"
        "leal -30(%%rsi), %%edi\n"
        "cmpl $0x0000011e, %%esi\n"
        "jb .Lminlz_better16k_%=_repeat_two_match_repeat_encodeBetterBlockAsm16K\n"
        "cmpl $0x0001001e, %%esi\n"
        "jb .Lminlz_better16k_%=_repeat_three_match_repeat_encodeBetterBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%edi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better16k_%=_repeat_end_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_three_match_repeat_encodeBetterBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%di, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better16k_%=_repeat_end_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_two_match_repeat_encodeBetterBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%dil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better16k_%=_repeat_end_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_one_match_repeat_encodeBetterBlockAsm16K:\n"
        "xorl %%edi, %%edi\n"
        "leal -4(%%rdi,%%rsi,8), %%edi\n"
        "movb %%dil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        ".Lminlz_better16k_%=_repeat_end_emit_encodeBetterBlockAsm16K:\n"
        "movl %%eax, 12(%%rsp)\n"
        "cmpl 8(%%rsp), %%eax\n"
        "jae .Lminlz_better16k_%=_emit_remainder_encodeBetterBlockAsm16K\n"
        "movq 48(%%rsp), %%rsi\n"
        "movq $0x0000cf1bbcdcbf9b, %%rdi\n"
        "movq $0x9e3779b1, %%r8\n"
        "leaq 1(%%rbx), %%rbx\n"
        "leaq -2(%%rax), %%r9\n"
        ".Lminlz_better16k_%=_repeat_index_loop_encodeBetterBlockAsm16K:\n"
        "cmpq %%r9, %%rbx\n"
        "jae .Lminlz_better16k_%=_search_loop_encodeBetterBlockAsm16K\n"
        "movq (%%rdx,%%rbx,1), %%r10\n"
        "movq 1(%%rdx,%%rbx,1), %%r11\n"
        "movq (%%rdx,%%r9,1), %%r12\n"
        "movq 1(%%rdx,%%r9,1), %%r13\n"
        "shlq $0x10, %%r10\n"
        "imulq %%rdi, %%r10\n"
        "shrq $0x32, %%r10\n"
        "shlq $0x20, %%r11\n"
        "imulq %%r8, %%r11\n"
        "shrq $0x35, %%r11\n"
        "shlq $0x10, %%r12\n"
        "imulq %%rdi, %%r12\n"
        "shrq $0x32, %%r12\n"
        "shlq $0x20, %%r13\n"
        "imulq %%r8, %%r13\n"
        "shrq $0x35, %%r13\n"
        "leaq 1(%%rbx), %%r14\n"
        "leaq 1(%%r9), %[repeat_index]\n"
        "movw %%bx, (%%rsi,%%r10,2)\n"
        "movw %%r14w, 32768(%%rsi,%%r11,2)\n"
        "movw %%r9w, (%%rsi,%%r12,2)\n"
        "movw %w[repeat_index], 32768(%%rsi,%%r13,2)\n"
        "addq $0x02, %%rbx\n"
        "subq $0x02, %%r9\n"
        "jmp .Lminlz_better16k_%=_repeat_index_loop_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_no_repeat_found_encodeBetterBlockAsm16K:\n"
        "cmpl %%edi, %%r10d\n"
        "je .Lminlz_better16k_%=_candidate_match_encodeBetterBlockAsm16K\n"
        "cmpl %%edi, %%r11d\n"
        "je .Lminlz_better16k_%=_candidateS_match_encodeBetterBlockAsm16K\n"
        "movl 20(%%rsp), %%eax\n"
#if MINLZ_BETTER16K_REUSE_HASH1
        /* This no-match edge has not modified the hash multiplier in r9. */
        "jmp .Lminlz_better16k_%=_search_reuse_hash1\n"
#else
        "jmp .Lminlz_better16k_%=_search_loop_encodeBetterBlockAsm16K\n"
#endif
        ".Lminlz_better16k_%=_candidateS_match_encodeBetterBlockAsm16K:\n"
        "shrq $0x08, %%rdi\n"
        "movq %%rdi, %%r10\n"
        "shlq $0x10, %%r10\n"
        "imulq %%r9, %%r10\n"
        "shrq $0x32, %%r10\n"
        "movzwl (%%rbx,%%r10,2), %%esi\n"
        "incl %%eax\n"
        "movw %%ax, (%%rbx,%%r10,2)\n"
        "cmpl %%edi, (%%rdx,%%rsi,1)\n"
        "je .Lminlz_better16k_%=_candidate_match_encodeBetterBlockAsm16K\n"
        "decl %%eax\n"
        "movl %%r8d, %%esi\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_candidate_match_encodeBetterBlockAsm16K:\n"
        "movl 12(%%rsp), %%ebx\n"
        "testl %%esi, %%esi\n"
        "jz .Lminlz_better16k_%=_match_extend_back_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_match_extend_back_loop_encodeBetterBlockAsm16K:\n"
        "cmpl %%ebx, %%eax\n"
        "jbe .Lminlz_better16k_%=_match_extend_back_end_encodeBetterBlockAsm16K\n"
        "movb -1(%%rdx,%%rsi,1), %%dil\n"
        "movb -1(%%rdx,%%rax,1), %%r8b\n"
        "cmpb %%r8b, %%dil\n"
        "jne .Lminlz_better16k_%=_match_extend_back_end_encodeBetterBlockAsm16K\n"
        "leal -1(%%rax), %%eax\n"
        "decl %%esi\n"
        "jz .Lminlz_better16k_%=_match_extend_back_end_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_match_extend_back_loop_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_match_extend_back_end_encodeBetterBlockAsm16K:\n"
        "movl %%eax, %%ebx\n"
        "subl 12(%%rsp), %%ebx\n"
        "leaq 3(%%rcx,%%rbx,1), %%rbx\n"
        "cmpq (%%rsp), %%rbx\n"
        "jb .Lminlz_better16k_%=_match_dst_size_check_encodeBetterBlockAsm16K\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        ".Lminlz_better16k_%=_match_dst_size_check_encodeBetterBlockAsm16K:\n"
        "movl %%eax, %%ebx\n"
        "addl $0x04, %%eax\n"
        "addl $0x04, %%esi\n"
        "movq 40(%%rsp), %%rdi\n"
        "subl %%eax, %%edi\n"
        "leaq (%%rdx,%%rax,1), %%r8\n"
        "leaq (%%rdx,%%rsi,1), %%r9\n"
        "xorl %%r11d, %%r11d\n"
        "jmp .Lminlz_better16k_%=_matchlen_loop_16_entry_match_nolit_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_matchlen_loopback_16_match_nolit_encodeBetterBlockAsm16K:\n"
        "movq (%%r8,%%r11,1), %%r10\n"
        "movq 8(%%r8,%%r11,1), %%r12\n"
        "xorq (%%r9,%%r11,1), %%r10\n"
        "jnz .Lminlz_better16k_%=_matchlen_bsf_8_match_nolit_encodeBetterBlockAsm16K\n"
        "xorq 8(%%r9,%%r11,1), %%r12\n"
        "jnz .Lminlz_better16k_%=_matchlen_bsf_16match_nolit_encodeBetterBlockAsm16K\n"
        "leal -16(%%rdi), %%edi\n"
        "leal 16(%%r11), %%r11d\n"
        ".Lminlz_better16k_%=_matchlen_loop_16_entry_match_nolit_encodeBetterBlockAsm16K:\n"
        "cmpl $0x10, %%edi\n"
        "jae .Lminlz_better16k_%=_matchlen_loopback_16_match_nolit_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_matchlen_match8_match_nolit_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_matchlen_bsf_16match_nolit_encodeBetterBlockAsm16K:\n"
        "tzcntq %%r12, %%r12\n"
        "sarq $0x03, %%r12\n"
        "leal 8(%%r11,%%r12,1), %%r11d\n"
        "jmp .Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_matchlen_match8_match_nolit_encodeBetterBlockAsm16K:\n"
        "cmpl $0x08, %%edi\n"
        "jb .Lminlz_better16k_%=_matchlen_match4_match_nolit_encodeBetterBlockAsm16K\n"
        "movq (%%r8,%%r11,1), %%r10\n"
        "xorq (%%r9,%%r11,1), %%r10\n"
        "jnz .Lminlz_better16k_%=_matchlen_bsf_8_match_nolit_encodeBetterBlockAsm16K\n"
        "leal -8(%%rdi), %%edi\n"
        "leal 8(%%r11), %%r11d\n"
        "jmp .Lminlz_better16k_%=_matchlen_match4_match_nolit_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_matchlen_bsf_8_match_nolit_encodeBetterBlockAsm16K:\n"
        "tzcntq %%r10, %%r10\n"
        "sarq $0x03, %%r10\n"
        "addl %%r10d, %%r11d\n"
        "jmp .Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_matchlen_match4_match_nolit_encodeBetterBlockAsm16K:\n"
        "cmpl $0x04, %%edi\n"
        "jb .Lminlz_better16k_%=_matchlen_match2_match_nolit_encodeBetterBlockAsm16K\n"
        "movl (%%r8,%%r11,1), %%r10d\n"
        "cmpl %%r10d, (%%r9,%%r11,1)\n"
        "jne .Lminlz_better16k_%=_matchlen_match2_match_nolit_encodeBetterBlockAsm16K\n"
        "leal -4(%%rdi), %%edi\n"
        "leal 4(%%r11), %%r11d\n"
        ".Lminlz_better16k_%=_matchlen_match2_match_nolit_encodeBetterBlockAsm16K:\n"
        "cmpl $0x01, %%edi\n"
        "je .Lminlz_better16k_%=_matchlen_match1_match_nolit_encodeBetterBlockAsm16K\n"
        "jb .Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K\n"
        "movw (%%r8,%%r11,1), %%r10w\n"
        "cmpw %%r10w, (%%r9,%%r11,1)\n"
        "jne .Lminlz_better16k_%=_matchlen_match1_match_nolit_encodeBetterBlockAsm16K\n"
        "leal 2(%%r11), %%r11d\n"
        "subl $0x02, %%edi\n"
        "jz .Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_matchlen_match1_match_nolit_encodeBetterBlockAsm16K:\n"
        "movb (%%r8,%%r11,1), %%r10b\n"
        "cmpb %%r10b, (%%r9,%%r11,1)\n"
        "jne .Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K\n"
        "leal 1(%%r11), %%r11d\n"
        "jmp .Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_match_nolit_end_encodeBetterBlockAsm16K:\n"
        "movl %%eax, %%edi\n"
        "subl %%esi, %%edi\n"
        "movl %%edi, 16(%%rsp)\n"
        "movl 12(%%rsp), %%r8d\n"
        "movl %%ebx, %%esi\n"
        "subl %%r8d, %%esi\n"
        "jz .Lminlz_better16k_%=_match_emit_nolits_encodeBetterBlockAsm16K\n"
        "cmpl $0x00000040, %%edi\n"
        "jl .Lminlz_better16k_%=_match_emit_lits_encodeBetterBlockAsm16K\n"
        "cmpl $0x04, %%esi\n"
        "ja .Lminlz_better16k_%=_match_emit_lits_encodeBetterBlockAsm16K\n"
        "movl (%%rdx,%%r8,1), %%r8d\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        "movl %%eax, 12(%%rsp)\n"
        "xorq %%r9, %%r9\n"
        "subl $0x40, %%edi\n"
        "leal -11(%%r11), %%r10d\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%di, 1(%%rcx)\n"
        "cmpl $0x07, %%r11d\n"
        "cmovgel %%r10d, %%r9d\n"
        "movq $0x00000007, %%rdi\n"
        "cmovll %%r11d, %%edi\n"
        "leal -1(%%rsi,%%rdi,4), %%edi\n"
        "movl $0x00000003, %%r10d\n"
        "leal (%%r10,%%rdi,8), %%edi\n"
        "movb %%dil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "movl %%r8d, (%%rcx)\n"
        "addq %%rsi, %%rcx\n"
        "testl %%r9d, %%r9d\n"
        "jz .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        "leal -1(%%r9), %%esi\n"
        "cmpl $0x1d, %%r9d\n"
        "jbe .Lminlz_better16k_%=_repeat_one_match_emit_repeat_copy2_encodeBetterBlockAsm16K\n"
        "leal -30(%%r9), %%esi\n"
        "cmpl $0x0000011e, %%r9d\n"
        "jb .Lminlz_better16k_%=_repeat_two_match_emit_repeat_copy2_encodeBetterBlockAsm16K\n"
        "cmpl $0x0001001e, %%r9d\n"
        "jb .Lminlz_better16k_%=_repeat_three_match_emit_repeat_copy2_encodeBetterBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_three_match_emit_repeat_copy2_encodeBetterBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_two_match_emit_repeat_copy2_encodeBetterBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_one_match_emit_repeat_copy2_encodeBetterBlockAsm16K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r9,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_match_emit_lits_encodeBetterBlockAsm16K:\n"
        "leaq (%%rdx,%%r8,1), %%r8\n"
        "leal -1(%%rsi), %%r9d\n"
        "cmpl $0x1d, %%r9d\n"
        "jb .Lminlz_better16k_%=_one_byte_match_emit_encodeBetterBlockAsm16K\n"
        "subl $0x1d, %%r9d\n"
        "cmpl $0x00000100, %%r9d\n"
        "jb .Lminlz_better16k_%=_two_bytes_match_emit_encodeBetterBlockAsm16K\n"
        "jb .Lminlz_better16k_%=_three_bytes_match_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_three_bytes_match_emit_encodeBetterBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%r9w, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%r9d\n"
        "jmp .Lminlz_better16k_%=_memmove_long_match_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_two_bytes_match_emit_encodeBetterBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%r9b, 1(%%rcx)\n"
        "addl $0x1d, %%r9d\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%r9d\n"
        "jb .Lminlz_better16k_%=_memmove_midmatch_emit_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_memmove_long_match_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_one_byte_match_emit_encodeBetterBlockAsm16K:\n"
        "shlb $0x03, %%r9b\n"
        "movb %%r9b, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x08, %%rsi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_8\n"
        "cmpq $0x10, %%rsi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_8:\n"
        "movq (%%r8), %%r10\n"
        "movq %%r10, (%%rcx)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_8through16:\n"
        "movq (%%r8), %%r10\n"
        "movq -8(%%r8,%%rsi,1), %%r8\n"
        "movq %%r10, (%%rcx)\n"
        "movq %%r8, -8(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_better16k_%=_memmove_end_copy_match_emit_encodeBetterBlockAsm16K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_emit_nolits_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_midmatch_emit_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "cmpq $0x20, %%rsi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rsi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_mid_end_copy_match_emit_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        ".Lminlz_better16k_%=_memmove_mid_end_copy_match_emit_encodeBetterBlockAsm16K:\n"
        "movq %%r9, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_emit_nolits_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_long_match_emit_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rsi,1), %%r9\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rsi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rsi,1), %%xmm3\n"
        "movq %%rsi, %%r12\n"
        "shrq $0x05, %%r12\n"
        "movq %%rcx, %%r10\n"
        "andl $0x0000001f, %%r10d\n"
        "movq $0x00000040, %%r13\n"
        "subq %%r10, %%r13\n"
        "decq %%r12\n"
        "ja .Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r13,1), %%r10\n"
        "leaq -32(%%rcx,%%r13,1), %%r14\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%r10), %%xmm4\n"
        "movdqu 16(%%r10), %%xmm5\n"
        "movdqa %%xmm4, (%%r14)\n"
        "movdqa %%xmm5, 16(%%r14)\n"
        "addq $0x20, %%r14\n"
        "addq $0x20, %%r10\n"
        "addq $0x20, %%r13\n"
        "decq %%r12\n"
        "jna .Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r13,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r13,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r13,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r13,1)\n"
        "addq $0x20, %%r13\n"
        "cmpq %%r13, %%rsi\n"
        "jae .Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rsi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rsi,1)\n"
        "movq %%r9, %%rcx\n"
        ".Lminlz_better16k_%=_match_emit_nolits_encodeBetterBlockAsm16K:\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        "movl %%eax, 12(%%rsp)\n"
        "cmpl $0x00000400, %%edi\n"
        "ja .Lminlz_better16k_%=_two_byte_match_nolit_encodeBetterBlockAsm16K\n"
        "cmpl $0x00000013, %%r11d\n"
        "jae .Lminlz_better16k_%=_emit_one_longer_match_nolit_encodeBetterBlockAsm16K\n"
        "leal -1(%%rdi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal -15(%%rsi,%%r11,4), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_one_longer_match_nolit_encodeBetterBlockAsm16K:\n"
        "cmpl $0x00000112, %%r11d\n"
        "jae .Lminlz_better16k_%=_emit_copy1_repeat_match_nolit_encodeBetterBlockAsm16K\n"
        "leal -1(%%rdi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 61(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "leal -18(%%r11), %%esi\n"
        "movb %%sil, 2(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_copy1_repeat_match_nolit_encodeBetterBlockAsm16K:\n"
        "leal -1(%%rdi), %%esi\n"
        "shll $0x06, %%esi\n"
        "leal 57(%%rsi), %%esi\n"
        "movw %%si, (%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "subl $0x12, %%r11d\n"
        "leal -1(%%r11), %%esi\n"
        "cmpl $0x1d, %%r11d\n"
        "jbe .Lminlz_better16k_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm16K\n"
        "leal -30(%%r11), %%esi\n"
        "cmpl $0x0000011e, %%r11d\n"
        "jb .Lminlz_better16k_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm16K\n"
        "cmpl $0x0001001e, %%r11d\n"
        "jb .Lminlz_better16k_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_three_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_two_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_one_emit_copy1_do_repeat_match_nolit_encodeBetterBlockAsm16K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r11,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_two_byte_match_nolit_encodeBetterBlockAsm16K:\n"
        "leal -64(%%rdi), %%edi\n"
        "leal -4(%%r11), %%r11d\n"
        "movw %%di, 1(%%rcx)\n"
        "cmpl $0x3c, %%r11d\n"
        "jbe .Lminlz_better16k_%=_emit_copy2_0_match_nolit_encodeBetterBlockAsm16K_emit2\n"
        "leal -60(%%r11), %%esi\n"
        "cmpl $0x0000013c, %%r11d\n"
        "jb .Lminlz_better16k_%=_emit_copy2_1_match_nolit_encodeBetterBlockAsm16K_emit2\n"
        "cmpl $0x0001003c, %%r11d\n"
        "jb .Lminlz_better16k_%=_emit_copy2_2_match_nolit_encodeBetterBlockAsm16K_emit2\n"
        "movb $0xfe, (%%rcx)\n"
        "movl %%esi, 3(%%rcx)\n"
        "addq $0x06, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_copy2_2_match_nolit_encodeBetterBlockAsm16K_emit2:\n"
        "movb $0xfa, (%%rcx)\n"
        "movw %%si, 3(%%rcx)\n"
        "addq $0x05, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_copy2_1_match_nolit_encodeBetterBlockAsm16K_emit2:\n"
        "movb $0xf6, (%%rcx)\n"
        "movb %%sil, 3(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_copy2_0_match_nolit_encodeBetterBlockAsm16K_emit2:\n"
        "movl $0x00000002, %%esi\n"
        "leal (%%rsi,%%r11,4), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        "movl 12(%%rsp), %%esi\n"
        "cmpl %%ebx, %%esi\n"
        "je .Lminlz_better16k_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm16K\n"
        "movl %%ebx, %%edi\n"
        "movl %%ebx, 12(%%rsp)\n"
        "leaq (%%rdx,%%rsi,1), %%r8\n"
        "subl %%esi, %%edi\n"
        "leal -1(%%rdi), %%esi\n"
        "cmpl $0x1d, %%esi\n"
        "jb .Lminlz_better16k_%=_one_byte_match_emit_repeat_encodeBetterBlockAsm16K\n"
        "subl $0x1d, %%esi\n"
        "cmpl $0x00000100, %%esi\n"
        "jb .Lminlz_better16k_%=_two_bytes_match_emit_repeat_encodeBetterBlockAsm16K\n"
        "jb .Lminlz_better16k_%=_three_bytes_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_three_bytes_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%esi\n"
        "jmp .Lminlz_better16k_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_two_bytes_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addl $0x1d, %%esi\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%esi\n"
        "jb .Lminlz_better16k_%=_memmove_midmatch_emit_repeat_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_one_byte_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "shlb $0x03, %%sil\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x08, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_8\n"
        "cmpq $0x10, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_8:\n"
        "movq (%%r8), %%r9\n"
        "movq %%r9, (%%rcx)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_8through16:\n"
        "movq (%%r8), %%r9\n"
        "movq -8(%%r8,%%rdi,1), %%r8\n"
        "movq %%r9, (%%rcx)\n"
        "movq %%r8, -8(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better16k_%=_memmove_end_copy_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better16k_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_midmatch_emit_repeat_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "cmpq $0x20, %%rdi\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rdi,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_mid_end_copy_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_match_emit_repeat_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        ".Lminlz_better16k_%=_memmove_mid_end_copy_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "movq %%rsi, %%rcx\n"
        "jmp .Lminlz_better16k_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_long_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rdi,1), %%rsi\n"
        "movdqu (%%r8), %%xmm0\n"
        "movdqu 16(%%r8), %%xmm1\n"
        "movdqu -32(%%r8,%%rdi,1), %%xmm2\n"
        "movdqu -16(%%r8,%%rdi,1), %%xmm3\n"
        "movq %%rdi, %%r10\n"
        "shrq $0x05, %%r10\n"
        "movq %%rcx, %%r9\n"
        "andl $0x0000001f, %%r9d\n"
        "movq $0x00000040, %%r12\n"
        "subq %%r9, %%r12\n"
        "decq %%r10\n"
        "ja .Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%r8,%%r12,1), %%r9\n"
        "leaq -32(%%rcx,%%r12,1), %%r13\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%r9), %%xmm4\n"
        "movdqu 16(%%r9), %%xmm5\n"
        "movdqa %%xmm4, (%%r13)\n"
        "movdqa %%xmm5, 16(%%r13)\n"
        "addq $0x20, %%r13\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%r12\n"
        "decq %%r10\n"
        "jna .Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%r8,%%r12,1), %%xmm4\n"
        "movdqu -16(%%r8,%%r12,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r12,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r12,1)\n"
        "addq $0x20, %%r12\n"
        "cmpq %%r12, %%rdi\n"
        "jae .Lminlz_better16k_%=_emit_lit_memmove_long_match_emit_repeat_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rdi,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rdi,1)\n"
        "movq %%rsi, %%rcx\n"
        ".Lminlz_better16k_%=_emit_literal_done_match_emit_repeat_encodeBetterBlockAsm16K:\n"
        "addl %%r11d, %%eax\n"
        "addl $0x04, %%r11d\n"
        "movl %%eax, 12(%%rsp)\n"
        "leal -1(%%r11), %%esi\n"
        "cmpl $0x1d, %%r11d\n"
        "jbe .Lminlz_better16k_%=_repeat_one_match_nolit_repeat_encodeBetterBlockAsm16K\n"
        "leal -30(%%r11), %%esi\n"
        "cmpl $0x0000011e, %%r11d\n"
        "jb .Lminlz_better16k_%=_repeat_two_match_nolit_repeat_encodeBetterBlockAsm16K\n"
        "cmpl $0x0001001e, %%r11d\n"
        "jb .Lminlz_better16k_%=_repeat_three_match_nolit_repeat_encodeBetterBlockAsm16K\n"
        "movb $0xfc, (%%rcx)\n"
        "movl %%esi, 1(%%rcx)\n"
        "addq $0x04, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_three_match_nolit_repeat_encodeBetterBlockAsm16K:\n"
        "movb $0xf4, (%%rcx)\n"
        "movw %%si, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_two_match_nolit_repeat_encodeBetterBlockAsm16K:\n"
        "movb $0xec, (%%rcx)\n"
        "movb %%sil, 1(%%rcx)\n"
        "addq $0x02, %%rcx\n"
        "jmp .Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_repeat_one_match_nolit_repeat_encodeBetterBlockAsm16K:\n"
        "xorl %%esi, %%esi\n"
        "leal -4(%%rsi,%%r11,8), %%esi\n"
        "movb %%sil, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        ".Lminlz_better16k_%=_match_nolit_emitcopy_end_encodeBetterBlockAsm16K:\n"
        "cmpl 8(%%rsp), %%eax\n"
        "jae .Lminlz_better16k_%=_emit_remainder_encodeBetterBlockAsm16K\n"
        "cmpq (%%rsp), %%rcx\n"
        "jb .Lminlz_better16k_%=_match_nolit_dst_ok_encodeBetterBlockAsm16K\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        ".Lminlz_better16k_%=_match_nolit_dst_ok_encodeBetterBlockAsm16K:\n"
        "movq 48(%%rsp), %%rsi\n"
        "movq $0x0000cf1bbcdcbf9b, %%rdi\n"
        "movq $0x9e3779b1, %%r8\n"
        "leaq 1(%%rbx), %%rbx\n"
        "leaq -2(%%rax), %%r9\n"
        "movq (%%rdx,%%rbx,1), %%r10\n"
        "movq 1(%%rdx,%%rbx,1), %%r11\n"
        "movq (%%rdx,%%r9,1), %%r12\n"
        "movq 1(%%rdx,%%r9,1), %%r13\n"
        "shlq $0x10, %%r10\n"
        "imulq %%rdi, %%r10\n"
        "shrq $0x32, %%r10\n"
        "shlq $0x20, %%r11\n"
        "imulq %%r8, %%r11\n"
        "shrq $0x35, %%r11\n"
        "shlq $0x10, %%r12\n"
        "imulq %%rdi, %%r12\n"
        "shrq $0x32, %%r12\n"
        "shlq $0x20, %%r13\n"
        "imulq %%r8, %%r13\n"
        "shrq $0x35, %%r13\n"
        "leaq 1(%%rbx), %%r8\n"
        "leaq 1(%%r9), %%r14\n"
        "movw %%bx, (%%rsi,%%r10,2)\n"
        "movw %%r9w, (%%rsi,%%r12,2)\n"
        "leaq 1(%%r9,%%rbx,1), %%r10\n"
        "shrq $0x01, %%r10\n"
        "addq $0x01, %%rbx\n"
        "subq $0x01, %%r9\n"
        "movw %%r8w, 32768(%%rsi,%%r11,2)\n"
        "movw %%r14w, 32768(%%rsi,%%r13,2)\n"
        ".Lminlz_better16k_%=_index_loop_encodeBetterBlockAsm16K:\n"
        "cmpq %%r9, %%r10\n"
        "jae .Lminlz_better16k_%=_search_loop_encodeBetterBlockAsm16K\n"
        "movq (%%rdx,%%rbx,1), %%r8\n"
        "movq (%%rdx,%%r10,1), %%r11\n"
        "shlq $0x10, %%r8\n"
        "imulq %%rdi, %%r8\n"
        "shrq $0x32, %%r8\n"
        "shlq $0x10, %%r11\n"
        "imulq %%rdi, %%r11\n"
        "shrq $0x32, %%r11\n"
        "movw %%bx, (%%rsi,%%r8,2)\n"
        "movw %%r10w, (%%rsi,%%r11,2)\n"
        "addq $0x02, %%rbx\n"
        "addq $0x02, %%r10\n"
        "jmp .Lminlz_better16k_%=_index_loop_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_remainder_encodeBetterBlockAsm16K:\n"
        "movq 40(%%rsp), %%rax\n"
        "subl 12(%%rsp), %%eax\n"
        "leaq 3(%%rcx,%%rax,1), %%rax\n"
        "cmpq (%%rsp), %%rax\n"
        "jb .Lminlz_better16k_%=_emit_remainder_ok_encodeBetterBlockAsm16K\n"
        "xorq %%rax, %%rax\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        ".Lminlz_better16k_%=_emit_remainder_ok_encodeBetterBlockAsm16K:\n"
        "movq 40(%%rsp), %%rax\n"
        "movl 12(%%rsp), %%ebx\n"
        "cmpl %%eax, %%ebx\n"
        "je .Lminlz_better16k_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm16K\n"
        "movl %%eax, %%esi\n"
        "movl %%eax, 12(%%rsp)\n"
        "leaq (%%rdx,%%rbx,1), %%rax\n"
        "subl %%ebx, %%esi\n"
        "leal -1(%%rsi), %%edx\n"
        "cmpl $0x1d, %%edx\n"
        "jb .Lminlz_better16k_%=_one_byte_emit_remainder_encodeBetterBlockAsm16K\n"
        "subl $0x1d, %%edx\n"
        "cmpl $0x00000100, %%edx\n"
        "jb .Lminlz_better16k_%=_two_bytes_emit_remainder_encodeBetterBlockAsm16K\n"
        "jb .Lminlz_better16k_%=_three_bytes_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_three_bytes_emit_remainder_encodeBetterBlockAsm16K:\n"
        "movb $0xf0, (%%rcx)\n"
        "movw %%dx, 1(%%rcx)\n"
        "addq $0x03, %%rcx\n"
        "addl $0x1d, %%edx\n"
        "jmp .Lminlz_better16k_%=_memmove_long_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_two_bytes_emit_remainder_encodeBetterBlockAsm16K:\n"
        "movb $0xe8, (%%rcx)\n"
        "movb %%dl, 1(%%rcx)\n"
        "addl $0x1d, %%edx\n"
        "addq $0x02, %%rcx\n"
        "cmpl $0x40, %%edx\n"
        "jb .Lminlz_better16k_%=_memmove_midemit_remainder_encodeBetterBlockAsm16K\n"
        "jmp .Lminlz_better16k_%=_memmove_long_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_one_byte_emit_remainder_encodeBetterBlockAsm16K:\n"
        "shlb $0x03, %%dl\n"
        "movb %%dl, (%%rcx)\n"
        "addq $0x01, %%rcx\n"
        "leaq (%%rcx,%%rsi,1), %%rdx\n"
        "movl %%esi, %%ebx\n"
        "cmpq $0x03, %%rbx\n"
        "jb .Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_1or2\n"
        "je .Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_3\n"
        "cmpq $0x08, %%rbx\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_4through8\n"
        "cmpq $0x10, %%rbx\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_8through16\n"
        "cmpq $0x20, %%rbx\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_1or2:\n"
        "movb (%%rax), %%sil\n"
        "movb -1(%%rax,%%rbx,1), %%al\n"
        "movb %%sil, (%%rcx)\n"
        "movb %%al, -1(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_3:\n"
        "movw (%%rax), %%si\n"
        "movb 2(%%rax), %%al\n"
        "movw %%si, (%%rcx)\n"
        "movb %%al, 2(%%rcx)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_4through8:\n"
        "movl (%%rax), %%esi\n"
        "movl -4(%%rax,%%rbx,1), %%eax\n"
        "movl %%esi, (%%rcx)\n"
        "movl %%eax, -4(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm16K\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_8through16:\n"
        "movq (%%rax), %%rsi\n"
        "movq -8(%%rax,%%rbx,1), %%rax\n"
        "movq %%rsi, (%%rcx)\n"
        "movq %%rax, -8(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_emit_remainder_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu 16(%%rax), %%xmm1\n"
        "movdqu -32(%%rax,%%rbx,1), %%xmm2\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rbx,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rbx,1)\n"
        ".Lminlz_better16k_%=_memmove_end_copy_emit_remainder_encodeBetterBlockAsm16K:\n"
        "movq %%rdx, %%rcx\n"
        "jmp .Lminlz_better16k_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_midemit_remainder_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rsi,1), %%rdx\n"
        "movl %%esi, %%ebx\n"
        "cmpq $0x20, %%rbx\n"
        "jbe .Lminlz_better16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm16K_memmove_move_17through32\n"
        "jmp .Lminlz_better16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm16K_memmove_move_33through64\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm16K_memmove_move_17through32:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm1\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, -16(%%rcx,%%rbx,1)\n"
        "jmp .Lminlz_better16k_%=_memmove_mid_end_copy_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_mid_emit_remainder_encodeBetterBlockAsm16K_memmove_move_33through64:\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu 16(%%rax), %%xmm1\n"
        "movdqu -32(%%rax,%%rbx,1), %%xmm2\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm3\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rbx,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rbx,1)\n"
        ".Lminlz_better16k_%=_memmove_mid_end_copy_emit_remainder_encodeBetterBlockAsm16K:\n"
        "movq %%rdx, %%rcx\n"
        "jmp .Lminlz_better16k_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm16K\n"
        ".Lminlz_better16k_%=_memmove_long_emit_remainder_encodeBetterBlockAsm16K:\n"
        "leaq (%%rcx,%%rsi,1), %%rdx\n"
        "movl %%esi, %%ebx\n"
        "movdqu (%%rax), %%xmm0\n"
        "movdqu 16(%%rax), %%xmm1\n"
        "movdqu -32(%%rax,%%rbx,1), %%xmm2\n"
        "movdqu -16(%%rax,%%rbx,1), %%xmm3\n"
        "movq %%rbx, %%rdi\n"
        "shrq $0x05, %%rdi\n"
        "movq %%rcx, %%rsi\n"
        "andl $0x0000001f, %%esi\n"
        "movq $0x00000040, %%r8\n"
        "subq %%rsi, %%r8\n"
        "decq %%rdi\n"
        "ja .Lminlz_better16k_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "leaq -32(%%rax,%%r8,1), %%rsi\n"
        "leaq -32(%%rcx,%%r8,1), %%r9\n"
        ".p2align 4\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm16Klarge_big_loop_back:\n"
        "movdqu (%%rsi), %%xmm4\n"
        "movdqu 16(%%rsi), %%xmm5\n"
        "movdqa %%xmm4, (%%r9)\n"
        "movdqa %%xmm5, 16(%%r9)\n"
        "addq $0x20, %%r9\n"
        "addq $0x20, %%rsi\n"
        "addq $0x20, %%r8\n"
        "decq %%rdi\n"
        "jna .Lminlz_better16k_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm16Klarge_big_loop_back\n"
        ".Lminlz_better16k_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm16Klarge_forward_sse_loop_32:\n"
        "movdqu -32(%%rax,%%r8,1), %%xmm4\n"
        "movdqu -16(%%rax,%%r8,1), %%xmm5\n"
        "movdqa %%xmm4, -32(%%rcx,%%r8,1)\n"
        "movdqa %%xmm5, -16(%%rcx,%%r8,1)\n"
        "addq $0x20, %%r8\n"
        "cmpq %%r8, %%rbx\n"
        "jae .Lminlz_better16k_%=_emit_lit_memmove_long_emit_remainder_encodeBetterBlockAsm16Klarge_forward_sse_loop_32\n"
        "movdqu %%xmm0, (%%rcx)\n"
        "movdqu %%xmm1, 16(%%rcx)\n"
        "movdqu %%xmm2, -32(%%rcx,%%rbx,1)\n"
        "movdqu %%xmm3, -16(%%rcx,%%rbx,1)\n"
        "movq %%rdx, %%rcx\n"
        ".Lminlz_better16k_%=_emit_literal_done_emit_remainder_encodeBetterBlockAsm16K:\n"
        "movq 24(%%rsp), %%rax\n"
        "subq %%rax, %%rcx\n"
        "movq %%rcx, %%rax\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        "jmp .Lminlz_better16k_%=_epilogue\n"
        ".Lminlz_better16k_%=_epilogue:\n"
        "addq $64, %%rsp\n"
        "popq %%r14\n"
        "popq %%r13\n"
        "popq %%r12\n"
        "popq %%rbx\n"
        : "=&a"(ret), [repeat_index] "=&r"(repeat_index)
        : [dst] "r"(dst), [src] "r"(src), [src_len] "r"(src_len), [tmp] "r"(tmp)
        : "rcx", "rdx", "rsi", "rdi", "r8", "r9", "r10", "r11",
          "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "memory", "cc");
    /* Preserve the v1.2.2 direct path's fallback when its size check
     * included the last repeat-table position. Only ret is the length
     * of the encoded payload. */
    if (direct_limit != 0u && ret + repeat_index > direct_limit) {
        return 0;
    }
    return ret;
#else
    (void)dst;
    (void)src;
    (void)src_len;
    (void)tmp;
    (void)direct_limit;
    return 0;
#endif
}
#endif

#if MINLZ_ENABLE_L2_16K_GENERIC_FAST
static size_t minlz_encode_balanced_16k_generic_fast(uint8_t * restrict dst,
                                                     size_t dst_cap,
                                                     const uint8_t * restrict src,
                                                     size_t src_len,
                                                     uint8_t *scratch) {
    enum { table_bits = 14, table_size = 1u << table_bits };
    uint16_t *table = (uint16_t *)scratch;
    size_t s = 1;
    size_t next_emit = 0;
    size_t repeat = 1;
    size_t d = 0;
    size_t s_limit;

    if (!scratch || src_len <= (4u << 10) || src_len > (16u << 10) ||
        src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || dst_cap < src_len) {
        return 0;
    }
    memset(table, 0, (size_t)table_size * sizeof(table[0]));
    s_limit = src_len - 4;

    for (;;) {
        size_t candidate;
        size_t base;
        size_t offset;
        size_t length;

        for (;;) {
            uint32_t cv;
            uint32_t h;
            size_t min_src_pos;
            if (s > s_limit) {
                goto emit_remainder;
            }
            cv = minlz_load32(src + s);
            h = minlz_hash4(cv, table_bits);
            candidate = (size_t)table[h];
            table[h] = (uint16_t)s;
            min_src_pos = s > MINLZ_MAX_COPY3_OFFSET ? s - MINLZ_MAX_COPY3_OFFSET : 0;
            if (candidate >= min_src_pos && candidate < s &&
                cv == minlz_load32(src + candidate)) {
                break;
            }
            s++;
        }

        base = s;
        offset = s - candidate;
        candidate += 4;
        s += 4;
        minlz_extend_match_size(src, &s, &candidate, src_len);
        length = s - base;

        if (next_emit != base) {
            const uint8_t *lits = src + next_emit;
            size_t lit_len = base - next_emit;
            int can_fuse = offset >= MINLZ_MIN_COPY2_OFFSET &&
                (lit_len <= 3 ||
                 (offset <= MINLZ_MAX_COPY2_OFFSET && lit_len <= 4));
            size_t n;
            if (d + lit_len + 16 > dst_cap) {
                return 0;
            }
            if (can_fuse) {
                if (offset <= MINLZ_MAX_COPY2_OFFSET) {
                    n = minlz_emit_copy_lits2_fast(dst + d, lits, lit_len, offset, length);
                } else {
                    n = minlz_emit_copy_lits3_fast(dst + d, lits, lit_len, offset, length);
                }
                d += n;
                length = 0;
            } else {
                n = minlz_emit_literal_fast(dst + d, lits, lit_len);
                d += n;
            }
        }

        if (length > 0) {
            size_t n;
            if (d + 16 > dst_cap) {
                return 0;
            }
            if (offset == repeat) {
                n = minlz_emit_repeat_fast(dst + d, length);
            } else {
                n = minlz_emit_copy_fast(dst + d, offset, length);
            }
            d += n;
        }
        repeat = offset;
        next_emit = s;

        if (s > s_limit) {
            goto emit_remainder;
        }
        base++;
        while (base < s) {
            uint32_t h = minlz_hash4(minlz_load32(src + base), table_bits);
            table[h] = (uint16_t)base;
            base++;
        }
    }

emit_remainder:
    if (next_emit < src_len) {
        size_t lit_len = src_len - next_emit;
        size_t n;
        if (d + lit_len + 4 > dst_cap) {
            return 0;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit, lit_len);
        d += n;
    }
    return d < src_len ? d : 0;
}
#endif

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32) && (defined(__GNUC__) || defined(__clang__))
static MINLZ_ALWAYS_INLINE void minlz_clear_l1_table_avx2(uint8_t *p, size_t n) {
    uint8_t *end = p + n;
    __asm__ volatile(
        "vpxor %%ymm0, %%ymm0, %%ymm0\n\t"
        "1:\n\t"
        "vmovdqu %%ymm0, 0(%[p])\n\t"
        "vmovdqu %%ymm0, 32(%[p])\n\t"
        "vmovdqu %%ymm0, 64(%[p])\n\t"
        "vmovdqu %%ymm0, 96(%[p])\n\t"
        "addq $128, %[p]\n\t"
        "cmpq %[end], %[p]\n\t"
        "jne 1b\n\t"
        "vzeroupper"
        : [p] "+r"(p)
        : [end] "r"(end)
        : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
          "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14", "xmm15", "cc", "memory");
}
#endif

#define MINLZ_FASTEST64K_IMPL minlz_encode_payload_fastest64k_impl_ws
#define MINLZ_FASTEST64K_ENTRY uint16_t
#define MINLZ_FASTEST64K_ENTRY_BYTES 2
#include "block_encode_fastest64k.inc"
#undef MINLZ_FASTEST64K_IMPL
#undef MINLZ_FASTEST64K_ENTRY
#undef MINLZ_FASTEST64K_ENTRY_BYTES

#define MINLZ_FASTEST64K_IMPL minlz_encode_payload_fastest256_impl_ws
#define MINLZ_FASTEST64K_ENTRY uint8_t
#define MINLZ_FASTEST64K_ENTRY_BYTES 1
#include "block_encode_fastest64k.inc"
#undef MINLZ_FASTEST64K_IMPL
#undef MINLZ_FASTEST64K_ENTRY
#undef MINLZ_FASTEST64K_ENTRY_BYTES

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64
#define MINLZ_FASTEST64K_IMPL minlz_encode_payload_fastest4k_prefix_ws
#define MINLZ_FASTEST64K_ENTRY uint16_t
#define MINLZ_FASTEST64K_ENTRY_BYTES 2
#define MINLZ_FASTEST64K_PREFIX_SSE2 1
#include "block_encode_fastest64k.inc"
#undef MINLZ_FASTEST64K_PREFIX_SSE2
#undef MINLZ_FASTEST64K_IMPL
#undef MINLZ_FASTEST64K_ENTRY
#undef MINLZ_FASTEST64K_ENTRY_BYTES
#endif

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
#define MINLZ_FASTEST64K_IMPL minlz_encode_payload_fastest64k_large_asm_impl_ws
#define MINLZ_FASTEST64K_ENTRY uint16_t
#define MINLZ_FASTEST64K_ENTRY_BYTES 2
#define MINLZ_FASTEST64K_FIXED_INLINE_PROBE 1
#include "block_encode_fastest64k.inc"
#undef MINLZ_FASTEST64K_FIXED_INLINE_PROBE
#undef MINLZ_FASTEST64K_IMPL
#undef MINLZ_FASTEST64K_ENTRY
#undef MINLZ_FASTEST64K_ENTRY_BYTES
#endif

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
static MINLZ_NOINLINE size_t minlz_encode_payload_fastest1k_ws(
    minlz_workspace *ws, uint8_t * restrict dst, size_t dst_cap,
    const uint8_t * restrict src, size_t src_len) {
    return minlz_encode_payload_fastest64k_impl_ws(ws, dst, dst_cap, src, src_len, 9, 3);
}
#endif

static size_t minlz_encode_payload_fastest64k_ws(minlz_workspace *ws, uint8_t * restrict dst,
    size_t dst_cap, const uint8_t * restrict src, size_t src_len) {
    if (src_len <= 1024u) {
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
        return minlz_encode_payload_fastest1k_ws(ws, dst, dst_cap, src, src_len);
#else
        return minlz_encode_payload_fastest64k_impl_ws(ws, dst, dst_cap, src, src_len, 9, 3);
#endif
    }
    if (src_len <= 4096u) {
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64
        if (_mm_movemask_epi8(_mm_cmpgt_epi8(
                _mm_loadu_si128((const __m128i *)(const void *)src), _mm_set1_epi8(31))) == 0xffff) {
            return minlz_encode_payload_fastest4k_prefix_ws(ws, dst, dst_cap, src, src_len, 10, 4);
        }
#endif
        return minlz_encode_payload_fastest64k_impl_ws(ws, dst, dst_cap, src, src_len, 10, 4);
    }
    if (src_len <= 16384u) {
        return minlz_encode_payload_fastest64k_impl_ws(ws, dst, dst_cap, src, src_len, 13, 5);
    }
    return minlz_encode_payload_fastest64k_impl_ws(ws, dst, dst_cap, src, src_len, 12, 4);
}

static MINLZ_ALWAYS_INLINE size_t minlz_encode_payload_fastest_impl(
    minlz_workspace *ws, uint8_t * restrict dst, size_t dst_cap,
    const uint8_t * restrict src, size_t src_len, unsigned table_bits, int skip_log,
    int check_offsets) {
    const size_t table_size = (size_t)1u << table_bits;
    const int input_margin = 8;
    const int max_copy3_offset = (int)MINLZ_MAX_COPY3_OFFSET;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    uint32_t *table;
    uint32_t *stamp;
    uint32_t generation;
    const int natural_2mb = src_len > (512u << 10) && src_len <= (2u << 20) &&
        minlz_better2mb_prefers_goasm(src, src_len);
    const int check_repeats = MINLZ_FASTEST_CHECK_REPEATS && !natural_2mb;
    const int fuse_literals = MINLZ_FASTEST_FUSE_LITERALS || natural_2mb;

    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || src_len > (size_t)INT_MAX) {
        return 0;
    }
    (void)dst_cap;
    if (minlz_workspace_prepare_table(ws, 0, table_size, &table, &stamp, &generation) != MINLZ_OK) {
        return 0;
    }
    (void)stamp;
    (void)generation;
    memset(table, 0, table_size * sizeof(table[0]));
    s_limit = (int)src_len - input_margin;
    dst_limit = (int)src_len - ((int)src_len >> 5) - 6;
    cv = minlz_load64(src + s);

    for (;;) {
        int candidate = 0;
        for (;;) {
            int next_s = s + ((s - next_emit) >> skip_log) + 4;
            int min_src_pos;
            uint32_t hash0;
            uint32_t hash1;
            uint32_t hash2;
            int candidate2;

            if (next_s > s_limit) {
                goto emit_remainder;
            }
            min_src_pos = s - max_copy3_offset;
            hash0 = minlz_hash6(cv, table_bits);
            hash1 = minlz_hash6(cv >> 8, table_bits);
            candidate = (int)table[hash0];
            candidate2 = (int)table[hash1];
            table[hash0] = (uint32_t)s;
            table[hash1] = (uint32_t)(s + 1);
            hash2 = minlz_hash6(cv >> 16, table_bits);

            if (check_repeats &&
                (uint32_t)(cv >> 8) == minlz_load32(src + s - repeat + 1)) {
                int base = s + 1;
                int i = base - repeat;
                size_t n;
                while (base > next_emit && i > 0 && src[i - 1] == src[base - 1]) {
                    i--;
                    base--;
                }
                if (d + (base - next_emit) > dst_limit) {
                    d = 0;
                    goto done;
                }
                n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)(base - next_emit));
                if (n == 0 && base != next_emit) {
                    d = 0;
                    goto done;
                }
                d += (int)n;

                candidate = s - repeat + 5;
                s += 5;
                minlz_extend_match_int_fastest_impl(src, &s, &candidate, (int)src_len, MINLZ_L1_MATCH_LONG);
                n = minlz_emit_repeat_fast(dst + d, (size_t)(s - base));
                if (n == 0) {
                    d = 0;
                    goto done;
                }
                d += (int)n;
                next_emit = s;
                if (s >= s_limit) {
                    goto emit_remainder;
                }
                cv = minlz_load64(src + s);
                continue;
            }

            if ((!check_offsets || candidate >= min_src_pos) && (uint32_t)cv == minlz_load32(src + candidate)) {
                break;
            }
            candidate = (int)table[hash2];
            if ((!check_offsets || candidate2 > min_src_pos) && (uint32_t)(cv >> 8) == minlz_load32(src + candidate2)) {
                table[hash2] = (uint32_t)(s + 2);
                candidate = candidate2;
                s++;
                break;
            }
            table[hash2] = (uint32_t)(s + 2);
            if ((!check_offsets || candidate >= min_src_pos + 2) && (uint32_t)(cv >> 16) == minlz_load32(src + candidate)) {
                s += 2;
                break;
            }

            cv = minlz_load64(src + next_s);
            s = next_s;
        }

        while (candidate > 0 && s > next_emit && src[candidate - 1] == src[s - 1]) {
            candidate--;
            s--;
        }

        {
            int base = s;
            int length;
            size_t n;
            repeat = base - candidate;
            s += 4;
            candidate += 4;
            minlz_extend_match_int_fastest_impl(src, &s, &candidate, (int)src_len, MINLZ_L1_MATCH_LONG);
            length = s - base;
            if (next_emit != base) {
                int lit_len = base - next_emit;
                if (!fuse_literals ||
                    lit_len > (int)MINLZ_MAX_COPY3_LITS ||
                    repeat < (int)MINLZ_MIN_COPY2_OFFSET) {
                    if (d + (base - next_emit) > dst_limit) {
                        d = 0;
                        goto done;
                    }
                    if (!check_offsets && (unsigned)(lit_len - 4) <= 3u &&
                        (unsigned)(length - 19) <= 45u && (unsigned)repeat <= MINLZ_MAX_COPY2_OFFSET) {
                        const uint32_t copy1 = ((uint32_t)(repeat - 1) << 6) |
                            (15u << 2) | MINLZ_TAG_COPY1 | ((uint32_t)(length - 18) << 16);
                        const uint32_t copy2 = ((uint32_t)(repeat - MINLZ_MIN_COPY2_OFFSET) << 8) |
                            ((uint32_t)(length - 4) << 2) | MINLZ_TAG_COPY2;
                        const uint32_t copy = (unsigned)repeat <= MINLZ_MAX_COPY1_OFFSET
                            ? copy1 : copy2;
                        /* The match supplies enough input for the word load.
                         * The second store overlaps the literal and writes
                         * the three-byte copy without writing past it. */
                        minlz_store64(dst + d, (minlz_load64(src + next_emit) << 8) |
                            ((uint64_t)(lit_len - 1) << 3));
                        minlz_store32(dst + d + lit_len, (copy << 8) | src[base - 1]);
                        n = (size_t)lit_len + 4u;
                    } else {
                        d += (int)minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)lit_len);
                        n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
                    }
                } else if (repeat <= (int)MINLZ_MAX_COPY2_OFFSET) {
                    n = minlz_emit_copy_lits2_fast(dst + d, src + next_emit, (size_t)lit_len,
                                                   (size_t)repeat, (size_t)length);
                } else {
                    n = minlz_emit_copy_lits3_fast(dst + d, src + next_emit, (size_t)lit_len,
                                                   (size_t)repeat, (size_t)length);
                }
            } else {
                n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
            }
            if (n == 0) {
                d = 0;
                goto done;
            }
            d += (int)n;
        }

        for (;;) {
            uint64_t x;
            uint32_t m2_hash;
            uint32_t curr_hash;
            int immediate_candidate;
            int base;
            size_t n;

            next_emit = s;
            if (s >= s_limit) {
                goto emit_remainder;
            }
            x = minlz_load64(src + s - 2);
            if (d > dst_limit) {
                d = 0;
                goto done;
            }
            m2_hash = minlz_hash6(x, table_bits);
            x >>= 16;
            curr_hash = minlz_hash6(x, table_bits);
            immediate_candidate = (int)table[curr_hash];
            table[m2_hash] = (uint32_t)(s - 2);
            table[curr_hash] = (uint32_t)s;
            if ((check_offsets && s - immediate_candidate > max_copy3_offset) ||
                (uint32_t)x !=
                    minlz_load32(src + immediate_candidate)) {
                cv = minlz_load64(src + s + 1);
                s++;
                break;
            }
            repeat = s - immediate_candidate;
            base = s;
            s += 4;
            immediate_candidate += 4;
            minlz_extend_match_int_fastest_impl(
                src, &s, &immediate_candidate, (int)src_len, MINLZ_L1_MATCH_LONG);
            n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)(s - base));
            if (n == 0) {
                d = 0;
                goto done;
            }
            d += (int)n;
        }
    }

emit_remainder:
    if (next_emit < (int)src_len) {
        size_t n;
        if (d + ((int)src_len - next_emit) > dst_limit) {
            d = 0;
            goto done;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit, src_len - (size_t)next_emit);
        if (n == 0) {
            d = 0;
            goto done;
        }
        d += (int)n;
    }
    if (d >= (int)src_len) {
        d = 0;
    }

done:
    return (size_t)d;
}

static size_t minlz_encode_payload_fastest_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                              size_t dst_cap,
                                              const uint8_t * restrict src, size_t src_len) {
    /* In the smaller buckets any previous position is in the COPY3 window. */
    if (src_len > (2u << 20)) {
        return minlz_encode_payload_fastest_impl(ws, dst, dst_cap, src, src_len, 14, 6, 1);
    }
#if MINLZ_FASTEST_2MB_TABLE_BITS != 13 || MINLZ_FASTEST_2MB_SKIP_LOG != 6
    if (src_len > (512u << 10)) {
        return minlz_encode_payload_fastest_impl(ws, dst, dst_cap, src, src_len,
            MINLZ_FASTEST_2MB_TABLE_BITS, MINLZ_FASTEST_2MB_SKIP_LOG, 0);
    }
#endif
    return minlz_encode_payload_fastest_impl(ws, dst, dst_cap, src, src_len, 13, 6, 0);
}

static size_t minlz_encode_fast_amd64_inline_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                                size_t dst_cap,
                                                const uint8_t * restrict src, size_t src_len);

static size_t minlz_encode_payload_superfast64k_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                                   size_t dst_cap,
                                                   const uint8_t * restrict src,
                                                   size_t src_len,
                                                   int accept_small_gain) {
    /* Match the small Go asm buckets: less scratch and the same probe spacing. */
    const unsigned table_bits = src_len <= 1024u ? 9u :
                                (src_len <= 4096u ? 10u :
                                 (src_len <= (16u << 10) ? 11u : 12u));
    const int skip_log = src_len <= 1024u ? 3 : 4;
    const int input_margin = 17;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    uint16_t *table;
    uint8_t *table_buf;

    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || src_len > 65536u) {
        return 0;
    }
    (void)dst_cap;
    if (minlz_workspace_prepare_encode_scratch(ws, 2, ((size_t)1u << table_bits) * sizeof(table[0]),
                                       &table_buf) != MINLZ_OK) {
        return 0;
    }
    table = (uint16_t *)table_buf;
    memset(table, 0, ((size_t)1u << table_bits) * sizeof(table[0]));
    s_limit = (int)src_len - input_margin;
    dst_limit = accept_small_gain
        ? (int)src_len - 1
        : (int)src_len - ((int)src_len >> 4) - 32;
    cv = minlz_load64(src + s);

    for (;;) {
        int candidate = 0;
        for (;;) {
            int next_s = s + ((s - next_emit) >> skip_log) + 4;
            uint32_t hash0;
            uint32_t hash1;
            uint32_t hash2;
            uint64_t cv1;
            uint64_t cv2;
            int candidate2;

            if (next_s > s_limit) {
                goto emit_remainder;
            }
            hash0 = minlz_hash8(cv, table_bits);
            cv1 = minlz_load64(src + s + 1);
            hash1 = minlz_hash8(cv1, table_bits);
            candidate = (int)table[hash0];
            candidate2 = (int)table[hash1];
            table[hash0] = (uint16_t)s;
            table[hash1] = (uint16_t)(s + 1);
            cv2 = minlz_load64(src + s + 2);
            hash2 = minlz_hash8(cv2, table_bits);

            if ((uint32_t)cv1 == minlz_load32(src + s - repeat + 1)) {
                int base = s + 1;
                int i = base - repeat;
                size_t n;
                while (base > next_emit && i > 0 && src[i - 1] == src[base - 1]) {
                    i--;
                    base--;
                }
                if (d + (base - next_emit) > dst_limit) {
                    return 0;
                }
                n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)(base - next_emit));
                d += (int)n;

                candidate = s - repeat + 5;
                s += 5;
                minlz_extend_match_int_fastest(src, &s, &candidate, (int)src_len);
                n = minlz_emit_repeat_fast(dst + d, (size_t)(s - base));
                d += (int)n;
                next_emit = s;
                if (s >= s_limit) {
                    goto emit_remainder;
                }
                cv = minlz_load64(src + s);
                continue;
            }

            if (cv == minlz_load64(src + candidate)) {
                break;
            }
            candidate = (int)table[hash2];
            if (cv1 == minlz_load64(src + candidate2)) {
                table[hash2] = (uint16_t)(s + 2);
                candidate = candidate2;
                s++;
                break;
            }
            table[hash2] = (uint16_t)(s + 2);
            if (cv2 == minlz_load64(src + candidate)) {
                s += 2;
                break;
            }

            cv = minlz_load64(src + next_s);
            s = next_s;
        }

        {
            int base = s;
            int length;
            size_t n;
            repeat = base - candidate;
            s += 8;
            candidate += 8;
            minlz_extend_match_int_fastest(src, &s, &candidate, (int)src_len);
            length = s - base;
            if (next_emit != base) {
                int lit_len = base - next_emit;
                if (lit_len > (int)MINLZ_MAX_COPY2_LITS ||
                    repeat < (int)MINLZ_MIN_COPY2_OFFSET) {
                    if (d + (base - next_emit) > dst_limit) {
                        return 0;
                    }
                    n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)lit_len);
                    d += (int)n;
                    n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
                } else {
                    n = minlz_emit_copy_lits2_fast(dst + d, src + next_emit, (size_t)lit_len,
                                                   (size_t)repeat, (size_t)length);
                }
            } else {
                n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
            }
            d += (int)n;
        }

        for (;;) {
            uint64_t x;
            uint32_t m2_hash;
            uint32_t curr_hash;
            int immediate_candidate;
            int base;
            size_t n;

            next_emit = s;
            if (s >= s_limit) {
                goto emit_remainder;
            }
            x = minlz_load64(src + s - 2);
            if (d > dst_limit) {
                return 0;
            }
            m2_hash = minlz_hash8(x, table_bits);
            x = minlz_load64(src + s);
            curr_hash = minlz_hash8(x, table_bits);
            immediate_candidate = (int)table[curr_hash];
            table[m2_hash] = (uint16_t)(s - 2);
            table[curr_hash] = (uint16_t)s;
            if (x != minlz_load64(src + immediate_candidate)) {
                cv = minlz_load64(src + s + 1);
                s++;
                break;
            }
            repeat = s - immediate_candidate;
            base = s;
            s += 8;
            immediate_candidate += 8;
            minlz_extend_match_int_fastest(
                src, &s, &immediate_candidate, (int)src_len);
            n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)(s - base));
            d += (int)n;
        }
    }

emit_remainder:
    if (next_emit < (int)src_len) {
        size_t n;
        if (d + ((int)src_len - next_emit) > dst_limit) {
            return 0;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit, src_len - (size_t)next_emit);
        d += (int)n;
    }
    if (d >= (int)src_len) {
        return 0;
    }
    return (size_t)d;
}

static size_t minlz_encode_payload_superfast_large_ws(minlz_workspace *ws,
                                                      uint8_t * restrict dst,
                                                      size_t dst_cap,
                                                      const uint8_t * restrict src,
                                                      size_t src_len) {
    const unsigned table_bits = 14u;
    const int skip_log = 5;
    const int input_margin = 17;
    const int max_copy3_offset = (int)MINLZ_MAX_COPY3_OFFSET;
    int src_size;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    uint32_t *table;
    uint8_t *table_buf;

    if (src_len <= 65536u || src_len > (size_t)INT_MAX) {
        return 0;
    }
    (void)dst_cap;
    if (minlz_workspace_prepare_encode_scratch(ws, 2, ((size_t)1u << table_bits) * sizeof(table[0]),
                                       &table_buf) != MINLZ_OK) {
        return 0;
    }
    table = (uint32_t *)table_buf;
    memset(table, 0, ((size_t)1u << table_bits) * sizeof(table[0]));
    src_size = (int)src_len;
    s_limit = src_size - input_margin;
    dst_limit = src_size - (src_size >> 3) - 17;
    cv = minlz_load64(src + s);

    for (;;) {
        int candidate = 0;
        for (;;) {
            int next_s = s + ((s - next_emit) >> skip_log) + 4;
            int min_src_pos;
            uint32_t hash0;
            uint32_t hash1;
            uint32_t hash2;
            uint64_t cv1;
            uint64_t cv2;
            int candidate2;

            if (next_s > s_limit) {
                goto emit_remainder;
            }
            min_src_pos = s - max_copy3_offset;
            hash0 = minlz_hash8(cv, table_bits);
            cv1 = minlz_load64(src + s + 1);
            hash1 = minlz_hash8(cv1, table_bits);
            candidate = (int)table[hash0];
            candidate2 = (int)table[hash1];
            table[hash0] = (uint32_t)s;
            table[hash1] = (uint32_t)(s + 1);
            cv2 = minlz_load64(src + s + 2);
            hash2 = minlz_hash8(cv2, table_bits);

            if ((uint32_t)cv1 == minlz_load32(src + s - repeat + 1)) {
                int base = s + 1;
                int i = base - repeat;
                size_t n;
                while (base > next_emit && i > 0 && src[i - 1] == src[base - 1]) {
                    i--;
                    base--;
                }
                if (d + (base - next_emit) > dst_limit) {
                    return 0;
                }
                n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)(base - next_emit));
                d += (int)n;

                candidate = s - repeat + 5;
                s += 5;
                minlz_extend_match_int_fastest(src, &s, &candidate, src_size);
                n = minlz_emit_repeat_fast(dst + d, (size_t)(s - base));
                d += (int)n;
                next_emit = s;
                if (s >= s_limit) {
                    goto emit_remainder;
                }
                cv = minlz_load64(src + s);
                continue;
            }

            if (candidate >= min_src_pos && cv == minlz_load64(src + candidate)) {
                break;
            }
            candidate = (int)table[hash2];
            if (candidate2 > min_src_pos && cv1 == minlz_load64(src + candidate2)) {
                table[hash2] = (uint32_t)(s + 2);
                candidate = candidate2;
                s++;
                break;
            }
            table[hash2] = (uint32_t)(s + 2);
            if (candidate >= min_src_pos + 2 && cv2 == minlz_load64(src + candidate)) {
                s += 2;
                break;
            }

            cv = minlz_load64(src + next_s);
            s = next_s;
        }

        {
            int base = s;
            int length;
            size_t n;
            repeat = base - candidate;
            s += 8;
            candidate += 8;
            minlz_extend_match_int_fastest(src, &s, &candidate, src_size);
            length = s - base;
            if (next_emit != base) {
                int lit_len = base - next_emit;
                if (lit_len > (int)MINLZ_MAX_COPY3_LITS ||
                    repeat < (int)MINLZ_MIN_COPY2_OFFSET) {
                    if (d + (base - next_emit) > dst_limit) {
                        return 0;
                    }
                    n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)lit_len);
                    d += (int)n;
                    n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
                } else if (repeat <= (int)MINLZ_MAX_COPY2_OFFSET) {
                    n = minlz_emit_copy_lits2_fast(dst + d, src + next_emit, (size_t)lit_len,
                                                   (size_t)repeat, (size_t)length);
                } else {
                    n = minlz_emit_copy_lits3_fast(dst + d, src + next_emit, (size_t)lit_len,
                                                   (size_t)repeat, (size_t)length);
                }
            } else {
                n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
            }
            d += (int)n;
        }

        for (;;) {
            uint64_t x;
            uint32_t m2_hash;
            uint32_t curr_hash;
            int immediate_candidate;
            int base;
            size_t n;

            next_emit = s;
            if (s >= s_limit) {
                goto emit_remainder;
            }
            x = minlz_load64(src + s - 2);
            if (d > dst_limit) {
                return 0;
            }
            m2_hash = minlz_hash8(x, table_bits);
            x = minlz_load64(src + s);
            curr_hash = minlz_hash8(x, table_bits);
            immediate_candidate = (int)table[curr_hash];
            table[m2_hash] = (uint32_t)(s - 2);
            table[curr_hash] = (uint32_t)s;
            if (s - immediate_candidate > max_copy3_offset ||
                x != minlz_load64(src + immediate_candidate)) {
                cv = minlz_load64(src + s + 1);
                s++;
                break;
            }
            repeat = s - immediate_candidate;
            base = s;
            s += 8;
            immediate_candidate += 8;
            minlz_extend_match_int_fastest(
                src, &s, &immediate_candidate, src_size);
            n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)(s - base));
            d += (int)n;
        }
    }

emit_remainder:
    if (next_emit < src_size) {
        size_t n;
        if (d + (src_size - next_emit) > dst_limit) {
            return 0;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit, src_len - (size_t)next_emit);
        d += (int)n;
    }
    if (d >= src_size) {
        return 0;
    }
    return (size_t)d;
}

static size_t minlz_encode_payload_balanced_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                               size_t dst_cap,
                                               const uint8_t * restrict src, size_t src_len);
static size_t minlz_encode_payload_smallest_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                               size_t dst_cap,
                                               const uint8_t * restrict src, size_t src_len);
static size_t minlz_encode_better_amd64_inline_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                                  size_t dst_cap,
                                                  const uint8_t * restrict src, size_t src_len);

static MINLZ_ALWAYS_INLINE int minlz_periodic_equal_avx2_fast(const uint8_t *a,
                                                              const uint8_t *b,
                                                              size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (!minlz_backend_has_avx2() || len < (1u << 20)) {
        return -1;
    }
#if MINLZ_PERIODIC_EQUAL_AVX2_512
    while (len >= 512u) {
        unsigned char nonzero;
        __asm__ volatile(
            "vmovdqu 0(%[a]), %%ymm0\n\t"
            "vmovdqu 32(%[a]), %%ymm1\n\t"
            "vmovdqu 64(%[a]), %%ymm2\n\t"
            "vmovdqu 96(%[a]), %%ymm3\n\t"
            "vmovdqu 128(%[a]), %%ymm4\n\t"
            "vmovdqu 160(%[a]), %%ymm5\n\t"
            "vmovdqu 192(%[a]), %%ymm6\n\t"
            "vmovdqu 224(%[a]), %%ymm7\n\t"
            "vmovdqu 256(%[a]), %%ymm8\n\t"
            "vmovdqu 288(%[a]), %%ymm9\n\t"
            "vmovdqu 320(%[a]), %%ymm10\n\t"
            "vmovdqu 352(%[a]), %%ymm11\n\t"
            "vmovdqu 384(%[a]), %%ymm12\n\t"
            "vmovdqu 416(%[a]), %%ymm13\n\t"
            "vmovdqu 448(%[a]), %%ymm14\n\t"
            "vmovdqu 480(%[a]), %%ymm15\n\t"
            "vpxor 0(%[b]), %%ymm0, %%ymm0\n\t"
            "vpxor 32(%[b]), %%ymm1, %%ymm1\n\t"
            "vpxor 64(%[b]), %%ymm2, %%ymm2\n\t"
            "vpxor 96(%[b]), %%ymm3, %%ymm3\n\t"
            "vpxor 128(%[b]), %%ymm4, %%ymm4\n\t"
            "vpxor 160(%[b]), %%ymm5, %%ymm5\n\t"
            "vpxor 192(%[b]), %%ymm6, %%ymm6\n\t"
            "vpxor 224(%[b]), %%ymm7, %%ymm7\n\t"
            "vpxor 256(%[b]), %%ymm8, %%ymm8\n\t"
            "vpxor 288(%[b]), %%ymm9, %%ymm9\n\t"
            "vpxor 320(%[b]), %%ymm10, %%ymm10\n\t"
            "vpxor 352(%[b]), %%ymm11, %%ymm11\n\t"
            "vpxor 384(%[b]), %%ymm12, %%ymm12\n\t"
            "vpxor 416(%[b]), %%ymm13, %%ymm13\n\t"
            "vpxor 448(%[b]), %%ymm14, %%ymm14\n\t"
            "vpxor 480(%[b]), %%ymm15, %%ymm15\n\t"
            "vpor %%ymm1, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm3, %%ymm2, %%ymm2\n\t"
            "vpor %%ymm5, %%ymm4, %%ymm4\n\t"
            "vpor %%ymm7, %%ymm6, %%ymm6\n\t"
            "vpor %%ymm9, %%ymm8, %%ymm8\n\t"
            "vpor %%ymm11, %%ymm10, %%ymm10\n\t"
            "vpor %%ymm13, %%ymm12, %%ymm12\n\t"
            "vpor %%ymm15, %%ymm14, %%ymm14\n\t"
            "vpor %%ymm2, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm6, %%ymm4, %%ymm4\n\t"
            "vpor %%ymm10, %%ymm8, %%ymm8\n\t"
            "vpor %%ymm14, %%ymm12, %%ymm12\n\t"
            "vpor %%ymm4, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm12, %%ymm8, %%ymm8\n\t"
            "vpor %%ymm8, %%ymm0, %%ymm0\n\t"
            "vptest %%ymm0, %%ymm0\n\t"
            "setnz %[nonzero]"
            : [nonzero] "=qm"(nonzero)
            : [a] "r"(a), [b] "r"(b)
            : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
              "ymm8", "ymm9", "ymm10", "ymm11", "ymm12", "ymm13", "ymm14", "ymm15",
              "cc", "memory");
        if (nonzero) {
            __asm__ volatile("vzeroupper" ::: "memory");
            return 0;
        }
        a += 512u;
        b += 512u;
        len -= 512u;
    }
#endif
    while (len >= 256u) {
        unsigned char nonzero;
        __asm__ volatile(
            "vmovdqu 0(%[a]), %%ymm0\n\t"
            "vmovdqu 32(%[a]), %%ymm1\n\t"
            "vmovdqu 64(%[a]), %%ymm2\n\t"
            "vmovdqu 96(%[a]), %%ymm3\n\t"
            "vmovdqu 128(%[a]), %%ymm4\n\t"
            "vmovdqu 160(%[a]), %%ymm5\n\t"
            "vmovdqu 192(%[a]), %%ymm6\n\t"
            "vmovdqu 224(%[a]), %%ymm7\n\t"
            "vpxor 0(%[b]), %%ymm0, %%ymm0\n\t"
            "vpxor 32(%[b]), %%ymm1, %%ymm1\n\t"
            "vpxor 64(%[b]), %%ymm2, %%ymm2\n\t"
            "vpxor 96(%[b]), %%ymm3, %%ymm3\n\t"
            "vpxor 128(%[b]), %%ymm4, %%ymm4\n\t"
            "vpxor 160(%[b]), %%ymm5, %%ymm5\n\t"
            "vpxor 192(%[b]), %%ymm6, %%ymm6\n\t"
            "vpxor 224(%[b]), %%ymm7, %%ymm7\n\t"
            "vpor %%ymm1, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm2, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm3, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm4, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm5, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm6, %%ymm0, %%ymm0\n\t"
            "vpor %%ymm7, %%ymm0, %%ymm0\n\t"
            "vptest %%ymm0, %%ymm0\n\t"
            "setnz %[nonzero]"
            : [nonzero] "=qm"(nonzero)
            : [a] "r"(a), [b] "r"(b)
            : "ymm0", "ymm1", "ymm2", "ymm3", "ymm4", "ymm5", "ymm6", "ymm7",
              "cc", "memory");
        if (nonzero) {
            __asm__ volatile("vzeroupper" ::: "memory");
            return 0;
        }
        a += 256u;
        b += 256u;
        len -= 256u;
    }
    __asm__ volatile("vzeroupper" ::: "memory");
    while (len >= 8u) {
        if (minlz_load64(a) != minlz_load64(b)) {
            return 0;
        }
        a += 8u;
        b += 8u;
        len -= 8u;
    }
    while (len != 0u) {
        if (*a++ != *b++) {
            return 0;
        }
        len--;
    }
    return 1;
#else
    (void)a;
    (void)b;
    (void)len;
    return -1;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_periodic_equal_sse42_xor_fast(const uint8_t *a,
                                                                   const uint8_t *b,
                                                                   size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (!minlz_backend_has_sse42() || len < MINLZ_PERIODIC_EQUAL_SSE42_XOR_MIN) {
        return -1;
    }
#if MINLZ_PERIODIC_EQUAL_SSE42_ACCUM
    {
        /*
         * The accumulator deliberately tests only once after the long loop.
         * Reject accidental 8-byte period candidates first so corrupt/random
         * inputs do not pay for a full multi-megabyte comparison.
         */
        if (memcmp(a, b, 4096u) != 0) {
            return 0;
        }
        a += 4096u;
        b += 4096u;
        len -= 4096u;
        const uint8_t *pa = a;
        const uint8_t *pb = b;
        size_t chunks = len >> 8;
        unsigned char nonzero;
        __asm__ volatile(
            "pxor %%xmm0, %%xmm0\n\t"
            "pxor %%xmm1, %%xmm1\n\t"
            "pxor %%xmm2, %%xmm2\n\t"
            "pxor %%xmm3, %%xmm3\n\t"
            "pxor %%xmm4, %%xmm4\n\t"
            "pxor %%xmm5, %%xmm5\n\t"
            "pxor %%xmm6, %%xmm6\n\t"
            "pxor %%xmm7, %%xmm7\n\t"
            "testq %[chunks], %[chunks]\n\t"
            "jz 2f\n\t"
            ".p2align 4\n\t"
            "1:\n\t"
            "movdqu 0(%[pa]), %%xmm8\n\t"
            "movdqu 16(%[pa]), %%xmm9\n\t"
            "movdqu 32(%[pa]), %%xmm10\n\t"
            "movdqu 48(%[pa]), %%xmm11\n\t"
            "movdqu 64(%[pa]), %%xmm12\n\t"
            "movdqu 80(%[pa]), %%xmm13\n\t"
            "movdqu 96(%[pa]), %%xmm14\n\t"
            "movdqu 112(%[pa]), %%xmm15\n\t"
            "pxor 0(%[pb]), %%xmm8\n\t"
            "pxor 16(%[pb]), %%xmm9\n\t"
            "pxor 32(%[pb]), %%xmm10\n\t"
            "pxor 48(%[pb]), %%xmm11\n\t"
            "pxor 64(%[pb]), %%xmm12\n\t"
            "pxor 80(%[pb]), %%xmm13\n\t"
            "pxor 96(%[pb]), %%xmm14\n\t"
            "pxor 112(%[pb]), %%xmm15\n\t"
            "por %%xmm8, %%xmm0\n\t"
            "por %%xmm9, %%xmm1\n\t"
            "por %%xmm10, %%xmm2\n\t"
            "por %%xmm11, %%xmm3\n\t"
            "por %%xmm12, %%xmm4\n\t"
            "por %%xmm13, %%xmm5\n\t"
            "por %%xmm14, %%xmm6\n\t"
            "por %%xmm15, %%xmm7\n\t"
            "movdqu 128(%[pa]), %%xmm8\n\t"
            "movdqu 144(%[pa]), %%xmm9\n\t"
            "movdqu 160(%[pa]), %%xmm10\n\t"
            "movdqu 176(%[pa]), %%xmm11\n\t"
            "movdqu 192(%[pa]), %%xmm12\n\t"
            "movdqu 208(%[pa]), %%xmm13\n\t"
            "movdqu 224(%[pa]), %%xmm14\n\t"
            "movdqu 240(%[pa]), %%xmm15\n\t"
            "pxor 128(%[pb]), %%xmm8\n\t"
            "pxor 144(%[pb]), %%xmm9\n\t"
            "pxor 160(%[pb]), %%xmm10\n\t"
            "pxor 176(%[pb]), %%xmm11\n\t"
            "pxor 192(%[pb]), %%xmm12\n\t"
            "pxor 208(%[pb]), %%xmm13\n\t"
            "pxor 224(%[pb]), %%xmm14\n\t"
            "pxor 240(%[pb]), %%xmm15\n\t"
            "por %%xmm8, %%xmm0\n\t"
            "por %%xmm9, %%xmm1\n\t"
            "por %%xmm10, %%xmm2\n\t"
            "por %%xmm11, %%xmm3\n\t"
            "por %%xmm12, %%xmm4\n\t"
            "por %%xmm13, %%xmm5\n\t"
            "por %%xmm14, %%xmm6\n\t"
            "por %%xmm15, %%xmm7\n\t"
            "addq $256, %[pa]\n\t"
            "addq $256, %[pb]\n\t"
            "decq %[chunks]\n\t"
            "jnz 1b\n\t"
            "2:\n\t"
            "por %%xmm1, %%xmm0\n\t"
            "por %%xmm3, %%xmm2\n\t"
            "por %%xmm5, %%xmm4\n\t"
            "por %%xmm7, %%xmm6\n\t"
            "por %%xmm2, %%xmm0\n\t"
            "por %%xmm6, %%xmm4\n\t"
            "por %%xmm4, %%xmm0\n\t"
            "ptest %%xmm0, %%xmm0\n\t"
            "setnz %[nonzero]"
            : [pa] "+r"(pa), [pb] "+r"(pb), [chunks] "+r"(chunks),
              [nonzero] "=qm"(nonzero)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",
              "xmm15", "cc", "memory");
        if (nonzero) {
            return 0;
        }
        a = pa;
        b = pb;
        len &= 255u;
    }
#else
    if (len >= 4096u) {
        const uint8_t *pa = a;
        const uint8_t *pb = b;
        size_t chunks = len >> 12;
        uintptr_t mismatch;
        __asm__ volatile(
            "testq %[chunks], %[chunks]\n\t"
            "jz 3f\n\t"
            ".p2align 4\n\t"
            "1:\n\t"
            "pxor %%xmm0, %%xmm0\n\t"
            "pxor %%xmm1, %%xmm1\n\t"
            "pxor %%xmm2, %%xmm2\n\t"
            "pxor %%xmm3, %%xmm3\n\t"
            "pxor %%xmm4, %%xmm4\n\t"
            "pxor %%xmm5, %%xmm5\n\t"
            "pxor %%xmm6, %%xmm6\n\t"
            "pxor %%xmm7, %%xmm7\n\t"
            ".rept 32\n\t"
            "movdqu 0(%[pa]), %%xmm8\n\t"
            "movdqu 16(%[pa]), %%xmm9\n\t"
            "movdqu 32(%[pa]), %%xmm10\n\t"
            "movdqu 48(%[pa]), %%xmm11\n\t"
            "movdqu 64(%[pa]), %%xmm12\n\t"
            "movdqu 80(%[pa]), %%xmm13\n\t"
            "movdqu 96(%[pa]), %%xmm14\n\t"
            "movdqu 112(%[pa]), %%xmm15\n\t"
            "pxor 0(%[pb]), %%xmm8\n\t"
            "pxor 16(%[pb]), %%xmm9\n\t"
            "pxor 32(%[pb]), %%xmm10\n\t"
            "pxor 48(%[pb]), %%xmm11\n\t"
            "pxor 64(%[pb]), %%xmm12\n\t"
            "pxor 80(%[pb]), %%xmm13\n\t"
            "pxor 96(%[pb]), %%xmm14\n\t"
            "pxor 112(%[pb]), %%xmm15\n\t"
            "por %%xmm8, %%xmm0\n\t"
            "por %%xmm9, %%xmm1\n\t"
            "por %%xmm10, %%xmm2\n\t"
            "por %%xmm11, %%xmm3\n\t"
            "por %%xmm12, %%xmm4\n\t"
            "por %%xmm13, %%xmm5\n\t"
            "por %%xmm14, %%xmm6\n\t"
            "por %%xmm15, %%xmm7\n\t"
            "addq $128, %[pa]\n\t"
            "addq $128, %[pb]\n\t"
            ".endr\n\t"
            "por %%xmm1, %%xmm0\n\t"
            "por %%xmm3, %%xmm2\n\t"
            "por %%xmm5, %%xmm4\n\t"
            "por %%xmm7, %%xmm6\n\t"
            "por %%xmm2, %%xmm0\n\t"
            "por %%xmm6, %%xmm4\n\t"
            "por %%xmm4, %%xmm0\n\t"
            "ptest %%xmm0, %%xmm0\n\t"
            "jnz 2f\n\t"
            "decq %[chunks]\n\t"
            "jnz 1b\n\t"
            "xorq %[mismatch], %[mismatch]\n\t"
            "jmp 3f\n\t"
            "2:\n\t"
            "movq $1, %[mismatch]\n\t"
            "3:"
            : [pa] "+r"(pa), [pb] "+r"(pb), [chunks] "+r"(chunks),
              [mismatch] "=&r"(mismatch)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6",
              "xmm7", "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13",
              "xmm14", "xmm15", "cc", "memory");
        if (mismatch != 0u) {
            return 0;
        }
        a = pa;
        b = pb;
        len &= 4095u;
    }
    while (len >= 256u) {
        unsigned char nonzero;
        __asm__ volatile(
            "movdqu 0(%[a]), %%xmm0\n\t"
            "movdqu 16(%[a]), %%xmm1\n\t"
            "movdqu 32(%[a]), %%xmm2\n\t"
            "movdqu 48(%[a]), %%xmm3\n\t"
            "movdqu 64(%[a]), %%xmm4\n\t"
            "movdqu 80(%[a]), %%xmm5\n\t"
            "movdqu 96(%[a]), %%xmm6\n\t"
            "movdqu 112(%[a]), %%xmm7\n\t"
            "pxor 0(%[b]), %%xmm0\n\t"
            "pxor 16(%[b]), %%xmm1\n\t"
            "pxor 32(%[b]), %%xmm2\n\t"
            "pxor 48(%[b]), %%xmm3\n\t"
            "pxor 64(%[b]), %%xmm4\n\t"
            "pxor 80(%[b]), %%xmm5\n\t"
            "pxor 96(%[b]), %%xmm6\n\t"
            "pxor 112(%[b]), %%xmm7\n\t"
            "por %%xmm1, %%xmm0\n\t"
            "por %%xmm3, %%xmm2\n\t"
            "por %%xmm5, %%xmm4\n\t"
            "por %%xmm7, %%xmm6\n\t"
            "por %%xmm2, %%xmm0\n\t"
            "por %%xmm6, %%xmm4\n\t"
            "por %%xmm4, %%xmm0\n\t"
            "movdqu 128(%[a]), %%xmm1\n\t"
            "movdqu 144(%[a]), %%xmm2\n\t"
            "movdqu 160(%[a]), %%xmm3\n\t"
            "movdqu 176(%[a]), %%xmm4\n\t"
            "movdqu 192(%[a]), %%xmm5\n\t"
            "movdqu 208(%[a]), %%xmm6\n\t"
            "movdqu 224(%[a]), %%xmm7\n\t"
            "movdqu 240(%[a]), %%xmm8\n\t"
            "pxor 128(%[b]), %%xmm1\n\t"
            "pxor 144(%[b]), %%xmm2\n\t"
            "pxor 160(%[b]), %%xmm3\n\t"
            "pxor 176(%[b]), %%xmm4\n\t"
            "pxor 192(%[b]), %%xmm5\n\t"
            "pxor 208(%[b]), %%xmm6\n\t"
            "pxor 224(%[b]), %%xmm7\n\t"
            "pxor 240(%[b]), %%xmm8\n\t"
            "por %%xmm2, %%xmm1\n\t"
            "por %%xmm4, %%xmm3\n\t"
            "por %%xmm6, %%xmm5\n\t"
            "por %%xmm8, %%xmm7\n\t"
            "por %%xmm3, %%xmm1\n\t"
            "por %%xmm7, %%xmm5\n\t"
            "por %%xmm5, %%xmm1\n\t"
            "por %%xmm1, %%xmm0\n\t"
            "ptest %%xmm0, %%xmm0\n\t"
            "setnz %[nonzero]"
            : [nonzero] "=qm"(nonzero)
            : [a] "r"(a), [b] "r"(b)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "xmm8", "cc", "memory");
        if (nonzero) {
            return 0;
        }
        a += 256u;
        b += 256u;
        len -= 256u;
    }
#endif
    while (len >= 16u) {
        unsigned char nonzero;
        __asm__ volatile(
            "movdqu (%[a]), %%xmm0\n\t"
            "pxor (%[b]), %%xmm0\n\t"
            "ptest %%xmm0, %%xmm0\n\t"
            "setnz %[nonzero]"
            : [nonzero] "=qm"(nonzero)
            : [a] "r"(a), [b] "r"(b)
            : "xmm0", "cc", "memory");
        if (nonzero) {
            return 0;
        }
        a += 16u;
        b += 16u;
        len -= 16u;
    }
    while (len != 0u) {
        if (*a++ != *b++) {
            return 0;
        }
        len--;
    }
    return 1;
#else
    (void)a;
    (void)b;
    (void)len;
    return -1;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_periodic_equal_match16_asm(const uint8_t *a,
                                                                 const uint8_t *b,
                                                                 size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    size_t chunks = len >> 4;
    uintptr_t mismatch;
    if (!minlz_backend_has_avx2() || len < (1u << 20)) {
        return -1;
    }
    __asm__ volatile(
        "testq %[chunks], %[chunks]\n\t"
        "jz 2f\n\t"
        "1:\n\t"
        "movq (%[pa]), %%r8\n\t"
        "xorq (%[pb]), %%r8\n\t"
        "jnz 3f\n\t"
        "movq 8(%[pa]), %%r9\n\t"
        "xorq 8(%[pb]), %%r9\n\t"
        "jnz 3f\n\t"
        "addq $16, %[pa]\n\t"
        "addq $16, %[pb]\n\t"
        "decq %[chunks]\n\t"
        "jnz 1b\n\t"
        "2:\n\t"
        "xorq %[mismatch], %[mismatch]\n\t"
        "jmp 4f\n\t"
        "3:\n\t"
        "movq $1, %[mismatch]\n\t"
        "4:"
        : [pa] "+r"(pa), [pb] "+r"(pb), [chunks] "+r"(chunks),
          [mismatch] "=&r"(mismatch)
        :
        : "r8", "r9", "cc", "memory");
    if (mismatch != 0u) {
        return 0;
    }
    len &= 15u;
    while (len != 0u) {
        if (*pa++ != *pb++) {
            return 0;
        }
        len--;
    }
    return 1;
#else
    (void)a;
    (void)b;
    (void)len;
    return -1;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_periodic_equal_match32_asm(const uint8_t *a,
                                                                 const uint8_t *b,
                                                                 size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    const uint8_t *pa = a;
    const uint8_t *pb = b;
    size_t chunks = len >> 5;
    uintptr_t mismatch;
    if (!(minlz_backend_has_avx2() ||
          minlz_select_backend() == MINLZ_BACKEND_SSE42) ||
        len < (1u << 20)) {
        return -1;
    }
    __asm__ volatile(
        "testq %[chunks], %[chunks]\n\t"
        "jz 2f\n\t"
        "1:\n\t"
        "movq 0(%[pa]), %%r8\n\t"
        "movq 8(%[pa]), %%r9\n\t"
        "movq 16(%[pa]), %%r10\n\t"
        "movq 24(%[pa]), %%r11\n\t"
        "xorq 0(%[pb]), %%r8\n\t"
        "xorq 8(%[pb]), %%r9\n\t"
        "xorq 16(%[pb]), %%r10\n\t"
        "xorq 24(%[pb]), %%r11\n\t"
        "orq %%r9, %%r8\n\t"
        "orq %%r10, %%r8\n\t"
        "orq %%r11, %%r8\n\t"
        "jnz 3f\n\t"
        "addq $32, %[pa]\n\t"
        "addq $32, %[pb]\n\t"
        "decq %[chunks]\n\t"
        "jnz 1b\n\t"
        "2:\n\t"
        "xorq %[mismatch], %[mismatch]\n\t"
        "jmp 4f\n\t"
        "3:\n\t"
        "movq $1, %[mismatch]\n\t"
        "4:"
        : [pa] "+r"(pa), [pb] "+r"(pb), [chunks] "+r"(chunks),
          [mismatch] "=&r"(mismatch)
        :
        : "r8", "r9", "r10", "r11", "cc", "memory");
    if (mismatch != 0u) {
        return 0;
    }
    len &= 31u;
    while (len != 0u) {
        if (*pa++ != *pb++) {
            return 0;
        }
        len--;
    }
    return 1;
#else
    (void)a;
    (void)b;
    (void)len;
    return -1;
#endif
}

static MINLZ_ALWAYS_INLINE int minlz_periodic_equal_fast(const uint8_t *a,
                                                         const uint8_t *b,
                                                         size_t len) {
    /* Both ranges may be unaligned. Let libc select the implementation
     * for large comparisons on the current CPU. */
    if (len >= MINLZ_PERIODIC_EQUAL_MEMCMP_MIN) {
        return memcmp(a, b, len) == 0;
    }
#if MINLZ_PERIODIC_EQUAL_AVX2_512
    if (minlz_backend_has_avx2()) {
        int avx2_equal = minlz_periodic_equal_avx2_fast(a, b, len);
        if (avx2_equal >= 0) {
            return avx2_equal;
        }
    }
#endif
#if MINLZ_PERIODIC_EQUAL_MATCH32_ASM
    {
        int match32_equal = minlz_periodic_equal_match32_asm(a, b, len);
        if (match32_equal >= 0) {
            return match32_equal;
        }
    }
#endif
#if MINLZ_PERIODIC_EQUAL_MATCH16_ASM
    {
        int match16_equal = minlz_periodic_equal_match16_asm(a, b, len);
        if (match16_equal >= 0) {
            return match16_equal;
        }
    }
#endif
    {
        int avx2_equal = minlz_periodic_equal_avx2_fast(a, b, len);
        if (avx2_equal >= 0) {
            return avx2_equal;
        }
    }
    {
        int sse42_equal = minlz_periodic_equal_sse42_xor_fast(a, b, len);
        if (sse42_equal >= 0) {
            return sse42_equal;
        }
    }
#if MINLZ_HAVE_INLINE_ASM_AMD64 && MINLZ_PERIODIC_EQUAL_REP_CMPSQ
    if (len >= (1u << 20)) {
        const uint8_t *pa = a;
        const uint8_t *pb = b;
        size_t qwords = len >> 3;
        unsigned char equal;
        __asm__ volatile(
            "repe cmpsq\n\t"
            "sete %[equal]"
            : [equal] "=q"(equal), "+S"(pa), "+D"(pb), "+c"(qwords)
            :
            : "cc", "memory");
        if (!equal) {
            return 0;
        }
        a += len & ~(size_t)7u;
        b += len & ~(size_t)7u;
        len &= 7u;
    }
#endif
#if MINLZ_HAVE_INLINE_ASM_AMD64
    while (len >= 256u) {
        int mask0;
        int mask1;
        __asm__ volatile(
            "prefetcht0 768(%[a])\n\t"
            "prefetcht0 768(%[b])\n\t"
            "movdqu 0(%[a]), %%xmm0\n\t"
            "movdqu 16(%[a]), %%xmm1\n\t"
            "movdqu 32(%[a]), %%xmm2\n\t"
            "movdqu 48(%[a]), %%xmm3\n\t"
            "movdqu 64(%[a]), %%xmm4\n\t"
            "movdqu 80(%[a]), %%xmm5\n\t"
            "movdqu 96(%[a]), %%xmm6\n\t"
            "movdqu 112(%[a]), %%xmm7\n\t"
            "pcmpeqb 0(%[b]), %%xmm0\n\t"
            "pcmpeqb 16(%[b]), %%xmm1\n\t"
            "pcmpeqb 32(%[b]), %%xmm2\n\t"
            "pcmpeqb 48(%[b]), %%xmm3\n\t"
            "pcmpeqb 64(%[b]), %%xmm4\n\t"
            "pcmpeqb 80(%[b]), %%xmm5\n\t"
            "pcmpeqb 96(%[b]), %%xmm6\n\t"
            "pcmpeqb 112(%[b]), %%xmm7\n\t"
            "pand %%xmm1, %%xmm0\n\t"
            "pand %%xmm2, %%xmm0\n\t"
            "pand %%xmm3, %%xmm0\n\t"
            "pand %%xmm4, %%xmm0\n\t"
            "pand %%xmm5, %%xmm0\n\t"
            "pand %%xmm6, %%xmm0\n\t"
            "pand %%xmm7, %%xmm0\n\t"
            "pmovmskb %%xmm0, %[mask0]\n\t"
            "movdqu 128(%[a]), %%xmm0\n\t"
            "movdqu 144(%[a]), %%xmm1\n\t"
            "movdqu 160(%[a]), %%xmm2\n\t"
            "movdqu 176(%[a]), %%xmm3\n\t"
            "movdqu 192(%[a]), %%xmm4\n\t"
            "movdqu 208(%[a]), %%xmm5\n\t"
            "movdqu 224(%[a]), %%xmm6\n\t"
            "movdqu 240(%[a]), %%xmm7\n\t"
            "pcmpeqb 128(%[b]), %%xmm0\n\t"
            "pcmpeqb 144(%[b]), %%xmm1\n\t"
            "pcmpeqb 160(%[b]), %%xmm2\n\t"
            "pcmpeqb 176(%[b]), %%xmm3\n\t"
            "pcmpeqb 192(%[b]), %%xmm4\n\t"
            "pcmpeqb 208(%[b]), %%xmm5\n\t"
            "pcmpeqb 224(%[b]), %%xmm6\n\t"
            "pcmpeqb 240(%[b]), %%xmm7\n\t"
            "pand %%xmm1, %%xmm0\n\t"
            "pand %%xmm2, %%xmm0\n\t"
            "pand %%xmm3, %%xmm0\n\t"
            "pand %%xmm4, %%xmm0\n\t"
            "pand %%xmm5, %%xmm0\n\t"
            "pand %%xmm6, %%xmm0\n\t"
            "pand %%xmm7, %%xmm0\n\t"
            "pmovmskb %%xmm0, %[mask1]"
            : [mask0] "=&r"(mask0), [mask1] "=&r"(mask1)
            : [a] "r"(a), [b] "r"(b)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "cc", "memory");
        if ((mask0 & mask1) != 0xffff) {
            return 0;
        }
        a += 256u;
        b += 256u;
        len -= 256u;
    }
    while (len >= 128u) {
        int mask;
        __asm__ volatile(
            "prefetcht0 512(%[a])\n\t"
            "prefetcht0 512(%[b])\n\t"
            "movdqu 0(%[a]), %%xmm0\n\t"
            "movdqu 16(%[a]), %%xmm1\n\t"
            "movdqu 32(%[a]), %%xmm2\n\t"
            "movdqu 48(%[a]), %%xmm3\n\t"
            "movdqu 64(%[a]), %%xmm4\n\t"
            "movdqu 80(%[a]), %%xmm5\n\t"
            "movdqu 96(%[a]), %%xmm6\n\t"
            "movdqu 112(%[a]), %%xmm7\n\t"
            "pcmpeqb 0(%[b]), %%xmm0\n\t"
            "pcmpeqb 16(%[b]), %%xmm1\n\t"
            "pcmpeqb 32(%[b]), %%xmm2\n\t"
            "pcmpeqb 48(%[b]), %%xmm3\n\t"
            "pcmpeqb 64(%[b]), %%xmm4\n\t"
            "pcmpeqb 80(%[b]), %%xmm5\n\t"
            "pcmpeqb 96(%[b]), %%xmm6\n\t"
            "pcmpeqb 112(%[b]), %%xmm7\n\t"
            "pand %%xmm1, %%xmm0\n\t"
            "pand %%xmm2, %%xmm0\n\t"
            "pand %%xmm3, %%xmm0\n\t"
            "pand %%xmm4, %%xmm0\n\t"
            "pand %%xmm5, %%xmm0\n\t"
            "pand %%xmm6, %%xmm0\n\t"
            "pand %%xmm7, %%xmm0\n\t"
            "pmovmskb %%xmm0, %[mask]"
            : [mask] "=r"(mask)
            : [a] "r"(a), [b] "r"(b)
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "cc", "memory");
        if (mask != 0xffff) {
            return 0;
        }
        a += 128u;
        b += 128u;
        len -= 128u;
    }
    while (len >= 64u) {
        int mask;
        __asm__ volatile(
            "movdqu 0(%[a]), %%xmm0\n\t"
            "movdqu 16(%[a]), %%xmm1\n\t"
            "movdqu 32(%[a]), %%xmm2\n\t"
            "movdqu 48(%[a]), %%xmm3\n\t"
            "pcmpeqb 0(%[b]), %%xmm0\n\t"
            "pcmpeqb 16(%[b]), %%xmm1\n\t"
            "pcmpeqb 32(%[b]), %%xmm2\n\t"
            "pcmpeqb 48(%[b]), %%xmm3\n\t"
            "pand %%xmm1, %%xmm0\n\t"
            "pand %%xmm2, %%xmm0\n\t"
            "pand %%xmm3, %%xmm0\n\t"
            "pmovmskb %%xmm0, %[mask]"
            : [mask] "=r"(mask)
            : [a] "r"(a), [b] "r"(b)
            : "xmm0", "xmm1", "xmm2", "xmm3", "cc", "memory");
        if (mask != 0xffff) {
            return 0;
        }
        a += 64u;
        b += 64u;
        len -= 64u;
    }
#endif
    while (len >= 8u) {
        if (minlz_load64(a) != minlz_load64(b)) {
            return 0;
        }
        a += 8u;
        b += 8u;
        len -= 8u;
    }
    while (len != 0u) {
        if (*a++ != *b++) {
            return 0;
        }
        len--;
    }
    return 1;
}

static size_t minlz_encode_payload_periodic_repeat_ws(minlz_workspace *ws,
                                                      uint8_t * restrict dst, size_t dst_cap,
                                                      const uint8_t * restrict src,
                                                      size_t src_len, int level) {
    const size_t max_period = 65535u;
    size_t limit;
    size_t period;
    uint64_t first;

    if (src_len < (2u << 20) || dst_cap < 16u) {
        return 0;
    }
    limit = src_len / 2u;
    if (limit > max_period) {
        limit = max_period;
    }
    if (limit < 8u) {
        return 0;
    }
    first = minlz_load64(src);
    const uint64_t last = minlz_load64(src + src_len - 8u);
    for (period = 1; period <= limit;) {
        size_t n;
        size_t m;
#if MINLZ_ENABLE_PERIODIC_SEARCH_MEMCHR
        const uint8_t *hit = (const uint8_t *)memchr(src + period, src[0],
                                                     limit - period + 1u);
        if (hit == NULL) {
            break;
        }
        period = (size_t)(hit - src);
#endif
        /* A near-periodic block can match at many offsets. Check its tail
         * before comparing the entire block at each candidate offset. */
        if (minlz_load64(src + period) != first ||
            minlz_load64(src + src_len - period - 8u) != last ||
            !minlz_periodic_equal_fast(src + period, src, src_len - period)) {
            period++;
            continue;
        }
        switch (level) {
        case MINLZ_LEVEL_SUPERFAST:
            n = minlz_encode_payload_superfast64k_ws(ws, dst, dst_cap, src, period, 1);
            if (n == 0) {
                n = minlz_emit_literal_fast(dst, src, period);
            }
            break;
        case MINLZ_LEVEL_FASTEST:
#if MINLZ_ENABLE_L1_PERIODIC_FAST16K_INLINE && MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM
            if (period > (4u << 10) && period <= (16u << 10) &&
                minlz_backend_has_inline_asm_amd64()) {
                uint8_t *scratch = NULL;
                if (minlz_workspace_prepare_encode_scratch(ws, 2, 8192u, &scratch) == MINLZ_OK) {
                    n = minlz_encode_block_16k_inline_asm(dst, src, period, scratch);
                    if (n != 0) {
                        break;
                    }
                }
            }
#endif
            n = minlz_encode_payload_fastest64k_ws(ws, dst, dst_cap, src, period);
            break;
        case MINLZ_LEVEL_BALANCED:
#if MINLZ_ENABLE_L2_PERIODIC_REPEAT_FAST
            if (period > (4u << 10) && period <= (16u << 10) &&
                minlz_backend_has_inline_asm_amd64()) {
                uint8_t *scratch = NULL;
                if (minlz_workspace_prepare_encode_scratch(ws, 2, 36864u, &scratch) == MINLZ_OK) {
                    n = minlz_encode_better_block_16k_inline_asm(
                        dst, src, period, scratch, 0u);
                    if (n == 0) {
                        n = minlz_encode_better_amd64_inline_ws(ws, dst, dst_cap, src, period);
                    }
                } else {
                    n = 0;
                }
            } else
#endif
            n = minlz_encode_better_amd64_inline_ws(ws, dst, dst_cap, src, period);
            if (n == 0) {
                n = minlz_encode_payload_balanced_ws(ws, dst, dst_cap, src, period);
            }
            break;
        case MINLZ_LEVEL_SMALLEST:
            n = minlz_encode_payload_smallest_ws(ws, dst, dst_cap, src, period);
            break;
        default:
            n = 0;
            break;
        }
        if (n == 0 || n >= dst_cap || dst_cap - n < 8u) {
            return 0;
        }
        m = minlz_emit_periodic_copy_tail_fast(dst + n, period, src_len - period);
        if (m != 0 && n + m < src_len) {
            return n + m;
        }
        return 0;
    }
    return 0;
}

static size_t minlz_encode_balanced_tiny_repeat4(uint8_t * restrict dst, size_t dst_cap,
                                                 const uint8_t * restrict src,
                                                 size_t src_len);

static size_t minlz_encode_payload_superfast_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                                size_t dst_cap,
                                                const uint8_t * restrict src, size_t src_len) {
    size_t n;
#if MINLZ_ENABLE_SUPERFAST2MB_GOASM && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    if (src_len > (512u << 10) && src_len <= (2u << 20) &&
        minlz_backend_has_inline_asm_amd64()) {
        uint8_t *scratch = NULL;
        if (minlz_workspace_prepare_encode_scratch(ws, 2, 32768u, &scratch) != MINLZ_OK) {
            return 0;
        }
        n = minlz_encode_fast_block_2mb_goasm(
            dst, dst_cap, src, src_len, scratch);
        return n;
    }
#else
    (void)dst_cap;
#endif
    if (src_len <= 65536u) {
        return minlz_encode_payload_superfast64k_ws(ws, dst, dst_cap, src, src_len, 0);
    }
    if (MINLZ_ENABLE_PERIODIC_REPEAT_SUPERFAST && src_len > (2u << 20)) {
        n = minlz_encode_payload_periodic_repeat_ws(ws, dst, dst_cap, src, src_len,
                                                    MINLZ_LEVEL_SUPERFAST);
        if (n != 0) {
            return n;
        }
    }
    n = minlz_encode_payload_superfast_large_ws(ws, dst, dst_cap, src, src_len);
    if (n != 0 || src_len <= (2u << 20)) {
        return n;
    }
    return minlz_encode_fast_amd64_inline_ws(ws, dst, dst_cap, src, src_len);
}

#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
/* This chooses a search implementation, never a raw/compressed wire format.
 * Random binary input is cheaper to search once with C. Keep the C rescue
 * after unsuccessful asm attempts for inputs whose matches the asm can miss. */
static inline int minlz_l1_use_c_search(const uint8_t *src, size_t src_len) {
    /* Preserve wide binary repetitions even when the sampled bytes look random. */
    const __m128i first = _mm_loadu_si128((const __m128i *)(const void *)src);
    const __m128i half = _mm_loadu_si128((const __m128i *)(const void *)(src + src_len / 2u));
    if (_mm_movemask_epi8(_mm_cmpeq_epi8(first, half)) == 65535) {
        return 0;
    }
    const size_t offsets[4] = {0u, src_len / 3u, (src_len * 2u) / 3u, src_len - 16u};
    const __m128i zero = _mm_setzero_si128();
    __m128i high = zero;
    __m128i zeros = zero;
    __m128i printable = zero;
    size_t i;
    for (i = 0; i < 4u; ++i) {
        const __m128i bytes = _mm_loadu_si128((const __m128i *)(const void *)(src + offsets[i]));
        high = _mm_add_epi64(high, _mm_sad_epu8(_mm_cmpgt_epi8(zero, bytes), zero));
        zeros = _mm_add_epi64(zeros, _mm_sad_epu8(_mm_cmpeq_epi8(bytes, zero), zero));
        if (src_len <= (16u << 10)) {
            const __m128i ascii = _mm_and_si128(_mm_cmpgt_epi8(bytes, _mm_set1_epi8(31)),
                                               _mm_cmpgt_epi8(_mm_set1_epi8(127), bytes));
            printable = _mm_add_epi64(printable, _mm_sad_epu8(ascii, zero));
        }
    }
    if (src_len <= (16u << 10)) {
        const uint64_t text = (uint64_t)_mm_cvtsi128_si64(printable) +
            (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(printable, 8));
        const uint64_t nzero = (uint64_t)_mm_cvtsi128_si64(zeros) +
            (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(zeros, 8));
        return text < 48u * 255u && nzero < 16u * 255u;
    }
    return (uint64_t)_mm_cvtsi128_si64(high) +
               (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(high, 8)) >= 24u * 255u &&
           (uint64_t)_mm_cvtsi128_si64(zeros) +
               (uint64_t)_mm_cvtsi128_si64(_mm_srli_si128(zeros, 8)) <= 2u * 255u;
}
#endif

static size_t minlz_encode_fast_amd64_inline_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                                size_t dst_cap,
                                                const uint8_t * restrict src, size_t src_len) {
    size_t scratch = 0;
    minlz_encode_bucket bucket = minlz_fast_go_bucket(src_len, &scratch);
    uint8_t *scratch_buf = NULL;
    int incompressible_checked = 0;
#if MINLZ_ENABLE_L1_2MB_INLINE_ACTIVE
    int l1_2mb_goasm_candidate = 0;
#endif
    (void)bucket;
    if (bucket == MINLZ_BUCKET_NONE) {
        return 0;
    }
    if (minlz_fastest_l1_cheap_binary_probe(src, src_len)) {
        incompressible_checked = 1;
        if (minlz_fastest_l1_likely_incompressible(src, src_len)) {
            return 0;
        }
    }
    if (MINLZ_ENABLE_PERIODIC_REPEAT_L1 && src_len > (2u << 20)) {
        size_t n = minlz_encode_payload_periodic_repeat_ws(ws, dst, dst_cap, src, src_len,
                                                           MINLZ_LEVEL_FASTEST);
        if (n != 0) {
            return n;
        }
    }
#if MINLZ_ENABLE_L1_2MB_INLINE_ACTIVE
    l1_2mb_goasm_candidate = src_len > (512u << 10) && src_len <= (2u << 20) &&
        minlz_backend_has_inline_asm_amd64() &&
        minlz_fastest_l1_2mb_prefers_goasm(src, src_len);
    if (!l1_2mb_goasm_candidate && !incompressible_checked &&
        minlz_fastest_l1_likely_incompressible(src, src_len)) {
        return 0;
    }
#else
    if (!incompressible_checked && minlz_fastest_l1_likely_incompressible(src, src_len)) {
        return 0;
    }
#endif
    if (src_len > (4u << 10) && src_len <= (8u << 10)) {
        size_t n = minlz_encode_balanced_tiny_repeat4(dst, dst_cap, src, src_len);
        if (n != 0) {
            return n;
        }
    }
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    if (src_len > (4u << 10) && src_len <= (64u << 10) &&
        minlz_backend_has_inline_asm_amd64() && minlz_l1_use_c_search(src, src_len)) {
        if (src_len >= 32768u) {
            return minlz_encode_payload_fastest64k_large_asm_impl_ws(
                ws, dst, dst_cap, src, src_len, 12, 4);
        }
        return minlz_encode_payload_fastest64k_ws(ws, dst, dst_cap, src, src_len);
    }
#endif
#if MINLZ_ENABLE_L1_2MB_INLINE_ACTIVE
    if (l1_2mb_goasm_candidate && scratch < 262144u) {
        scratch = 262144u;
    }
#endif
#if MINLZ_ENABLE_L1_8M_GOASM
    if (src_len > (2u << 20) && scratch < 131072u) {
        scratch = 131072u;
    }
#endif
#if MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM || MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM || MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
    if (src_len > (4u << 10) && src_len <= (16u << 10) && scratch < 8192u) {
        scratch = 8192u;
    }
    if (src_len > (16u << 10) && src_len <= (64u << 10) && scratch < 16384u) {
        scratch = 16384u;
    }
#endif
#if !MINLZ_HAVE_INLINE_ASM_AMD64
    /* Portable encoders prepare their own table; asm scratch is unnecessary. */
    if (src_len <= 65536u) {
        return minlz_encode_payload_fastest64k_ws(ws, dst, dst_cap, src, src_len);
    }
    return minlz_encode_payload_fastest_ws(ws, dst, dst_cap, src, src_len);
#endif
    /*
     * This is the stable bucket switch point for the Go encodeBlockAsm*
     * port. The current body intentionally keeps the verified C encoder as the
     * safety fallback until each bucket proves size and speed in minlz_perf.
     */
    if (scratch != 0 &&
        minlz_workspace_prepare_encode_scratch(ws, 2, scratch, &scratch_buf) != MINLZ_OK) {
        return 0;
    }
    (void)scratch_buf;
#if MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM || MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM || MINLZ_ENABLE_L1_BLOCK2MB_INLINE_ASM
    if (minlz_backend_has_inline_asm_amd64()) {
#if MINLZ_ENABLE_L1_BLOCK16K_INLINE_ASM
        if (src_len > (4u << 10) && src_len <= (16u << 10)) {
            size_t n = minlz_encode_block_16k_inline_asm(dst, src, src_len, scratch_buf);
            if (n != 0) {
                return n;
            }
        }
#endif
#if MINLZ_ENABLE_L1_BLOCK64K_INLINE_ASM
        if (src_len > (16u << 10) && src_len <= (64u << 10)) {
            size_t n = minlz_encode_block_64k_inline_asm(dst, src, src_len, scratch_buf);
            if (n != 0) {
                return n;
            }
        }
#endif
    }
#endif
#if MINLZ_ENABLE_L1_8M_GOASM
    if (src_len > (2u << 20) && minlz_backend_has_inline_asm_amd64()) {
        size_t n = minlz_encode_l1_block_8m_goasm(dst, dst_cap, src, src_len, scratch_buf);
        if (n != 0) {
            return n;
        }
    }
#endif
#if MINLZ_ENABLE_L1_2MB_INLINE_ACTIVE
    if (l1_2mb_goasm_candidate) {
        size_t n = minlz_encode_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
        if (n != 0) {
            return n;
        }
    }
#endif
#if MINLZ_ENABLE_FAST2MB_GOASM
    if (src_len > (512u << 10) && src_len <= (2u << 20) &&
        minlz_backend_has_inline_asm_amd64() && minlz_better2mb_prefers_goasm(src, src_len)) {
        size_t n = minlz_encode_fast_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
        if (n != 0) {
            return n;
        }
    }
#endif
    if (src_len <= 65536u) {
        return minlz_encode_payload_fastest64k_ws(ws, dst, dst_cap, src, src_len);
    }
    return minlz_encode_payload_fastest_ws(ws, dst, dst_cap, src, src_len);
}

static size_t minlz_encode_payload_balanced_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                               size_t dst_cap,
                                               const uint8_t * restrict src, size_t src_len) {
    const unsigned l_table_bits = src_len > (2u << 20) ? 17u :
        (src_len > (512u << 10) ? MINLZ_BALANCED_2MB_L_BITS : 16u);
    const unsigned s_table_bits = src_len > (2u << 20) ? 14u :
        (src_len > (512u << 10) ? MINLZ_BALANCED_2MB_S_BITS : 13u);
    const size_t l_table_size = (size_t)1u << l_table_bits;
    const size_t s_table_size = (size_t)1u << s_table_bits;
    const int input_margin = 8;
    const int max_copy3_offset = (int)MINLZ_MAX_COPY3_OFFSET;
    int src_size;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    uint32_t *l_table;
    uint32_t *s_table;
    uint32_t *stamp;
    uint32_t generation;
    int use_asm_probe_512k = 0;
    int use_known_inline_asm_match = 0;

    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || src_len > (size_t)INT_MAX) {
        return 0;
    }
    if (MINLZ_ENABLE_PERIODIC_REPEAT_L2 && src_len > (2u << 20)) {
        size_t n = minlz_encode_payload_periodic_repeat_ws(ws, dst, dst_cap, src, src_len,
                                                           MINLZ_LEVEL_BALANCED);
        if (n != 0) {
            return n;
        }
    }
    (void)dst_cap;
    if (minlz_workspace_prepare_table(ws, 0, l_table_size, &l_table, &stamp,
                                      &generation) != MINLZ_OK) {
        return 0;
    }
    (void)stamp;
    (void)generation;
    if (minlz_workspace_prepare_table(ws, 1, s_table_size, &s_table, &stamp,
                                      &generation) != MINLZ_OK) {
        return 0;
    }
#if MINLZ_HAVE_INLINE_ASM_AMD64
    use_known_inline_asm_match = minlz_backend_has_inline_asm_amd64();
    use_asm_probe_512k = src_len > (512u << 10) && src_len <= (2u << 20) &&
        l_table_bits == 13u && s_table_bits == 15u && use_known_inline_asm_match;
#endif
    memset(l_table, 0, l_table_size * sizeof(l_table[0]));
    memset(s_table, 0, s_table_size * sizeof(s_table[0]));

    src_size = (int)src_len;
    s_limit = src_size - input_margin;
    dst_limit = src_size - (src_size >> 5) - 6;
    cv = minlz_load64(src + s);

    for (;;) {
        int candidate_l = 0;
        int next_s = 0;
        for (;;) {
            int min_src_pos;
            uint32_t hash_l;
            uint32_t hash_s;
            int candidate_s;
            uint64_t val_long;
            uint64_t val_short;

            {
                int skip_shift = use_asm_probe_512k ? MINLZ_BALANCED_2MB_SKIP_SHIFT : 7;
                int skip_cap = use_asm_probe_512k ? MINLZ_BALANCED_2MB_SKIP_CAP : 99;
                int skip = (s - next_emit) >> skip_shift;
                if (skip > skip_cap) {
                    skip = skip_cap;
                }
                next_s = s + skip + 1;
            }
            if (next_s > s_limit) {
                goto emit_remainder;
            }
            min_src_pos = s - max_copy3_offset + 1;
            if (MINLZ_LIKELY(use_asm_probe_512k)) {
                minlz_better_probe_512k_inline_asm_amd64(src, l_table, s_table,
                                                         cv, (uint32_t)s,
                                                         &candidate_l, &candidate_s,
                                                         &val_long, &val_short);
            } else {
                hash_l = minlz_hash7(cv, l_table_bits);
                hash_s = minlz_hash4((uint32_t)cv, s_table_bits);
                candidate_l = (int)l_table[hash_l];
                candidate_s = (int)s_table[hash_s];
                l_table[hash_l] = (uint32_t)s;
                s_table[hash_s] = (uint32_t)s;

                val_long = minlz_load64(src + candidate_l);
                val_short = minlz_load64(src + candidate_s);
            }
            if (candidate_l > min_src_pos && cv == val_long) {
                break;
            }

            if (repeat > 0 && s >= repeat) {
                const uint64_t repeat_mask = 0xffffffff00ull;
                if ((cv & repeat_mask) == (minlz_load64(src + s - repeat) & repeat_mask)) {
                    int base = s + 1;
                    int i = base - repeat;
                    size_t n;
                    while (base > next_emit && i > 0 && src[i - 1] == src[base - 1]) {
                        i--;
                        base--;
                    }
                    if (d + (base - next_emit) > dst_limit) {
                        d = 0;
                        goto done;
                    }
                    n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)(base - next_emit));
                    if (n == 0 && base != next_emit) {
                        d = 0;
                        goto done;
                    }
                    d += (int)n;

                    candidate_l = s - repeat + 5;
                    s += 5;
                    minlz_extend_match_int_better(src, &s, &candidate_l, src_size,
                                                  use_known_inline_asm_match);
                    n = minlz_emit_repeat_fast(dst + d, (size_t)(s - base));
                    if (n == 0) {
                        d = 0;
                        goto done;
                    }
                    d += (int)n;
                    next_emit = s;
                    if (s >= s_limit) {
                        goto emit_remainder;
                    }
                    {
                        int index0 = base + 1;
                        int index1 = s - 2;
                        while (index0 < index1) {
                            if (MINLZ_LIKELY(use_asm_probe_512k)) {
                                minlz_better_index_pair_512k_inline_asm_amd64(
                                    src, l_table, s_table, (uint32_t)index0,
                                    (uint32_t)index1);
                            } else {
                                uint64_t cv0 = minlz_load64(src + index0);
                                uint64_t cv1 = minlz_load64(src + index1);
                                l_table[minlz_hash7(cv0, l_table_bits)] = (uint32_t)index0;
                                s_table[minlz_hash4((uint32_t)(cv0 >> 8), s_table_bits)] =
                                    (uint32_t)(index0 + 1);
                                l_table[minlz_hash7(cv1, l_table_bits)] = (uint32_t)index1;
                                s_table[minlz_hash4((uint32_t)(cv1 >> 8), s_table_bits)] =
                                    (uint32_t)(index1 + 1);
                            }
                            index0 += 2;
                            index1 -= 2;
                        }
                    }
                    cv = minlz_load64(src + s);
                    continue;
                }
            }

            if (candidate_l >= min_src_pos && (uint32_t)cv == (uint32_t)val_long) {
                break;
            }
            if (candidate_s >= min_src_pos && (uint32_t)cv == (uint32_t)val_short) {
                uint32_t shifted_candidate_value;
                if (MINLZ_LIKELY(use_asm_probe_512k)) {
                    minlz_better_long_lookup_shift1_512k_inline_asm_amd64(
                        src, l_table, cv, (uint32_t)(s + 1), &candidate_l,
                        &shifted_candidate_value);
                } else {
                    hash_l = minlz_hash7(cv >> 8, l_table_bits);
                    candidate_l = (int)l_table[hash_l];
                    l_table[hash_l] = (uint32_t)(s + 1);
                    shifted_candidate_value = minlz_load32(src + candidate_l);
                }
                if (candidate_l > min_src_pos &&
                    (uint32_t)(cv >> 8) == shifted_candidate_value) {
                    s++;
                    break;
                }
                candidate_l = candidate_s;
                break;
            }

            cv = minlz_load64(src + next_s);
            s = next_s;
        }

        while (candidate_l > 0 && s > next_emit && src[candidate_l - 1] == src[s - 1]) {
            candidate_l--;
            s--;
        }
        if (d + (s - next_emit) > dst_limit) {
            d = 0;
            goto done;
        }

        {
            int base = s;
            int offset = base - candidate_l;
            int index0;
            int index1;
            size_t n;

            s += 4;
            candidate_l += 4;
            minlz_extend_match_int_better(src, &s, &candidate_l, src_size,
                                          use_known_inline_asm_match);

            if (offset > 65535 && s - base <= 4 && repeat != offset) {
                s = next_s + 1;
                if (s >= s_limit) {
                    goto emit_remainder;
                }
                cv = minlz_load64(src + s);
                continue;
            }

            if (next_emit != base) {
                size_t lit_len = (size_t)(base - next_emit);
                if (offset <= (int)MINLZ_MAX_COPY2_OFFSET) {
                    if (lit_len > MINLZ_MAX_COPY2_LITS || offset < (int)MINLZ_MIN_COPY2_OFFSET) {
                        n = minlz_emit_literal_fast(dst + d, src + next_emit, lit_len);
                        if (n == 0) {
                            d = 0;
                            goto done;
                        }
                        d += (int)n;
                        n = minlz_emit_copy_fast(dst + d, (size_t)offset, (size_t)(s - base));
                    } else {
                        n = minlz_emit_copy_lits2_overcopy4_fast(
                            dst + d, src + next_emit, lit_len, (size_t)offset,
                            (size_t)(s - base));
                    }
                } else {
                    if (lit_len > MINLZ_MAX_COPY3_LITS) {
                        n = minlz_emit_literal_fast(dst + d, src + next_emit, lit_len);
                        if (n == 0) {
                            d = 0;
                            goto done;
                        }
                        d += (int)n;
                        n = minlz_emit_copy_fast(dst + d, (size_t)offset, (size_t)(s - base));
                    } else {
                        n = minlz_emit_copy_lits3_overcopy4_fast(
                            dst + d, src + next_emit, lit_len, (size_t)offset,
                            (size_t)(s - base));
                    }
                }
            } else {
                n = minlz_emit_copy_fast(dst + d, (size_t)offset, (size_t)(s - base));
            }
            if (n == 0) {
                d = 0;
                goto done;
            }
            d += (int)n;
            repeat = offset;

            next_emit = s;
            if (s >= s_limit) {
                goto emit_remainder;
            }
            if (d > dst_limit) {
                d = 0;
                goto done;
            }

            index0 = base + 1;
            index1 = s - 2;
            if (MINLZ_LIKELY(use_asm_probe_512k)) {
                minlz_better_index_pair_512k_inline_asm_amd64(
                    src, l_table, s_table, (uint32_t)index0, (uint32_t)index1);
            } else {
                uint64_t cv0 = minlz_load64(src + index0);
                uint64_t cv1 = minlz_load64(src + index1);
                l_table[minlz_hash7(cv0, l_table_bits)] = (uint32_t)index0;
                s_table[minlz_hash4((uint32_t)(cv0 >> 8), s_table_bits)] =
                    (uint32_t)(index0 + 1);
                l_table[minlz_hash7(cv1, l_table_bits)] = (uint32_t)index1;
                s_table[minlz_hash4((uint32_t)(cv1 >> 8), s_table_bits)] =
                    (uint32_t)(index1 + 1);
            }
            index0++;
            index1--;
            cv = minlz_load64(src + s);

            {
                int index2 = (index0 + index1 + 1) >> 1;
                while (index2 < index1) {
                    if (MINLZ_LIKELY(use_asm_probe_512k)) {
                        minlz_better_index_long_pair_512k_inline_asm_amd64(
                            src, l_table, (uint32_t)index0, (uint32_t)index2);
                    } else {
                        l_table[minlz_hash7(minlz_load64(src + index0), l_table_bits)] =
                            (uint32_t)index0;
                        l_table[minlz_hash7(minlz_load64(src + index2), l_table_bits)] =
                            (uint32_t)index2;
                    }
                    index0 += 2;
                    index2 += 2;
                }
            }
        }
    }

emit_remainder:
    if (next_emit < src_size) {
        size_t n;
        if (d + (src_size - next_emit) > dst_limit) {
            d = 0;
            goto done;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)(src_size - next_emit));
        if (n == 0) {
            d = 0;
            goto done;
        }
        d += (int)n;
    }
    if (d >= src_size) {
        d = 0;
    }

done:
    return (size_t)d;
}

#if MINLZ_ENABLE_BALANCED_512K_BODY
static size_t minlz_encode_payload_balanced_512k_asm_ws(minlz_workspace *ws,
                                                        uint8_t * restrict dst,
                                                        size_t dst_cap,
                                                        const uint8_t * restrict src,
                                                        size_t src_len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    const size_t l_table_size = (size_t)1u << 13;
    const size_t s_table_size = (size_t)1u << 15;
    const int input_margin = 8;
    const int max_copy3_offset = (int)MINLZ_MAX_COPY3_OFFSET;
    int src_size;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    uint32_t *l_table;
    uint32_t *s_table;
    uint32_t *stamp;
    uint32_t generation;

    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE ||
        src_len <= (512u << 10) || src_len > (2u << 20) ||
        src_len > (size_t)INT_MAX) {
        return 0;
    }
    (void)dst_cap;
    if (minlz_workspace_prepare_table(ws, 0, l_table_size, &l_table, &stamp,
                                      &generation) != MINLZ_OK) {
        return 0;
    }
    (void)stamp;
    (void)generation;
    if (minlz_workspace_prepare_table(ws, 1, s_table_size, &s_table, &stamp,
                                      &generation) != MINLZ_OK) {
        return 0;
    }
    memset(l_table, 0, l_table_size * sizeof(l_table[0]));
    memset(s_table, 0, s_table_size * sizeof(s_table[0]));

    src_size = (int)src_len;
    s_limit = src_size - input_margin;
    dst_limit = src_size - (src_size >> 5) - 6;
    cv = minlz_load64(src + s);

    for (;;) {
        int candidate_l = 0;
        int next_s = 0;
        for (;;) {
            int min_src_pos;
            int candidate_s;
            uint64_t val_long;
            uint64_t val_short;

            {
                int skip = (s - next_emit) >> 7;
                if (skip > 99) {
                    skip = 99;
                }
                next_s = s + skip + 1;
            }
            if (next_s > s_limit) {
                goto emit_remainder;
            }
            min_src_pos = s - max_copy3_offset + 1;
            minlz_better_probe_512k_inline_asm_amd64(src, l_table, s_table,
                                                     cv, (uint32_t)s,
                                                     &candidate_l, &candidate_s,
                                                     &val_long, &val_short);
            if (candidate_l > min_src_pos && cv == val_long) {
                break;
            }

            if (repeat > 0 && s >= repeat) {
                const uint64_t repeat_mask = 0xffffffff00ull;
                if ((cv & repeat_mask) == (minlz_load64(src + s - repeat) & repeat_mask)) {
                    int base = s + 1;
                    int i = base - repeat;
                    size_t n;
                    while (base > next_emit && i > 0 && src[i - 1] == src[base - 1]) {
                        i--;
                        base--;
                    }
                    if (d + (base - next_emit) > dst_limit) {
                        d = 0;
                        goto done;
                    }
                    n = minlz_emit_literal_fast(dst + d, src + next_emit,
                                                (size_t)(base - next_emit));
                    if (n == 0 && base != next_emit) {
                        d = 0;
                        goto done;
                    }
                    d += (int)n;

                    candidate_l = s - repeat + 5;
                    s += 5;
                    minlz_extend_match_int_better(src, &s, &candidate_l, src_size, 1);
                    n = minlz_emit_repeat_fast(dst + d, (size_t)(s - base));
                    if (n == 0) {
                        d = 0;
                        goto done;
                    }
                    d += (int)n;
                    next_emit = s;
                    if (s >= s_limit) {
                        goto emit_remainder;
                    }
                    {
                        int index0 = base + 1;
                        int index1 = s - 2;
                        while (index0 < index1) {
                            minlz_better_index_pair_512k_inline_asm_amd64(
                                src, l_table, s_table, (uint32_t)index0,
                                (uint32_t)index1);
                            index0 += 2;
                            index1 -= 2;
                        }
                    }
                    cv = minlz_load64(src + s);
                    continue;
                }
            }

            if (candidate_l >= min_src_pos && (uint32_t)cv == (uint32_t)val_long) {
                break;
            }
            if (candidate_s >= min_src_pos && (uint32_t)cv == (uint32_t)val_short) {
                uint32_t shifted_candidate_value;
                minlz_better_long_lookup_shift1_512k_inline_asm_amd64(
                    src, l_table, cv, (uint32_t)(s + 1), &candidate_l,
                    &shifted_candidate_value);
                if (candidate_l > min_src_pos &&
                    (uint32_t)(cv >> 8) == shifted_candidate_value) {
                    s++;
                    break;
                }
                candidate_l = candidate_s;
                break;
            }

            cv = minlz_load64(src + next_s);
            s = next_s;
        }

        while (candidate_l > 0 && s > next_emit && src[candidate_l - 1] == src[s - 1]) {
            candidate_l--;
            s--;
        }
        if (d + (s - next_emit) > dst_limit) {
            d = 0;
            goto done;
        }

        {
            int base = s;
            int offset = base - candidate_l;
            int index0;
            int index1;
            size_t n;

            s += 4;
            candidate_l += 4;
            minlz_extend_match_int_better(src, &s, &candidate_l, src_size, 1);

            if (offset > 65535 && s - base <= 4 && repeat != offset) {
                s = next_s + 1;
                if (s >= s_limit) {
                    goto emit_remainder;
                }
                cv = minlz_load64(src + s);
                continue;
            }

            if (next_emit != base) {
                size_t lit_len = (size_t)(base - next_emit);
                if (offset <= (int)MINLZ_MAX_COPY2_OFFSET) {
                    if (lit_len > MINLZ_MAX_COPY2_LITS ||
                        offset < (int)MINLZ_MIN_COPY2_OFFSET) {
                        n = minlz_emit_literal_fast(dst + d, src + next_emit, lit_len);
                        if (n == 0) {
                            d = 0;
                            goto done;
                        }
                        d += (int)n;
                        n = minlz_emit_copy_fast(dst + d, (size_t)offset,
                                                 (size_t)(s - base));
                    } else {
                        n = minlz_emit_copy_lits2_overcopy4_fast(
                            dst + d, src + next_emit, lit_len, (size_t)offset,
                            (size_t)(s - base));
                    }
                } else {
                    if (lit_len > MINLZ_MAX_COPY3_LITS) {
                        n = minlz_emit_literal_fast(dst + d, src + next_emit, lit_len);
                        if (n == 0) {
                            d = 0;
                            goto done;
                        }
                        d += (int)n;
                        n = minlz_emit_copy_fast(dst + d, (size_t)offset,
                                                 (size_t)(s - base));
                    } else {
                        n = minlz_emit_copy_lits3_overcopy4_fast(
                            dst + d, src + next_emit, lit_len, (size_t)offset,
                            (size_t)(s - base));
                    }
                }
            } else {
                n = minlz_emit_copy_fast(dst + d, (size_t)offset, (size_t)(s - base));
            }
            if (n == 0) {
                d = 0;
                goto done;
            }
            d += (int)n;
            repeat = offset;

            next_emit = s;
            if (s >= s_limit) {
                goto emit_remainder;
            }
            if (d > dst_limit) {
                d = 0;
                goto done;
            }

            index0 = base + 1;
            index1 = s - 2;
            minlz_better_index_pair_512k_inline_asm_amd64(
                src, l_table, s_table, (uint32_t)index0, (uint32_t)index1);
            index0++;
            index1--;
            cv = minlz_load64(src + s);

            {
                int index2 = (index0 + index1 + 1) >> 1;
                while (index2 < index1) {
                    minlz_better_index_long_pair_512k_inline_asm_amd64(
                        src, l_table, (uint32_t)index0, (uint32_t)index2);
                    index0 += 2;
                    index2 += 2;
                }
            }
        }
    }

emit_remainder:
    if (next_emit < src_size) {
        size_t n;
        if (d + (src_size - next_emit) > dst_limit) {
            d = 0;
            goto done;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit,
                                    (size_t)(src_size - next_emit));
        if (n == 0) {
            d = 0;
            goto done;
        }
        d += (int)n;
    }
    if (d >= src_size) {
        d = 0;
    }

done:
    return (size_t)d;
#else
    (void)ws;
    (void)dst;
    (void)dst_cap;
    (void)src;
    (void)src_len;
    return 0;
#endif
}
#endif

/* Find starts of four matching bytes in the first five positions.
 * A borrow in the zero-byte test can set a later flag, but the first
 * flagged byte is always zero. */
static MINLZ_ALWAYS_INLINE uint64_t minlz_repeat4_starts(uint64_t difference) {
    difference |= difference >> 8;
    difference |= difference >> 16;
    return (difference - UINT64_C(0x0101010101010101)) & ~difference &
        UINT64_C(0x0000008080808080);
}

static MINLZ_ALWAYS_INLINE size_t minlz_emit_tiny_literal(uint8_t *dst, size_t dst_cap,
                                                        const uint8_t *src, size_t len) {
    /* Literals in this bucket use at most three tag bytes. */
    if (MINLZ_LIKELY(len + 3u <= dst_cap)) {
        return minlz_emit_literal_fast(dst, src, len);
    }
    return minlz_emit_literal(dst, dst_cap, src, len);
}

static size_t minlz_encode_balanced_tiny_repeat4(uint8_t * restrict dst, size_t dst_cap,
                                                 const uint8_t * restrict src,
                                                 size_t src_len) {
    uint16_t table[1024];
    size_t s = 1u;
    size_t next_emit = 0u;
    size_t d = 0u;
    size_t repeat = 1u;
    size_t matches = 0u;
    if (src_len <= 1024u || src_len > (8u << 10) || dst_cap < 32u) {
        return 0;
    }
    memset(table, 0, sizeof(table));
    /* An untouched entry also refers to position zero. A word can only
     * match there when it hashes to the same slot as the first word. */
    while (s + 4u <= src_len) {
        uint32_t cv = minlz_load32(src + s);
        uint32_t h = minlz_hash4(cv, 10);
        size_t candidate = table[h];
        size_t offset = s - candidate;
        table[h] = (uint16_t)s;
        if (offset <= MINLZ_MAX_COPY1_OFFSET && cv == minlz_load32(src + candidate)) {
            size_t base = s;
            size_t cand_end = candidate + 4u;
            size_t s_end = s + 4u;
            size_t length;
            size_t n;
            minlz_extend_match_size(src, &s_end, &cand_end, src_len);
            length = s_end - base;
            if (length > 18u) {
                length = 18u;
                s_end = base + length;
            }
            if (length < 4u) {
                s++;
                continue;
            }
            if (base > next_emit) {
                n = minlz_emit_tiny_literal(dst + d, dst_cap - d, src + next_emit, base - next_emit);
                if (n == 0) {
                    return 0;
                }
                d += n;
            }
            if (offset == repeat) {
                if (MINLZ_LIKELY(length < 30u)) {
                    if (MINLZ_UNLIKELY(d >= dst_cap)) {
                        return 0;
                    }
                    dst[d++] = (uint8_t)(((length - 1u) << 3) | MINLZ_TAG_REPEAT);
                } else {
                    n = minlz_emit_repeat(dst + d, dst_cap - d, length);
                    if (n == 0) {
                        return 0;
                    }
                    d += n;
                }
            } else {
                if (MINLZ_LIKELY(length < 19u)) {
                    uint16_t enc;
                    if (MINLZ_UNLIKELY(d + 2u > dst_cap)) {
                        return 0;
                    }
                    enc = (uint16_t)(((offset - 1u) << 6) |
                                     ((length - 4u) << 2) |
                                     MINLZ_TAG_COPY1);
                    minlz_store16(dst + d, enc);
                    d += 2u;
                } else {
                    n = minlz_emit_copy(dst + d, dst_cap - d, offset, length);
                    if (n == 0) {
                        return 0;
                    }
                    d += n;
                }
                repeat = offset;
            }
            matches++;
            s = s_end;
            next_emit = s;
            break;
        }
        s++;
    }
    /* After the first match only the selected offset is searched. */
    while (s + 4u <= src_len) {
        if (repeat == 28u && s + 32u <= src_len &&
            minlz_load32(src + s + 28u) == minlz_load32(src + s)) {
            s += 28u;
        }
        if (repeat <= s && minlz_load32(src + s) == minlz_load32(src + s - repeat)) {
            size_t base = s;
            size_t cand_end = s - repeat + 4u;
            size_t s_end = s + 4u;
            size_t length;
            size_t n;
            minlz_extend_match_size(src, &s_end, &cand_end, src_len);
            length = s_end - base;
            if (base > next_emit) {
                n = minlz_emit_tiny_literal(dst + d, dst_cap - d, src + next_emit, base - next_emit);
                if (n == 0) {
                    return 0;
                }
                d += n;
            }
            if (MINLZ_LIKELY(length < 30u)) {
                if (MINLZ_UNLIKELY(d >= dst_cap)) {
                    return 0;
                }
                dst[d++] = (uint8_t)(((length - 1u) << 3) | MINLZ_TAG_REPEAT);
            } else {
                n = minlz_emit_repeat(dst + d, dst_cap - d, length);
                if (n == 0) {
                    return 0;
                }
                d += n;
            }
            matches++;
            s = s_end;
            next_emit = s;
            continue;
        }
        s++;
        if (repeat != 28u && repeat <= s) {
            while (src_len - s >= 8u) {
                const uint64_t difference = minlz_load64(src + s) ^ minlz_load64(src + s - repeat);
                const uint64_t starts = minlz_repeat4_starts(difference);
                if (starts != 0) {
                    s += minlz_ctz64(starts) >> 3;
                    break;
                }
                s += 5u;
            }
        } else if (repeat == 28u && repeat <= s) {
            /* Keep the earlier of a repeat and the special lookahead. */
            while (src_len - s >= 36u) {
                const uint64_t current = minlz_load64(src + s);
                const uint64_t starts =
                    minlz_repeat4_starts(current ^ minlz_load64(src + s - 28u)) |
                    minlz_repeat4_starts(current ^ minlz_load64(src + s + 28u));
                if (starts != 0) {
                    s += minlz_ctz64(starts) >> 3;
                    break;
                }
                s += 5u;
            }
        }
    }
    if (matches < 4u) {
        return 0;
    }
    if (next_emit < src_len) {
        size_t n = minlz_emit_tiny_literal(dst + d, dst_cap - d, src + next_emit, src_len - next_emit);
        if (n == 0) {
            return 0;
        }
        d += n;
    }
    return d < src_len ? d : 0;
}

static size_t minlz_encode_better_amd64_inline_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                                  size_t dst_cap,
                                                  const uint8_t * restrict src, size_t src_len) {
    size_t scratch = 0;
    minlz_encode_bucket bucket = minlz_better_go_bucket(src_len, &scratch);
    uint8_t *scratch_buf = NULL;
    (void)bucket;
    if (bucket == MINLZ_BUCKET_NONE) {
        return 0;
    }
    {
        size_t n = minlz_encode_balanced_tiny_repeat4(dst, dst_cap, src, src_len);
        if (n != 0) {
            return n;
        }
    }
    /*
     * Bucket-level entry point for the Go encodeBetterBlockAsm* port. The C
     * body now mirrors the Go 512K/2MB table sizing before the full inline-asm
     * search/emit loop is enabled bucket by bucket.
     */
    if (scratch != 0 &&
        minlz_workspace_prepare_encode_scratch(ws, 2, scratch, &scratch_buf) != MINLZ_OK) {
        return 0;
    }
    (void)scratch_buf;
#if MINLZ_ENABLE_BETTER8M_GOASM
    if (src_len > (2u << 20) && minlz_backend_has_inline_asm_amd64() &&
        minlz_better2mb_prefers_goasm(src, src_len)) {
        size_t n = minlz_encode_better_block_8m_goasm(dst, dst_cap, src, src_len, scratch_buf);
        if (n != 0) {
            return n;
        }
    }
#endif
#if MINLZ_ENABLE_BETTER2MB_GOASM
    if (src_len > (512u << 10) && src_len <= (2u << 20) &&
        minlz_backend_has_inline_asm_amd64()) {
#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM && MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM && \
    MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED && !MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM
        size_t n = minlz_encode_better_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
        if (n == 0) {
            n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
        }
        if (n != 0) {
            return n;
        }
#else
        const minlz_better2mb_shape shape = minlz_better2mb_classify(src, src_len);
#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM
        if (MINLZ_ENABLE_BETTER2MB_GOASM_STRUCTURED && shape.structured) {
#if MINLZ_ENABLE_BETTER2MB_STRUCTURED_GLOBAL_ASM
            size_t n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
            if (n == 0) {
                n = minlz_encode_better_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
            }
#else
            size_t n = minlz_encode_better_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
            if (n == 0) {
                n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
            }
#endif
            if (n != 0) {
                return n;
            }
        }
#endif
        {
        const int prefer_goasm = shape.prefer_goasm;
        if (prefer_goasm) {
#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM
            size_t n;
            /*
             * The generated global 2MB loop is more stable on natural text
             * buckets, while the in-C inline candidate keeps the edge on dense
             * structured/log-like data and recursive repeat work. Both produce
             * Go-compatible output; choose by cheap shape classification.
             */
#if MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM
            (void)prefer_goasm;
            n = minlz_encode_better_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
            if (n == 0) {
                n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
            }
#else
            if (prefer_goasm) {
                n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
                if (n == 0) {
                    n = minlz_encode_better_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
                }
            } else {
                n = minlz_encode_better_block_2mb_inline_asm(dst, src, src_len, scratch_buf);
                if (n == 0) {
                    n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
                }
            }
#endif
#else
            size_t n = minlz_encode_better_block_2mb_goasm(dst, dst_cap, src, src_len, scratch_buf);
#endif
            if (n != 0) {
                return n;
            }
        }
        }
#endif
    }
#endif
#if MINLZ_ENABLE_BETTER_SMALL_GOASM
    if (minlz_backend_has_inline_asm_amd64()) {
        size_t n = 0;
        if (src_len > (64u << 10) && src_len <= (512u << 10)) {
            n = minlz_encode_better_block_512k_goasm(dst, dst_cap, src, src_len, scratch_buf);
        } else if (src_len > (16u << 10) && src_len <= (64u << 10)) {
            n = minlz_encode_better_block_64k_goasm(dst, dst_cap, src, src_len, scratch_buf);
        } else if (src_len > (4u << 10) && src_len <= (16u << 10)) {
#if MINLZ_ENABLE_L2_16K_GENERIC_FAST
            n = minlz_encode_balanced_16k_generic_fast(dst, dst_cap, src, src_len, scratch_buf);
            if (n != 0) {
                return n;
            }
#endif
#if MINLZ_ENABLE_BETTER16K_INLINE_ASM
            n = minlz_encode_better_block_16k_inline_asm(dst, src, src_len, scratch_buf, 0u);
#else
            n = minlz_encode_better_block_16k_goasm(dst, dst_cap, src, src_len, scratch_buf);
#endif
        } else if (src_len > (1u << 10) && src_len <= (4u << 10)) {
            n = minlz_encode_better_block_4k_goasm(dst, dst_cap, src, src_len, scratch_buf);
        } else if (src_len > MINLZ_MIN_NON_LITERAL_BLOCK_SIZE && src_len <= (1u << 10)) {
            n = minlz_encode_better_block_1k_goasm(dst, dst_cap, src, src_len, scratch_buf);
        }
        if (n != 0) {
            return n;
        }
    }
#endif
#if MINLZ_ENABLE_BALANCED_512K_BODY
    if (src_len > (512u << 10) && src_len <= (2u << 20) &&
        minlz_backend_has_inline_asm_amd64() &&
        !minlz_better2mb_prefers_goasm(src, src_len)) {
        size_t n = minlz_encode_payload_balanced_512k_asm_ws(ws, dst, dst_cap, src, src_len);
        if (n != 0) {
            return n;
        }
    }
#endif
    return minlz_encode_payload_balanced_ws(ws, dst, dst_cap, src, src_len);
}

typedef struct minlz_best_match {
    int offset;
    int s;
    int length;
    int score;
    int rep;
    int nextrep;
} minlz_best_match;

static MINLZ_ALWAYS_INLINE int minlz_best_emit_literal_size(int n) {
    if (n == 0) {
        return 0;
    }
    if (n <= 29) {
        return 1;
    }
    if (n < 29 + (1 << 8)) {
        return 2;
    }
    if (n < 29 + (1 << 16)) {
        return 3;
    }
    return 4;
}

static MINLZ_ALWAYS_INLINE int minlz_best_emit_repeat_size(int length) {
    if (length <= 0) {
        return 0;
    }
    if (length <= 29) {
        return 1;
    }
    length -= 29;
    if (length <= 256) {
        return 2;
    }
    if (length <= 65536) {
        return 3;
    }
    return 4;
}

static MINLZ_ALWAYS_INLINE int minlz_best_emit_copy2_size(int length) {
    length -= 4;
    if (length <= 60) {
        return 3;
    }
    length -= 60;
    if (length < 256) {
        return 4;
    }
    if (length < 65536) {
        return 5;
    }
    return 6;
}

static MINLZ_ALWAYS_INLINE int minlz_best_len_bytes(int v) {
    if (v <= 0xff) {
        return 1;
    }
    if (v <= 0xffff) {
        return 2;
    }
    return 3;
}

static MINLZ_ALWAYS_INLINE int minlz_best_emit_copy_size(int offset, int length) {
    if (offset > 65536 + 63) {
        length -= 64;
        if (length <= 0) {
            return 4;
        }
        return 4 + minlz_best_len_bytes(length);
    }
    if (offset <= (int)MINLZ_MAX_COPY1_OFFSET) {
        if (length <= 18) {
            return 2;
        }
        if (length < 18 + 256) {
            return 3;
        }
        return 2 + minlz_best_emit_repeat_size(length - 18);
    }
    return minlz_best_emit_copy2_size(length);
}

static MINLZ_ALWAYS_INLINE int minlz_best_score(const minlz_best_match *m, int next_emit) {
    int ll = m->s - next_emit;
    int offset = m->s - m->offset;
    int score = m->length - minlz_best_emit_literal_size(ll) - m->s;
    if (m->rep) {
        return score - minlz_best_emit_repeat_size(m->length);
    }
    if (ll > 0 && offset > (int)MINLZ_MAX_COPY1_OFFSET) {
        if (ll <= (int)MINLZ_MAX_COPY2_LITS &&
            offset < 65536 + 63 &&
            m->length <= (int)MINLZ_COPY2_LIT_MAX_LEN) {
            score++;
        } else if (ll <= (int)MINLZ_MAX_COPY3_LITS) {
            score++;
        }
    }
    return score - minlz_best_emit_copy_size(offset, m->length);
}

static MINLZ_ALWAYS_INLINE minlz_best_match minlz_best_of(minlz_best_match a,
                                                          minlz_best_match b) {
    if (b.length == 0) {
        return a;
    }
    if (a.length == 0) {
        return b;
    }
    if (a.score > b.score) {
        return a;
    }
    if (b.score > a.score) {
        return b;
    }
    if (a.s != b.s) {
        return a.s < b.s ? a : b;
    }
    if (a.nextrep != b.nextrep) {
        return a.nextrep ? a : b;
    }
    return a.offset > b.offset ? a : b;
}

static MINLZ_ALWAYS_INLINE minlz_best_match minlz_best_match_at(
    const uint8_t *src, int src_size, int s_limit, int next_emit,
    minlz_best_match current_best, int offset, int s, uint32_t first) {
    minlz_best_match m;
    int original_offset;
    int match_abs;
    int scan;
    m.offset = offset;
    m.s = s;
    m.length = 0;
    m.score = 0;
    m.rep = 0;
    m.nextrep = 0;
    if ((current_best.length != 0 && current_best.s - current_best.offset == s - offset) ||
        s - offset >= (int)MINLZ_MAX_COPY3_OFFSET || s <= offset || offset < 0) {
        return m;
    }
    if (minlz_load32(src + offset) != first) {
        return m;
    }
    original_offset = offset;
    match_abs = offset + 4;
    scan = s + 4;
    while (scan < src_size) {
        if (src_size - scan < 8) {
            if (src[scan] == src[match_abs]) {
                scan++;
                match_abs++;
                continue;
            }
            break;
        } else {
            uint64_t diff = minlz_load64(src + scan) ^ minlz_load64(src + match_abs);
            if (diff != 0) {
                match_abs += (int)(minlz_ctz64(diff) >> 3);
                break;
            }
            scan += 8;
            match_abs += 8;
        }
    }
    while (m.s > next_emit && m.offset > 0 && src[m.offset - 1] == src[m.s - 1]) {
        m.s--;
        m.offset--;
        match_abs++;
    }
    m.length = match_abs - original_offset;
    m.score = minlz_best_score(&m, next_emit);
    if (m.score <= -m.s) {
        m.length = 0;
    }
    if (m.s + m.length < s_limit) {
        int a = m.s + m.length + 1;
        int b = m.offset + m.length + 1;
        m.nextrep = minlz_load32(src + a) == minlz_load32(src + b);
    }
    return m;
}

static MINLZ_ALWAYS_INLINE minlz_best_match minlz_best_match_at_repeat(
    const uint8_t *src, int src_size, int s_limit, int next_emit,
    minlz_best_match current_best, int offset, int s, uint32_t first) {
    const uint32_t mask = (1u << 24) - 1u;
    minlz_best_match m;
    int original_offset;
    int match_abs;
    int scan;
    m.offset = offset;
    m.s = s;
    m.length = 0;
    m.score = 0;
    m.rep = 1;
    m.nextrep = 0;
    if (current_best.rep || offset < 0 || s >= src_size) {
        return m;
    }
    if ((minlz_load32(src + offset) & mask) != (first & mask)) {
        return m;
    }
    original_offset = offset;
    match_abs = offset + 3;
    scan = s + 3;
    while (scan < src_size) {
        if (src_size - scan < 8) {
            if (src[scan] == src[match_abs]) {
                scan++;
                match_abs++;
                continue;
            }
            break;
        } else {
            uint64_t diff = minlz_load64(src + scan) ^ minlz_load64(src + match_abs);
            if (diff != 0) {
                match_abs += (int)(minlz_ctz64(diff) >> 3);
                break;
            }
            scan += 8;
            match_abs += 8;
        }
    }
    while (m.s > next_emit && m.offset > 0 && src[m.offset - 1] == src[m.s - 1]) {
        m.s--;
        m.offset--;
        match_abs++;
    }
    m.length = match_abs - original_offset;
    if (m.s + m.length < s_limit) {
        int a = m.s + m.length + 1;
        int b = m.offset + m.length + 1;
        m.nextrep = minlz_load32(src + a) == minlz_load32(src + b);
    }
    m.score = minlz_best_score(&m, next_emit);
    return m;
}

static MINLZ_ALWAYS_INLINE size_t minlz_emit_best_copy(uint8_t *dst, const uint8_t *src,
                                                       int next_emit, minlz_best_match best,
                                                       int *s_out) {
    int base = best.s;
    int offset = base - best.offset;
    int lit_len = base - next_emit;
    if (best.rep) {
        size_t n = minlz_emit_literal_fast(dst, src + next_emit, (size_t)lit_len);
        if (best.length == 30) {
            best.length--;
            (*s_out)--;
        }
        return n + minlz_emit_repeat_fast(dst + n, (size_t)best.length);
    }
    if (lit_len > 0) {
        if (offset <= (int)MINLZ_MAX_COPY2_OFFSET) {
            if (lit_len > (int)MINLZ_MAX_COPY2_LITS || offset < (int)MINLZ_MIN_COPY2_OFFSET ||
                (offset <= (int)MINLZ_MAX_COPY1_OFFSET &&
                 best.length > (int)MINLZ_COPY2_LIT_MAX_LEN)) {
                size_t n = minlz_emit_literal_fast(dst, src + next_emit, (size_t)lit_len);
                if (best.length > 18 && best.length <= 64 &&
                    offset >= (int)MINLZ_MIN_COPY2_OFFSET) {
                    return n + minlz_emit_copy2_fast(dst + n, (size_t)offset,
                                                     (size_t)best.length);
                }
                if (best.length == 19) {
                    best.length--;
                    (*s_out)--;
                }
                return n + minlz_emit_copy_fast(dst + n, (size_t)offset,
                                                (size_t)best.length);
            }
            if (best.length > (int)MINLZ_COPY2_LIT_MAX_LEN) {
                *s_out = best.s + (int)MINLZ_COPY2_LIT_MAX_LEN;
                return minlz_emit_copy_lits2_fast(dst, src + next_emit, (size_t)lit_len,
                                                  (size_t)offset,
                                                  (size_t)MINLZ_COPY2_LIT_MAX_LEN);
            }
            return minlz_emit_copy_lits2_fast(dst, src + next_emit, (size_t)lit_len,
                                              (size_t)offset, (size_t)best.length);
        }
        if (best.length == 65) {
            best.length--;
            (*s_out)--;
        }
        if (lit_len > (int)MINLZ_MAX_COPY3_LITS) {
            size_t n = minlz_emit_literal_fast(dst, src + next_emit, (size_t)lit_len);
            return n + minlz_emit_copy_fast(dst + n, (size_t)offset, (size_t)best.length);
        }
        return minlz_emit_copy_lits3_fast(dst, src + next_emit, (size_t)lit_len,
                                          (size_t)offset, (size_t)best.length);
    }
    if (best.length > 18) {
        if (offset <= (int)MINLZ_MAX_COPY2_OFFSET) {
            if (best.length <= 64 && offset >= (int)MINLZ_MIN_COPY2_OFFSET) {
                return minlz_emit_copy2_fast(dst, (size_t)offset, (size_t)best.length);
            }
            if ((offset <= (int)MINLZ_MAX_COPY1_OFFSET && best.length == 19) ||
                (offset > (int)MINLZ_MAX_COPY1_OFFSET && best.length == 65)) {
                best.length--;
                (*s_out)--;
            }
        } else if (best.length == 65) {
            best.length--;
            (*s_out)--;
        }
    }
    return minlz_emit_copy_fast(dst, (size_t)offset, (size_t)best.length);
}

static size_t minlz_encode_payload_smallest_ws(minlz_workspace *ws, uint8_t * restrict dst,
                                               size_t dst_cap,
                                               const uint8_t * restrict src,
                                               size_t src_len) {
    const unsigned l_table_bits = 20;
    const unsigned s_table_bits = 18;
    const size_t l_table_size = (size_t)1u << 20;
    const size_t s_table_size = (size_t)1u << 18;
    const int max_skip = 64;
    uint8_t *scratch;
    uint64_t *l_table;
    uint64_t *s_table_ptr;
    uint32_t *l_pos_unused;
    uint32_t *s_pos_unused;
    uint32_t *l_stamp;
    uint32_t *s_stamp;
    uint32_t l_generation;
    uint32_t s_generation;
    int src_size;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    (void)dst_cap;

    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || src_len > (size_t)INT_MAX) {
        return 0;
    }
    if (src_len > (16u << 10) && !minlz_obviously_structured(src, src_len) &&
        minlz_likely_incompressible(src, src_len)) {
        return 0;
    }
    if (minlz_workspace_prepare_encode_scratch(
            ws, 2, (l_table_size + s_table_size) * sizeof(uint64_t), &scratch) != MINLZ_OK) {
        return 0;
    }
    if (minlz_workspace_prepare_table(ws, 0, l_table_size, &l_pos_unused, &l_stamp,
                                      &l_generation) != MINLZ_OK ||
        minlz_workspace_prepare_table(ws, 1, s_table_size, &s_pos_unused, &s_stamp,
                                      &s_generation) != MINLZ_OK) {
        return 0;
    }
    (void)l_pos_unused;
    (void)s_pos_unused;
    l_table = (uint64_t *)scratch;
    s_table_ptr = l_table + l_table_size;

    src_size = (int)src_len;
    s_limit = src_size - 10;
    dst_limit = src_size - 5;
    cv = minlz_load64(src + s);

    for (;;) {
        minlz_best_match best;
        best.offset = 0;
        best.s = 0;
        best.length = 0;
        best.score = 0;
        best.rep = 0;
        best.nextrep = 0;

        for (;;) {
            int next_s = ((s - next_emit) >> 8) + 1;
            uint32_t hash_l;
            uint32_t hash_s;
            uint64_t candidate_l;
            uint64_t candidate_s;

            if (next_s > max_skip) {
                next_s = s + max_skip;
            } else {
                next_s += s;
            }
            if (next_s > s_limit) {
                goto emit_remainder;
            }

            hash_l = minlz_hash8(cv, l_table_bits);
            hash_s = minlz_hash4((uint32_t)cv, s_table_bits);
            candidate_l = l_stamp[hash_l] == l_generation ? l_table[hash_l] : 0;
            candidate_s = s_stamp[hash_s] == s_generation ? s_table_ptr[hash_s] : 0;

            best = minlz_best_of(
                minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                    (int)(uint32_t)candidate_l, s, (uint32_t)cv),
                minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                    (int)(candidate_l >> 32), s, (uint32_t)cv));
            best = minlz_best_of(
                best,
                minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                    (int)(uint32_t)candidate_s, s, (uint32_t)cv));
            best = minlz_best_of(
                best,
                minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                    (int)(candidate_s >> 32), s, (uint32_t)cv));

            if (repeat <= s) {
                best = minlz_best_of(
                    best, minlz_best_match_at_repeat(src, src_size, s_limit, next_emit,
                                                     best, s - repeat, s, (uint32_t)cv));
                best = minlz_best_of(
                    best, minlz_best_match_at_repeat(src, src_size, s_limit, next_emit,
                                                     best, s - repeat + 1, s + 1,
                                                     (uint32_t)(cv >> 8)));
            }

            if (best.length > 0) {
                int s_fwd = s + 1;
                uint64_t fwd_cv = minlz_load64(src + s_fwd);
                uint32_t fwd_hash_s = minlz_hash4((uint32_t)(cv >> 8), s_table_bits);
                uint32_t fwd_hash_l = minlz_hash8(fwd_cv, l_table_bits);
                uint64_t next_short =
                    s_stamp[fwd_hash_s] == s_generation ? s_table_ptr[fwd_hash_s] : 0;
                uint64_t next_long =
                    l_stamp[fwd_hash_l] == l_generation ? l_table[fwd_hash_l] : 0;
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(uint32_t)next_short, s_fwd,
                                        (uint32_t)fwd_cv));
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(next_short >> 32), s_fwd,
                                        (uint32_t)fwd_cv));
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(uint32_t)next_long, s_fwd,
                                        (uint32_t)fwd_cv));
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(next_long >> 32), s_fwd,
                                        (uint32_t)fwd_cv));

                s_fwd++;
                fwd_cv = minlz_load64(src + s_fwd);
                if (repeat <= s_fwd) {
                    best = minlz_best_of(
                        best,
                        minlz_best_match_at_repeat(src, src_size, s_limit, next_emit,
                                                   best, s_fwd - repeat, s_fwd,
                                                   (uint32_t)fwd_cv));
                }
                fwd_hash_s = minlz_hash4((uint32_t)fwd_cv, s_table_bits);
                fwd_hash_l = minlz_hash8(fwd_cv, l_table_bits);
                next_short =
                    s_stamp[fwd_hash_s] == s_generation ? s_table_ptr[fwd_hash_s] : 0;
                next_long =
                    l_stamp[fwd_hash_l] == l_generation ? l_table[fwd_hash_l] : 0;
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(uint32_t)next_short, s_fwd,
                                        (uint32_t)fwd_cv));
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(next_short >> 32), s_fwd,
                                        (uint32_t)fwd_cv));
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(uint32_t)next_long, s_fwd,
                                        (uint32_t)fwd_cv));
                best = minlz_best_of(
                    best,
                    minlz_best_match_at(src, src_size, s_limit, next_emit, best,
                                        (int)(next_long >> 32), s_fwd,
                                        (uint32_t)fwd_cv));

                {
                    int s_at = best.s + best.length - 1;
                    if (s_at < s_limit) {
                        int s_back = best.s + 1;
                        int back_l = best.length - 2;
                        uint64_t back_cv = minlz_load64(src + s_back);
                        uint32_t at_hash_l = minlz_hash8(minlz_load64(src + s_at),
                                                         l_table_bits);
                        uint32_t at_hash_s = minlz_hash4((uint32_t)minlz_load64(src + s_at),
                                                         s_table_bits);
                        uint64_t next =
                            l_stamp[at_hash_l] == l_generation ? l_table[at_hash_l] : 0;
                        int check_at = (int)(uint32_t)next - back_l;
                        if (check_at > 0) {
                            best = minlz_best_of(
                                best,
                                minlz_best_match_at(src, src_size, s_limit, next_emit,
                                                    best, check_at, s_back,
                                                    (uint32_t)back_cv));
                        }
                        check_at = (int)(next >> 32) - back_l;
                        if (check_at > 0) {
                            best = minlz_best_of(
                                best,
                                minlz_best_match_at(src, src_size, s_limit, next_emit,
                                                    best, check_at, s_back,
                                                    (uint32_t)back_cv));
                        }
                        next = s_stamp[at_hash_s] == s_generation ? s_table_ptr[at_hash_s] : 0;
                        check_at = (int)(uint32_t)next - back_l;
                        if (check_at > 0) {
                            best = minlz_best_of(
                                best,
                                minlz_best_match_at(src, src_size, s_limit, next_emit,
                                                    best, check_at, s_back,
                                                    (uint32_t)back_cv));
                        }
                        check_at = (int)(next >> 32) - back_l;
                        if (check_at > 0) {
                            best = minlz_best_of(
                                best,
                                minlz_best_match_at(src, src_size, s_limit, next_emit,
                                                    best, check_at, s_back,
                                                    (uint32_t)back_cv));
                        }
                    }
                }
            }

            l_stamp[hash_l] = l_generation;
            s_stamp[hash_s] = s_generation;
            l_table[hash_l] = (uint64_t)(uint32_t)s | (candidate_l << 32);
            s_table_ptr[hash_s] = (uint64_t)(uint32_t)s | (candidate_s << 32);

            if (best.length > 0) {
                break;
            }
            cv = minlz_load64(src + next_s);
            s = next_s;
        }

        {
            int start_idx = s + 1;
            int base;
            int offset;
            size_t n;
            s = best.s;
            if (d + (s - next_emit) > dst_limit) {
                return 0;
            }
            base = s;
            offset = s - best.offset;
            s += best.length;
            if (!best.rep && best.length <= 4) {
                if (offset > 65535 ||
                    (offset > (int)MINLZ_MAX_COPY1_OFFSET &&
                     offset <= (int)MINLZ_MAX_COPY2_OFFSET &&
                     base - next_emit > (int)MINLZ_MAX_COPY2_LITS)) {
                    s = start_idx + 1;
                    if (s >= s_limit) {
                        goto emit_remainder;
                    }
                    cv = minlz_load64(src + s);
                    continue;
                }
            }
            n = minlz_emit_best_copy(dst + d, src, next_emit, best, &s);
            if (n == 0) {
                return 0;
            }
            d += (int)n;
            repeat = offset;
            next_emit = s;
            if (s >= s_limit) {
                goto emit_remainder;
            }
            if (d > dst_limit) {
                return 0;
            }
            while (start_idx < s) {
                uint64_t cv0 = minlz_load64(src + start_idx);
                uint32_t long0 = minlz_hash8(cv0, l_table_bits);
                uint32_t short0 = minlz_hash4((uint32_t)cv0, s_table_bits);
                uint64_t old_long = l_stamp[long0] == l_generation ? l_table[long0] : 0;
                uint64_t old_short =
                    s_stamp[short0] == s_generation ? s_table_ptr[short0] : 0;
                l_stamp[long0] = l_generation;
                s_stamp[short0] = s_generation;
                l_table[long0] = (uint64_t)(uint32_t)start_idx | (old_long << 32);
                s_table_ptr[short0] =
                    (uint64_t)(uint32_t)start_idx | (old_short << 32);
                start_idx++;
            }
            cv = minlz_load64(src + s);
        }
    }

emit_remainder:
    if (next_emit < src_size) {
        int lit_len = src_size - next_emit;
        size_t n;
        if (d + lit_len + minlz_best_emit_literal_size(lit_len) > dst_limit) {
            return 0;
        }
        n = minlz_emit_literal_fast(dst + d, src + next_emit, (size_t)lit_len);
        if (n == 0) {
            return 0;
        }
        d += (int)n;
    }
    if (d >= src_size) {
        return 0;
    }
    return (size_t)d;
}

size_t minlz_encode_payload_ws(minlz_workspace *ws, uint8_t * restrict dst, size_t dst_cap,
                               const uint8_t * restrict src, size_t src_len, int level) {
    unsigned table_bits;
    size_t table_size;
    uint32_t *table;
    uint32_t *stamp;
    uint32_t generation;
    size_t s_limit;
    size_t next_emit = 0;
    size_t s = 1;
    size_t d = 0;
    size_t repeat = 1;
    size_t step;

    if (!minlz_valid_level(level) || level == MINLZ_LEVEL_UNCOMPRESSED ||
        src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || src_len > MINLZ_MAX_BLOCK_SIZE) {
        return 0;
    }

    if (level == MINLZ_LEVEL_FASTEST) {
        return minlz_encode_fast_amd64_inline_ws(ws, dst, dst_cap, src, src_len);
    }
    if (level == MINLZ_LEVEL_SUPERFAST) {
        return minlz_encode_payload_superfast_ws(ws, dst, dst_cap, src, src_len);
    }
    if (level == MINLZ_LEVEL_BALANCED && src_len > MINLZ_MIN_NON_LITERAL_BLOCK_SIZE) {
        if (src_len > (16u << 10) && !minlz_obviously_structured(src, src_len) &&
            minlz_balanced_likely_incompressible(src, src_len)) {
            return 0;
        }
#if MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER
        if (src_len > (512u << 10) && src_len <= (2u << 20) &&
            minlz_obviously_structured(src, src_len)) {
            return minlz_encode_payload_balanced_ws(ws, dst, dst_cap, src, src_len);
        }
#endif
        return minlz_encode_better_amd64_inline_ws(ws, dst, dst_cap, src, src_len);
    }
    if (level == MINLZ_LEVEL_SMALLEST) {
        return minlz_encode_payload_smallest_ws(ws, dst, dst_cap, src, src_len);
    }
    if (level == MINLZ_LEVEL_BALANCED && src_len > (16u << 10) &&
        !minlz_obviously_structured(src, src_len) &&
        minlz_balanced_likely_incompressible(src, src_len)) {
        return 0;
    }

    switch (level) {
    case MINLZ_LEVEL_SUPERFAST:
        table_bits = 13;
        step = 3;
        break;
    case MINLZ_LEVEL_FASTEST:
        table_bits = 15;
        step = 2;
        break;
    case MINLZ_LEVEL_BALANCED:
        table_bits = src_len <= 1024u ? 11u : (src_len <= (64u << 10) ? 14u : 16u);
        step = 1;
        break;
    case MINLZ_LEVEL_SMALLEST:
    default:
        table_bits = 17;
        step = 1;
        break;
    }

    table_size = (size_t)1u << table_bits;
    if (minlz_workspace_prepare_table(ws, 0, table_size, &table, &stamp, &generation) != MINLZ_OK) {
        return 0;
    }
    (void)stamp;
    (void)generation;
    memset(table, 0, table_size * sizeof(table[0]));

    s_limit = src_len - 4;
    for (;;) {
        size_t candidate = 0;
        int found = 0;
        for (;;) {
            uint32_t cv;
            uint32_t h;
            size_t min_src_pos;
            if (s > s_limit) {
                goto emit_remainder;
            }
            cv = minlz_load32(src + s);
            h = minlz_hash4(cv, table_bits);
            candidate = table[h];
            table[h] = (uint32_t)s;
            min_src_pos = s > MINLZ_MAX_COPY3_OFFSET ? s - MINLZ_MAX_COPY3_OFFSET : 0;
            if (candidate >= min_src_pos && candidate < s &&
                cv == minlz_load32(src + candidate)) {
                found = 1;
                break;
            }
            s += step;
        }

        if (found) {
            size_t base = s;
            size_t offset = s - candidate;
            size_t length;

            candidate += 4;
            s += 4;
            minlz_extend_match_size(src, &s, &candidate, src_len);
            length = s - base;

            if (next_emit != base) {
                const uint8_t *lits = src + next_emit;
                size_t lit_len = base - next_emit;
                int can_fuse = offset >= MINLZ_MIN_COPY2_OFFSET &&
                    (lit_len <= 3 ||
                     (offset <= MINLZ_MAX_COPY2_OFFSET && lit_len <= 4));
                if (can_fuse) {
                    size_t n;
                    if (offset <= MINLZ_MAX_COPY2_OFFSET) {
                        n = minlz_emit_copy_lits2(dst + d, dst_cap - d, lits, lit_len, offset, length);
                    } else {
                        n = minlz_emit_copy_lits3(dst + d, dst_cap - d, lits, lit_len, offset, length);
                    }
                    if (n == 0) {
                        d = 0;
                        goto done;
                    }
                    d += n;
                    length = 0;
                } else {
                    size_t n = minlz_emit_literal(dst + d, dst_cap - d, lits, lit_len);
                    if (n == 0) {
                        d = 0;
                        goto done;
                    }
                    d += n;
                }
            }

            if (length > 0) {
                size_t n;
                if (offset == repeat) {
                    n = minlz_emit_repeat(dst + d, dst_cap - d, length);
                } else {
                    n = minlz_emit_copy(dst + d, dst_cap - d, offset, length);
                }
                if (n == 0) {
                    d = 0;
                    goto done;
                }
                d += n;
            }
            repeat = offset;
            next_emit = s;

            if (s > s_limit) {
                goto emit_remainder;
            }
            base++;
            while (base < s) {
                uint32_t h = minlz_hash4(minlz_load32(src + base), table_bits);
                table[h] = (uint32_t)base;
                base++;
            }
        }
    }

emit_remainder:
    if (next_emit < src_len) {
        size_t n = minlz_emit_literal(dst + d, dst_cap - d, src + next_emit, src_len - next_emit);
        if (n == 0) {
            d = 0;
            goto done;
        }
        d += n;
    }
    if (d >= src_len) {
        d = 0;
    }

done:
    return d;
}

size_t minlz_encode_payload(uint8_t *dst, size_t dst_cap, const uint8_t *src,
                            size_t src_len, int level) {
    minlz_workspace tmp;
    size_t n;
    memset(&tmp, 0, sizeof(tmp));
    n = minlz_encode_payload_ws(&tmp, dst, dst_cap, src, src_len, level);
    minlz_workspace_cleanup(&tmp);
    return n;
}

minlz_status minlz_encode_stream_payload_ws(minlz_workspace *ws, uint8_t *dst,
                                            size_t dst_cap, size_t *dst_len,
                                            const uint8_t *src, size_t src_len, int level) {
    size_t h;
    size_t n;
    if (!dst_len) {
        return MINLZ_CORRUPT;
    }
    *dst_len = 0;
    if (!minlz_valid_level(level)) {
        return MINLZ_INVALID_LEVEL;
    }
    if (src_len > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (level == MINLZ_LEVEL_UNCOMPRESSED || src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE) {
        return MINLZ_UNSUPPORTED;
    }
    if (dst_cap < 10) {
        return MINLZ_DST_TOO_SMALL;
    }
    h = minlz_put_uvarint(dst, (uint64_t)src_len);
    n = minlz_encode_payload_ws(ws, dst + h, dst_cap - h, src, src_len, level);
    if (n == 0 || h + n > src_len) {
        return MINLZ_UNSUPPORTED;
    }
    *dst_len = h + n;
    return MINLZ_OK;
}

minlz_status minlz_encode_stream_payload(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                                         const uint8_t *src, size_t src_len, int level) {
    minlz_workspace tmp;
    minlz_status st;
    memset(&tmp, 0, sizeof(tmp));
    st = minlz_encode_stream_payload_ws(&tmp, dst, dst_cap, dst_len, src, src_len, level);
    minlz_workspace_cleanup(&tmp);
    return st;
}

static minlz_status minlz_encode_short_dst(minlz_workspace *ws,
                                           uint8_t *dst, size_t dst_cap,
                                           size_t *dst_len, const uint8_t *src,
                                           size_t src_len, int level,
                                           int try_only) {
    size_t cap = minlz_max_encoded_len(src_len);
    size_t encoded_len = 0;
    uint8_t *scratch = (uint8_t *)malloc(cap);
    minlz_status st;
    if (!scratch) {
        return MINLZ_IO;
    }
    st = try_only
        ? minlz_try_encode_ws(ws, scratch, cap, &encoded_len,
                              src, src_len, level)
        : minlz_encode_ws(ws, scratch, cap, &encoded_len,
                          src, src_len, level);
    if (st == MINLZ_OK) {
        if (encoded_len > dst_cap) {
            *dst_len = encoded_len;
            st = MINLZ_DST_TOO_SMALL;
        } else {
            memcpy(dst, scratch, encoded_len);
            *dst_len = encoded_len;
        }
    }
    free(scratch);
    return st;
}

#if MINLZ_ENCODE_WS_SPLIT_FAST
static MINLZ_NOINLINE minlz_status minlz_encode_ws_impl(
    minlz_workspace *ws, uint8_t *dst, size_t dst_cap, size_t *dst_len,
    const uint8_t *src, size_t src_len, int level) {
#else
minlz_status minlz_encode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap, size_t *dst_len,
                             const uint8_t *src, size_t src_len, int level) {
#endif
    if (dst_len && dst && src && minlz_valid_level(level) &&
        level != MINLZ_LEVEL_UNCOMPRESSED &&
        src_len > MINLZ_MIN_NON_LITERAL_BLOCK_SIZE &&
        src_len <= MINLZ_MAX_BLOCK_SIZE &&
        MINLZ_UNLIKELY(dst_cap < src_len + 2u)) {
        *dst_len = 0;
        return minlz_encode_short_dst(ws, dst, dst_cap, dst_len,
                                      src, src_len, level, 0);
    }
#if MINLZ_ENCODE_10000_TOPLEVEL_FAST
    if (level == MINLZ_LEVEL_UNCOMPRESSED && src_len == 10000u) {
        if (!dst_len || !src || !dst) {
            return MINLZ_CORRUPT;
        }
        if (dst_cap < 10002u) {
            *dst_len = 10002u;
            return MINLZ_DST_TOO_SMALL;
        }
        minlz_store16(dst, 0);
#if MINLZ_ENCODE_8K16K_AVX2_GO_FWD
        if (minlz_backend_has_avx2() &&
            minlz_copy_uncompressed_avx2_go_fwd(dst + 2u, src, 10000u)) {
            *dst_len = 10002u;
            return MINLZ_OK;
        }
#endif
        memmove(dst + 2u, src, 10000u);
        *dst_len = 10002u;
        return MINLZ_OK;
    }
#endif
    if (level == MINLZ_LEVEL_UNCOMPRESSED && src_len >= (8u << 10) &&
        src_len <= (16u << 10)) {
        if (!dst_len || !src || !dst) {
            return MINLZ_CORRUPT;
        }
        return minlz_encode_uncompressed_8k16k(dst, dst_cap, dst_len, src, src_len);
    }
    if (!dst_len || (!src && src_len != 0) || !dst) {
        return MINLZ_CORRUPT;
    }
    *dst_len = 0;
    if (!minlz_valid_level(level)) {
        return MINLZ_INVALID_LEVEL;
    }
    size_t max_len;
    size_t h;
    size_t n;
    minlz_workspace tmp;
    int use_tmp = 0;

    if (level == MINLZ_LEVEL_UNCOMPRESSED || src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE) {
        if (src_len > MINLZ_MAX_BLOCK_SIZE) {
            return MINLZ_TOO_LARGE;
        }
#if MINLZ_ENCODE_TINY_RAW_DIRECT_MEMCPY
        if (src_len != 0 && src_len <= 64u) {
            if (dst_cap < src_len + 2u) {
                *dst_len = src_len + 2u;
                return MINLZ_DST_TOO_SMALL;
            }
            dst[0] = 0;
            dst[1] = 0;
            memcpy(dst + 2u, src, src_len);
            *dst_len = src_len + 2u;
            return MINLZ_OK;
        }
#endif
        if (src_len >= (8u << 10) && src_len <= (16u << 10)) {
            return minlz_encode_uncompressed_8k16k(dst, dst_cap, dst_len, src, src_len);
        }
        n = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
        if (n == 0) {
            *dst_len = minlz_uncompressed_encoded_len(src_len);
            return MINLZ_DST_TOO_SMALL;
        }
        *dst_len = n;
        return MINLZ_OK;
    }
    max_len = minlz_max_encoded_len(src_len);
    if (max_len == 0) {
        return MINLZ_TOO_LARGE;
    }
    if (level == MINLZ_LEVEL_BALANCED &&
        minlz_balanced_tiny_lacks_long_match(src, src_len)) {
        n = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
        if (n == 0) {
            *dst_len = max_len;
            return MINLZ_DST_TOO_SMALL;
        }
        *dst_len = n;
        return MINLZ_OK;
    }
    if (level == MINLZ_LEVEL_FASTEST &&
        minlz_fastest_l1_cheap_binary_probe(src, src_len) &&
        minlz_fastest_l1_likely_incompressible(src, src_len)) {
        n = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
        if (n == 0) {
            *dst_len = max_len;
            return MINLZ_DST_TOO_SMALL;
        }
        *dst_len = n;
        return MINLZ_OK;
    }
    if (!ws) {
        memset(&tmp, 0, sizeof(tmp));
        ws = &tmp;
        use_tmp = 1;
    }
    {
        uint8_t hdr[10];
        h = minlz_put_uvarint(hdr, (uint64_t)src_len);
        if (dst_cap > 1 + h) {
            dst[0] = 0;
            memcpy(dst + 1, hdr, h);
            n = minlz_encode_payload_ws(ws, dst + 1 + h, dst_cap - 1 - h, src, src_len, level);
            if (n > 0 && 1 + h + n <= max_len) {
                *dst_len = 1 + h + n;
                if (use_tmp) {
                    minlz_workspace_cleanup(&tmp);
                }
                return MINLZ_OK;
            }
        }
    }
    n = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
    if (n == 0) {
        *dst_len = max_len;
        if (use_tmp) {
            minlz_workspace_cleanup(&tmp);
        }
        return MINLZ_DST_TOO_SMALL;
    }
    *dst_len = n;
    if (use_tmp) {
        minlz_workspace_cleanup(&tmp);
    }
    return MINLZ_OK;
}

#if MINLZ_ENCODE_WS_SPLIT_FAST
static MINLZ_NOINLINE minlz_status minlz_encode_ws_nonraw(
    minlz_workspace *ws, uint8_t *dst, size_t dst_cap, size_t *dst_len,
    const uint8_t *src, size_t src_len, int level) {
    if (level == MINLZ_LEVEL_UNCOMPRESSED && src_len <= 2048u) {
        if (!dst_len || (!src && src_len != 0u) || !dst) {
            return MINLZ_CORRUPT;
        }
        if (src_len == 0u) {
            if (dst_cap < 1u) {
                *dst_len = 1u;
                return MINLZ_DST_TOO_SMALL;
            }
            dst[0] = 0;
            *dst_len = 1u;
            return MINLZ_OK;
        }
        if (dst_cap < src_len + 2u) {
            *dst_len = src_len + 2u;
            return MINLZ_DST_TOO_SMALL;
        }
        minlz_store16(dst, 0);
        minlz_copy_uncompressed_encode(dst + 2u, src, src_len);
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
    if (level == MINLZ_LEVEL_UNCOMPRESSED && src_len == 10000u) {
        if (!dst_len || !src || !dst) {
            return MINLZ_CORRUPT;
        }
        if (dst_cap < 10002u) {
            *dst_len = 10002u;
            return MINLZ_DST_TOO_SMALL;
        }
        minlz_store16(dst, 0);
        *dst_len = 10002u;
        if (minlz_backend_has_avx2() &&
            minlz_copy_uncompressed_avx2_go_fwd_unchecked(dst + 2u, src, 10000u)) {
            return MINLZ_OK;
        }
        memmove(dst + 2u, src, 10000u);
        return MINLZ_OK;
    }
#if MINLZ_ENCODE_WS_SPLIT_RAW_ALL
    if (level == MINLZ_LEVEL_UNCOMPRESSED) {
        if (!dst_len || (!src && src_len != 0u) || !dst) {
            return MINLZ_CORRUPT;
        }
        if (src_len > MINLZ_MAX_BLOCK_SIZE) {
            return MINLZ_TOO_LARGE;
        }
        if (dst_cap < src_len + 2u) {
            *dst_len = src_len + 2u;
            return MINLZ_DST_TOO_SMALL;
        }
        minlz_store16(dst, 0);
        if (src_len != 0u) {
            const uintptr_t d = (uintptr_t)(dst + 2u);
            const uintptr_t s = (uintptr_t)src;
            if (d + src_len <= s || s + src_len <= d) {
                memcpy(dst + 2u, src, src_len);
            } else {
                memmove(dst + 2u, src, src_len);
            }
        }
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
#endif
#if MINLZ_ENCODE_WS_SPLIT_BALANCED2MB
    if (level == MINLZ_LEVEL_BALANCED && src_len > (16u << 10) &&
        src_len <= MINLZ_MAX_BLOCK_SIZE && src && dst && dst_len) {
        const size_t max_len = minlz_max_encoded_len(src_len);
        if (max_len != 0u && dst_cap >= max_len &&
            !minlz_obviously_structured(src, src_len) &&
            minlz_balanced_likely_incompressible(src, src_len)) {
            const size_t n = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
            if (n != 0u) {
                *dst_len = n;
                return MINLZ_OK;
            }
        }
    }
#endif
#if MINLZ_ENCODE_WS_SPLIT_BALANCED16K && MINLZ_ENABLE_BETTER_SMALL_GOASM
    if (ws && level == MINLZ_LEVEL_BALANCED && src_len > (4u << 10) &&
        src_len <= (16u << 10) && src && dst && dst_len &&
        minlz_backend_has_inline_asm_amd64()) {
        const size_t max_len = minlz_max_encoded_len(src_len);
        if (max_len != 0 && dst_cap >= max_len) {
            uint8_t *scratch = NULL;
            if (minlz_workspace_prepare_encode_scratch(ws, 2, 36864u, &scratch) == MINLZ_OK) {
                const size_t h = minlz_put_uvarint(dst + 1u, (uint64_t)src_len);
                size_t n;
                dst[0] = 0;
#if MINLZ_ENABLE_BETTER16K_INLINE_ASM
                n = minlz_encode_better_block_16k_inline_asm(
                    dst + 1u + h, src, src_len, scratch, max_len - 1u);
#else
                n = minlz_encode_better_block_16k_goasm(
                    dst + 1u + h, dst_cap - 1u - h, src, src_len, scratch);
#endif
                if (n != 0 && 1u + h + n <= max_len) {
                    *dst_len = 1u + h + n;
                    return MINLZ_OK;
                }
            }
        }
    }
#endif
#if MINLZ_ENCODE_WS_SPLIT_BALANCED2MB && MINLZ_ENABLE_BETTER2MB_GOASM
    if (ws && level == MINLZ_LEVEL_BALANCED && src_len > (512u << 10) &&
        src_len <= (2u << 20) && src && dst && dst_len &&
        minlz_backend_has_inline_asm_amd64()) {
        const size_t max_len = minlz_max_encoded_len(src_len);
        if (max_len != 0 && dst_cap >= max_len) {
            const size_t h = minlz_put_uvarint(dst + 1u, (uint64_t)src_len);
            size_t n;
            dst[0] = 0;
#if MINLZ_ENABLE_BETTER2MB_STRUCTURED_SMALLER
            if (minlz_obviously_structured(src, src_len)) {
                n = minlz_encode_payload_balanced_ws(
                    ws, dst + 1u + h, dst_cap - 1u - h, src, src_len);
                if (n != 0 && 1u + h + n <= max_len) {
                    *dst_len = 1u + h + n;
                    return MINLZ_OK;
                }
            }
#endif
            uint8_t *scratch = NULL;
            if (minlz_workspace_prepare_encode_scratch(ws, 2, 589824u, &scratch) == MINLZ_OK) {
#if MINLZ_ENABLE_BETTER2MB_INLINE_ASM && MINLZ_ENABLE_BETTER2MB_FORCE_INLINE_ASM
                n = minlz_encode_better_block_2mb_inline_asm(
                    dst + 1u + h, src, src_len, scratch);
                if (n == 0u) {
                    n = minlz_encode_better_block_2mb_goasm(
                        dst + 1u + h, dst_cap - 1u - h, src, src_len, scratch);
                }
#else
                n = minlz_encode_better_block_2mb_goasm(
                    dst + 1u + h, dst_cap - 1u - h, src, src_len, scratch);
#endif
                if (n != 0 && 1u + h + n <= max_len) {
                    *dst_len = 1u + h + n;
                    return MINLZ_OK;
                }
            }
        }
    }
#endif
    return minlz_encode_ws_impl(ws, dst, dst_cap, dst_len, src, src_len, level);
}
#endif

#if MINLZ_ENCODE_WS_SPLIT_FAST
minlz_status minlz_encode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                             size_t *dst_len, const uint8_t *src, size_t src_len,
                             int level) {
    /* L1 returns raw blocks for inputs up to 32 bytes. Blocks up to 4 KiB
     * with a workspace can enter the encoder without generic dispatch. */
    if (level == MINLZ_LEVEL_FASTEST && src_len <= 4096u &&
        dst && dst_len && (src || src_len == 0u)) {
        if (src_len <= 32u) {
            const size_t n = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
            *dst_len = n != 0u ? n : minlz_uncompressed_encoded_len(src_len);
            return n != 0u ? MINLZ_OK : MINLZ_DST_TOO_SMALL;
        }
        if (ws && dst_cap >= src_len + 2u) {
            size_t h;
            size_t n;
            *dst_len = 0;
            dst[0] = 0;
            /* The validated length fits in a one- or two-byte uvarint. */
            if (src_len < 128u) {
                dst[1] = (uint8_t)src_len;
                h = 1u;
            } else {
                dst[1] = (uint8_t)(src_len | 0x80u);
                dst[2] = (uint8_t)(src_len >> 7);
                h = 2u;
            }
#if !(MINLZ_ENABLE_FASTEST64K_INLINE_PROBE && MINLZ_HAVE_INLINE_ASM_AMD64)
            if (src_len <= 256u) {
                n = minlz_encode_payload_fastest256_impl_ws(ws, dst + 1u + h,
                    dst_cap - 1u - h, src, src_len, 9, 3);
            } else
#endif
            {
                n = minlz_encode_payload_fastest64k_ws(ws, dst + 1u + h,
                                                      dst_cap - 1u - h, src, src_len);
            }
            if (n != 0u && 1u + h + n <= src_len + 2u) {
                *dst_len = 1u + h + n;
                return MINLZ_OK;
            }
            *dst_len = minlz_encode_uncompressed(dst, dst_cap, src, src_len);
            return MINLZ_OK;
        }
    }
    if (dst_len && dst && src && minlz_valid_level(level) &&
        level != MINLZ_LEVEL_UNCOMPRESSED &&
        src_len > MINLZ_MIN_NON_LITERAL_BLOCK_SIZE &&
        src_len <= MINLZ_MAX_BLOCK_SIZE &&
        MINLZ_UNLIKELY(dst_cap < src_len + 2u)) {
        *dst_len = 0;
        return minlz_encode_short_dst(ws, dst, dst_cap, dst_len,
                                      src, src_len, level, 0);
    }
    if (level != MINLZ_LEVEL_UNCOMPRESSED) {
        return minlz_encode_ws_nonraw(ws, dst, dst_cap, dst_len, src, src_len, level);
    }
    if (!dst_len || (!src && src_len != 0u) || !dst) {
        return MINLZ_CORRUPT;
    }
    if (src_len > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (src_len == 0u) {
        if (dst_cap < 1u) {
            *dst_len = 1u;
            return MINLZ_DST_TOO_SMALL;
        }
        dst[0] = 0;
        *dst_len = 1u;
        return MINLZ_OK;
    }
    if (dst_cap < src_len + 2u) {
        *dst_len = src_len + 2u;
        return MINLZ_DST_TOO_SMALL;
    }
    minlz_store16(dst, 0);
    if (src_len == 10000u && minlz_backend_has_avx2() &&
        minlz_copy_uncompressed_avx2_go_fwd_unchecked(dst + 2u, src, src_len)) {
        *dst_len = src_len + 2u;
        return MINLZ_OK;
    }
    {
        const uintptr_t d = (uintptr_t)(dst + 2u);
        const uintptr_t s = (uintptr_t)src;
        const int nonoverlap = d <= s ? s - d >= src_len : d - s >= src_len;
        if (nonoverlap) {
            memcpy(dst + 2u, src, src_len);
        } else {
            memmove(dst + 2u, src, src_len);
        }
    }
    *dst_len = src_len + 2u;
    return MINLZ_OK;
}
#endif

minlz_status minlz_encode(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                          const uint8_t *src, size_t src_len, int level) {
    return minlz_encode_ws(NULL, dst, dst_cap, dst_len, src, src_len, level);
}

minlz_status minlz_try_encode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                                 size_t *dst_len, const uint8_t *src, size_t src_len,
                                 int level) {
    size_t h;
    size_t n;
    minlz_workspace tmp;
    int use_tmp = 0;
    if (!dst_len || (!src && src_len != 0) || !dst) {
        return MINLZ_CORRUPT;
    }
    *dst_len = 0;
    if (!minlz_valid_level(level) || level == MINLZ_LEVEL_UNCOMPRESSED) {
        return !minlz_valid_level(level) ? MINLZ_INVALID_LEVEL : MINLZ_UNSUPPORTED;
    }
    if (src_len > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || dst_cap < 2) {
        return MINLZ_UNSUPPORTED;
    }
    if (MINLZ_UNLIKELY(dst_cap < src_len + 2u)) {
        return minlz_encode_short_dst(ws, dst, dst_cap, dst_len,
                                      src, src_len, level, 1);
    }
    if (!ws) {
        memset(&tmp, 0, sizeof(tmp));
        ws = &tmp;
        use_tmp = 1;
    }
    {
        uint8_t hdr[10];
        h = minlz_put_uvarint(hdr, (uint64_t)src_len);
        if (dst_cap <= 1 + h) {
            if (use_tmp) {
                minlz_workspace_cleanup(&tmp);
            }
            return MINLZ_DST_TOO_SMALL;
        }
        dst[0] = 0;
        memcpy(dst + 1, hdr, h);
    }
    n = minlz_encode_payload_ws(ws, dst + 1 + h, dst_cap - 1 - h, src, src_len, level);
    if (n == 0 || 1 + h + n >= src_len) {
        if (use_tmp) {
            minlz_workspace_cleanup(&tmp);
        }
        return MINLZ_UNSUPPORTED;
    }
    *dst_len = 1 + h + n;
    if (use_tmp) {
        minlz_workspace_cleanup(&tmp);
    }
    return MINLZ_OK;
}

minlz_status minlz_try_encode(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                              const uint8_t *src, size_t src_len, int level) {
    return minlz_try_encode_ws(NULL, dst, dst_cap, dst_len, src, src_len, level);
}
