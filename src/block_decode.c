/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#ifndef MINLZ_ENABLE_DECODE_GOASM
#define MINLZ_ENABLE_DECODE_GOASM 1
#endif

#ifndef MINLZ_DECODE_REPMOVSB_OVERLAP_MIN
#define MINLZ_DECODE_REPMOVSB_OVERLAP_MIN 128u
#endif

#ifndef MINLZ_DECODE_UNCOMPRESSED_REPMOVSB_MIN
#define MINLZ_DECODE_UNCOMPRESSED_REPMOVSB_MIN (1u << 30)
#endif

#ifndef MINLZ_DECODE_LARGE_RAW_SSE_NT_MIN
#define MINLZ_DECODE_LARGE_RAW_SSE_NT_MIN (1u << 20)
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_XMM
#define MINLZ_ENABLE_RAWCOPY_SSE42_XMM 0
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_MOVSQ
#define MINLZ_ENABLE_RAWCOPY_SSE42_MOVSQ 0
#endif

#ifndef MINLZ_DECODE_8K16K_PLAIN_MEMCPY
#define MINLZ_DECODE_8K16K_PLAIN_MEMCPY 1
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_QWORD
#define MINLZ_ENABLE_RAWCOPY_SSE42_QWORD 0
#endif

#ifndef MINLZ_ENABLE_RAWCOPY_SSE42_XMM_ALIGNED_STORE
#define MINLZ_ENABLE_RAWCOPY_SSE42_XMM_ALIGNED_STORE 1
#endif

#ifndef MINLZ_DECODE_8K16K_AVX2_ALIGNED_STORE
#define MINLZ_DECODE_8K16K_AVX2_ALIGNED_STORE 0
#endif

#ifndef MINLZ_DECODE_8K16K_AVX2_GO_FWD
#define MINLZ_DECODE_8K16K_AVX2_GO_FWD 1
#endif

#ifndef MINLZ_DECODE_8K16K_DIRECT_MEMMOVE
#define MINLZ_DECODE_8K16K_DIRECT_MEMMOVE 1
#endif

#ifndef MINLZ_DECODE_LARGE_AVX2_GO_FWD
#define MINLZ_DECODE_LARGE_AVX2_GO_FWD 0
#endif

#ifndef MINLZ_DECODE_1K2K_MEMCPY
#define MINLZ_DECODE_1K2K_MEMCPY 1
#endif

#ifndef MINLZ_DECODE_1K2K_AVX2_GO_FWD
#define MINLZ_DECODE_1K2K_AVX2_GO_FWD 1
#endif

#ifndef MINLZ_DECODE_GOASM_MAX_BLOCK
#define MINLZ_DECODE_GOASM_MAX_BLOCK (8u << 20)
#endif

#ifndef MINLZ_DECODE_TINY_UNCOMPRESSED_MAX
#define MINLZ_DECODE_TINY_UNCOMPRESSED_MAX 2050u
#endif

#ifndef MINLZ_DECODE_TINY_RAW_DIRECT_MEMCPY
#define MINLZ_DECODE_TINY_RAW_DIRECT_MEMCPY 1
#endif

#ifndef MINLZ_DECODE_WS_SPLIT_RAW_FAST
#define MINLZ_DECODE_WS_SPLIT_RAW_FAST 1
#endif

#ifndef MINLZ_DECODE_RAW_MID_MEMCPY_NONOVERLAP
#define MINLZ_DECODE_RAW_MID_MEMCPY_NONOVERLAP 1
#endif

#ifndef MINLZ_DECODE_FAST_LOOP_MARGIN
#define MINLZ_DECODE_FAST_LOOP_MARGIN 64u
#endif

#ifndef MINLZ_DECODE_COPY16_GPR
#define MINLZ_DECODE_COPY16_GPR 0
#endif

#ifndef MINLZ_DECODE_MATCH_HOT_MAX
#define MINLZ_DECODE_MATCH_HOT_MAX 64u
#endif

#ifndef MINLZ_DECODE_COPY3_SHORT_INLINE
#define MINLZ_DECODE_COPY3_SHORT_INLINE 1
#endif

#ifndef MINLZ_DECODE_COPY3_MEDIUM_INLINE
#define MINLZ_DECODE_COPY3_MEDIUM_INLINE 1
#endif

#ifndef MINLZ_DECODE_MATCH_EXACT_65_128
#define MINLZ_DECODE_MATCH_EXACT_65_128 0
#endif

#ifndef MINLZ_DECODE_MATCH_EXACT_65_80
#define MINLZ_DECODE_MATCH_EXACT_65_80 0
#endif

#ifndef MINLZ_DECODE_SKIP_MIDRATIO_GOASM
#define MINLZ_DECODE_SKIP_MIDRATIO_GOASM 1
#endif

#ifndef MINLZ_DECODE_MIDRATIO_C_PERMILLE
#define MINLZ_DECODE_MIDRATIO_C_PERMILLE 181u
#endif

#ifndef MINLZ_DECODE_MIDRATIO_SHAPE_SCAN
#define MINLZ_DECODE_MIDRATIO_SHAPE_SCAN 0
#endif

int minlz_decode_block_goasm(uint8_t *dst, size_t dst_len,
                             const uint8_t *src, size_t src_len);

#if MINLZ_DECODE_SKIP_MIDRATIO_GOASM && MINLZ_DECODE_MIDRATIO_SHAPE_SCAN
static int minlz_decode_midratio_prefers_c_parser(const uint8_t *src, size_t src_len) {
    size_t i = 0;
    size_t commands = 0;
    size_t fused = 0;
    size_t copy2 = 0;
    size_t repeats = 0;
    while (i < src_len && commands < 256u) {
        uint8_t tag_byte = src[i];
        uint8_t tag = (uint8_t)(tag_byte & 3u);
        commands++;
        if (tag == MINLZ_TAG_LITERAL) {
            uint8_t x = (uint8_t)(tag_byte >> 3);
            size_t length;
            i++;
            if (x < 29u) {
                length = (size_t)x + 1u;
            } else if (x == 29u) {
                if (i >= src_len) {
                    return 0;
                }
                length = 30u + src[i++];
            } else if (x == 30u) {
                if (src_len - i < 2u) {
                    return 0;
                }
                length = 30u + minlz_load16(src + i);
                i += 2u;
            } else {
                if (src_len - i < 3u) {
                    return 0;
                }
                length = 30u + (minlz_load32(src + i - 1u) >> 8);
                i += 3u;
            }
            if ((tag_byte & MINLZ_TAG_REPEAT) == 0) {
                if (length > src_len - i) {
                    return 0;
                }
                i += length;
            } else {
                repeats++;
            }
        } else if (tag == MINLZ_TAG_COPY1) {
            uint8_t len_code;
            if (src_len - i < 2u) {
                return 0;
            }
            len_code = (uint8_t)((tag_byte >> 2) & 15u);
            i += 2u;
            if (len_code == 15u) {
                if (i >= src_len) {
                    return 0;
                }
                i++;
            }
        } else if (tag == MINLZ_TAG_COPY2) {
            uint8_t len_code = (uint8_t)(tag_byte >> 2);
            if (src_len - i < 3u) {
                return 0;
            }
            copy2++;
            i += 3u;
            if (len_code == 61u) {
                if (i >= src_len) {
                    return 0;
                }
                i++;
            } else if (len_code == 62u) {
                if (src_len - i < 2u) {
                    return 0;
                }
                i += 2u;
            } else if (len_code == 63u) {
                if (src_len - i < 3u) {
                    return 0;
                }
                i += 3u;
            }
        } else {
            uint32_t val;
            size_t lit_len;
            if (src_len - i < 4u) {
                return 0;
            }
            val = minlz_load32(src + i);
            lit_len = (size_t)((val >> 3) & 3u);
            if ((val & 4u) == 0) {
                i += 3u;
                lit_len++;
                if (lit_len > src_len - i) {
                    return 0;
                }
                i += lit_len;
                fused++;
            } else {
                uint8_t len_code = (uint8_t)((val >> 5) & 63u);
                i += 4u;
                if (len_code == 61u) {
                    if (i >= src_len) {
                        return 0;
                    }
                    i++;
                } else if (len_code == 62u) {
                    if (src_len - i < 2u) {
                        return 0;
                    }
                    i += 2u;
                } else if (len_code == 63u) {
                    if (src_len - i < 3u) {
                        return 0;
                    }
                    i += 3u;
                }
                if (lit_len != 0u) {
                    if (lit_len > src_len - i) {
                        return 0;
                    }
                    i += lit_len;
                }
            }
        }
    }
    /*
     * Dense copy2/fused log/JSON blocks decode best in the checked C parser,
     * but repeat-heavy L2 output is faster through the generated dispatcher.
     * Keep the selector shape-based so L3 stays on the C parser while L2 exact
     * output can take the faster repeat path.
     */
    return fused >= 20u && copy2 >= 40u && repeats <= fused;
}
#endif

static MINLZ_ALWAYS_INLINE void minlz_copy_small_decode(uint8_t *dst, const uint8_t *src,
                                                        size_t len) {
    if (len >= 16) {
        if (len == 16) {
            minlz_store64(dst, minlz_load64(src));
            minlz_store64(dst + 8, minlz_load64(src + 8));
        } else {
            minlz_copy_17to64_asm(dst, src, len);
        }
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

static MINLZ_ALWAYS_INLINE void minlz_copy16_decode(uint8_t *dst, const uint8_t *src) {
#if MINLZ_HAVE_INLINE_ASM_AMD64 && !MINLZ_DECODE_COPY16_GPR
    __asm__ volatile(
        "movdqu (%[src]), %%xmm0\n\t"
        "movdqu %%xmm0, (%[dst])"
        :
        : [dst] "r"(dst), [src] "r"(src)
        : "xmm0", "memory");
#else
    minlz_store64(dst, minlz_load64(src));
    minlz_store64(dst + 8, minlz_load64(src + 8));
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_copy64_decode(uint8_t *dst, const uint8_t *src) {
    minlz_copy_17to64_asm(dst, src, 64);
}

static MINLZ_ALWAYS_INLINE void minlz_copy_65to256_decode(uint8_t *dst, const uint8_t *src,
                                                          size_t len) {
    minlz_copy64_decode(dst, src);
    if (len <= 80) {
        minlz_copy16_decode(dst + len - 16, src + len - 16);
    } else if (len <= 128) {
        minlz_copy64_decode(dst + len - 64, src + len - 64);
    } else if (len <= 192) {
        minlz_copy64_decode(dst + 64, src + 64);
        minlz_copy64_decode(dst + len - 64, src + len - 64);
    } else {
        minlz_copy64_decode(dst + 64, src + 64);
        minlz_copy64_decode(dst + 128, src + 128);
        minlz_copy64_decode(dst + len - 64, src + len - 64);
    }
}

static MINLZ_ALWAYS_INLINE void minlz_copy_65to128_exact_decode(uint8_t *dst,
                                                                const uint8_t *src,
                                                                size_t len) {
    minlz_copy64_decode(dst, src);
    dst += 64u;
    src += 64u;
    len -= 64u;
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
}

static MINLZ_ALWAYS_INLINE void minlz_copy_257to2048_decode(uint8_t *dst, const uint8_t *src,
                                                            size_t len) {
    while (len > 256u) {
        minlz_copy_65to256_decode(dst, src, 256u);
        dst += 256u;
        src += 256u;
        len -= 256u;
    }
    if (len > 64u) {
        minlz_copy_65to256_decode(dst, src, len);
    } else if (len != 0u) {
        minlz_copy_small_decode(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void minlz_copy_literal_decode(uint8_t *dst, const uint8_t *src,
                                                          size_t len) {
    if (MINLZ_LIKELY(len <= 64)) {
        minlz_copy_small_decode(dst, src, len);
    } else if (len <= 256) {
        minlz_copy_65to256_decode(dst, src, len);
    } else {
        memcpy(dst, src, len);
    }
}

static MINLZ_ALWAYS_INLINE void minlz_copy_literal_fast_decode(uint8_t *dst,
                                                               const uint8_t *src,
                                                               size_t len,
                                                               const uint8_t *dst_end,
                                                               const uint8_t *src_end) {
    if (MINLZ_LIKELY(len <= 16 &&
                     (size_t)(dst_end - dst) >= 16 &&
                     (size_t)(src_end - src) >= 16)) {
        minlz_copy16_decode(dst, src);
        return;
    }
    minlz_copy_literal_decode(dst, src, len);
}

static MINLZ_ALWAYS_INLINE void minlz_copy_match_exact_tiny_decode(uint8_t *out,
                                                                    size_t offset,
                                                                    size_t length) {
    const uint8_t *from = out - offset;
    if (length >= 8u) {
        minlz_store64(out, minlz_load64(from));
    } else if (length >= 4u) {
        minlz_store32(out, minlz_load32(from));
        minlz_store32(out + length - 4u, minlz_load32(from + length - 4u));
    } else {
        size_t i;
        for (i = 0; i < length; i++) {
            out[i] = from[i];
        }
    }
}

static int minlz_decode_tiny_literal_repeat(uint8_t *dst, size_t dst_len,
                                            const uint8_t *src, size_t src_len) {
    const uint8_t *ip = src;
    const uint8_t *src_end = src + src_len;
    uint8_t *op = dst;
    uint8_t *dst_end = dst + dst_len;
    size_t offset = 1u;
    size_t commands = 0u;
    int saw_copy = 0;

    while (ip < src_end) {
        uint8_t tag_byte = *ip;
        uint8_t tag = (uint8_t)(tag_byte & 3u);
        size_t length;
        if (tag == MINLZ_TAG_LITERAL) {
            uint8_t x = (uint8_t)(tag_byte >> 3);
            ip++;
            if (MINLZ_LIKELY(x < 29u)) {
                length = (size_t)x + 1u;
            } else if (x == 29u) {
                if (MINLZ_UNLIKELY(ip >= src_end)) {
                    return 0;
                }
                length = 30u + *ip++;
            } else {
                return 0;
            }
            if ((tag_byte & MINLZ_TAG_REPEAT) == 0) {
                if (MINLZ_UNLIKELY(length > 256u ||
                                   length > (size_t)(src_end - ip) ||
                                   length > (size_t)(dst_end - op))) {
                    return 0;
                }
                minlz_copy_literal_decode(op, ip, length);
                ip += length;
                op += length;
            } else {
                if (MINLZ_UNLIKELY(!saw_copy || length > 8u ||
                                   offset > (size_t)(op - dst) ||
                                   offset < length ||
                                   length > (size_t)(dst_end - op))) {
                    return 0;
                }
                minlz_copy_match_exact_tiny_decode(op, offset, length);
                op += length;
            }
        } else if (tag == MINLZ_TAG_COPY1) {
            uint16_t v;
            uint8_t len_code;
            if (MINLZ_UNLIKELY((size_t)(src_end - ip) < 2u)) {
                return 0;
            }
            v = minlz_load16(ip);
            len_code = (uint8_t)((tag_byte >> 2) & 15u);
            if (MINLZ_UNLIKELY(len_code == 15u)) {
                return 0;
            }
            offset = (size_t)(v >> 6) + 1u;
            length = 4u + len_code;
            ip += 2;
            if (MINLZ_UNLIKELY(length > 8u ||
                               offset > (size_t)(op - dst) ||
                               offset < length ||
                               length > (size_t)(dst_end - op))) {
                return 0;
            }
            minlz_copy_match_exact_tiny_decode(op, offset, length);
            op += length;
            saw_copy = 1;
        } else {
            return 0;
        }
        if (MINLZ_UNLIKELY(++commands > 2048u)) {
            return 0;
        }
    }
    return saw_copy && ip == src_end && op == dst_end;
}

static MINLZ_ALWAYS_INLINE void minlz_copy_forward_decode(uint8_t *dst, const uint8_t *src,
                                                          size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    __asm__ volatile("rep movsb"
                     : "+D"(dst), "+S"(src), "+c"(len)
                     :
                     : "memory");
#elif defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
    minlz_copy_forward_asm_amd64(dst, src, len);
#else
    memcpy(dst, src, len);
#endif
}

static MINLZ_ALWAYS_INLINE void minlz_copy_uncompressed_decode(uint8_t *dst, const uint8_t *src,
                                                               size_t len) {
    if (len <= 64u) {
        minlz_copy_small_decode(dst, src, len);
        return;
    }
    if (len <= 256u) {
        minlz_copy_65to256_decode(dst, src, len);
        return;
    }
#if MINLZ_DECODE_1K2K_AVX2_GO_FWD
    if (len >= 1024u && len <= 2048u && minlz_copy_uncompressed_avx2_go_fwd(dst, src, len)) {
        return;
    }
#endif
#if MINLZ_DECODE_1K2K_MEMCPY
    if (len >= 1024u && len <= 2048u) {
        memcpy(dst, src, len);
        return;
    }
#endif
    if (len <= 2048u) {
        minlz_copy_257to2048_decode(dst, src, len);
        return;
    }
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
#if MINLZ_DECODE_8K16K_PLAIN_MEMCPY
    if (len >= (8u << 10) && len <= (16u << 10)) {
        memcpy(dst, src, len);
        return;
    }
#endif
#if MINLZ_DECODE_LARGE_AVX2_GO_FWD
    if (len >= (256u << 10) && len < (1u << 20) &&
        minlz_copy_uncompressed_avx2_go_fwd(dst, src, len)) {
        return;
    }
#endif
    if (len >= (8u << 10) && len <= (64u << 10)) {
        minlz_store64(dst, minlz_load64(src));
        minlz_store64(dst + 6u, minlz_load64(src + 6u));
        memcpy(dst + 14u, src + 14u, len - 14u);
        return;
    }
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (len >= MINLZ_DECODE_UNCOMPRESSED_REPMOVSB_MIN && minlz_backend_has_asm_amd64()) {
        minlz_copy_forward_decode(dst, src, len);
        return;
    }
#endif
    memcpy(dst, src, len);
}

static MINLZ_ALWAYS_INLINE size_t minlz_store_pattern64_decode(uint8_t *dst, size_t length,
                                                               uint64_t pattern) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    uint8_t *p = dst;
    size_t qwords = length >> 3;
    if (qwords != 0) {
        __asm__ volatile(
            "1:\n\t"
            "movq %[pattern], (%[p])\n\t"
            "addq $8, %[p]\n\t"
            "decq %[qwords]\n\t"
            "jnz 1b"
            : [p] "+r"(p), [qwords] "+r"(qwords)
            : [pattern] "r"(pattern)
            : "cc", "memory");
    }
    return (size_t)(p - dst);
#else
    size_t i = 0;
    while (length - i >= 8) {
        minlz_store64(dst + i, pattern);
        i += 8;
    }
    return i;
#endif
}

static void minlz_copy_match_slow(uint8_t *out, size_t offset, size_t length);

static MINLZ_ALWAYS_INLINE void minlz_copy_match_hot(uint8_t *out, size_t offset, size_t length) {
    if (MINLZ_LIKELY(length <= MINLZ_DECODE_MATCH_HOT_MAX)) {
        size_t i = 0;
        const uint8_t *from = out - offset;
        if (offset >= length) {
            if (length <= 64u) {
                minlz_copy_small_decode(out, from, length);
#if MINLZ_DECODE_MATCH_EXACT_65_128
            } else if (length <= 128u) {
                minlz_copy_65to128_exact_decode(out, from, length);
#elif MINLZ_DECODE_MATCH_EXACT_65_80
            } else if (length <= 80u && offset <= 2048u) {
                minlz_copy_65to128_exact_decode(out, from, length);
#endif
            } else {
                minlz_copy_65to256_decode(out, from, length);
            }
            return;
        }
        if (offset == 1) {
            uint64_t pat = 0x0101010101010101ull * (uint64_t)from[0];
            i = minlz_store_pattern64_decode(out, length, pat);
        } else if (offset == 2) {
            uint16_t v = minlz_load16(from);
            uint64_t pat = (uint64_t)v | ((uint64_t)v << 16) |
                           ((uint64_t)v << 32) | ((uint64_t)v << 48);
            i = minlz_store_pattern64_decode(out, length, pat);
        } else if (offset == 4) {
            uint32_t v = minlz_load32(from);
            uint64_t pat = (uint64_t)v | ((uint64_t)v << 32);
            i = minlz_store_pattern64_decode(out, length, pat);
        } else if (offset == 8) {
            uint64_t pat = minlz_load64(from);
            i = minlz_store_pattern64_decode(out, length, pat);
        } else if (offset < 8) {
            while (length - i >= 8) {
                out[i + 0] = out[i + 0 - offset];
                out[i + 1] = out[i + 1 - offset];
                out[i + 2] = out[i + 2 - offset];
                out[i + 3] = out[i + 3 - offset];
                out[i + 4] = out[i + 4 - offset];
                out[i + 5] = out[i + 5 - offset];
                out[i + 6] = out[i + 6 - offset];
                out[i + 7] = out[i + 7 - offset];
                i += 8;
            }
        } else if (offset >= 16) {
            while (length - i >= 16) {
                minlz_store64(out + i, minlz_load64(out + i - offset));
                minlz_store64(out + i + 8, minlz_load64(out + i + 8 - offset));
                i += 16;
            }
            while (length - i >= 8) {
                minlz_store64(out + i, minlz_load64(out + i - offset));
                i += 8;
            }
        } else {
            while (length - i >= 8) {
                minlz_store64(out + i, minlz_load64(out + i - offset));
                i += 8;
            }
        }
        for (; i < length; i++) {
            out[i] = out[i - offset];
        }
        return;
    }
    minlz_copy_match_slow(out, offset, length);
}

static MINLZ_ALWAYS_INLINE void minlz_copy_match_fast_decode(uint8_t *out, size_t offset,
                                                             size_t length,
                                                             const uint8_t *dst_end) {
    if (MINLZ_LIKELY(length <= 16 && (size_t)(dst_end - out) >= 16)) {
        if (MINLZ_LIKELY(offset >= 16)) {
            minlz_copy16_decode(out, out - offset);
            return;
        }
        if (offset == 1) {
            uint64_t pat = 0x0101010101010101ull * (uint64_t)out[-1];
            minlz_store64(out, pat);
            minlz_store64(out + 8, pat);
            return;
        }
        if (offset == 2) {
            uint16_t v = minlz_load16(out - 2);
            uint64_t pat = (uint64_t)v | ((uint64_t)v << 16) |
                           ((uint64_t)v << 32) | ((uint64_t)v << 48);
            minlz_store64(out, pat);
            minlz_store64(out + 8, pat);
            return;
        }
        if (offset == 4) {
            uint32_t v = minlz_load32(out - 4);
            uint64_t pat = (uint64_t)v | ((uint64_t)v << 32);
            minlz_store64(out, pat);
            minlz_store64(out + 8, pat);
            return;
        }
        if (offset == 8) {
            uint64_t pat = minlz_load64(out - 8);
            minlz_store64(out, pat);
            minlz_store64(out + 8, pat);
            return;
        }
    }
    minlz_copy_match_hot(out, offset, length);
}

static void minlz_copy_match_slow(uint8_t *out, size_t offset, size_t length) {
    const uint8_t *from = out - offset;
    size_t i = 0;

    if (offset >= length) {
        if (length <= 64) {
            minlz_copy_small_decode(out, from, length);
            return;
        }
#if MINLZ_DECODE_MATCH_EXACT_65_128
        if (length <= 128) {
            minlz_copy_65to128_exact_decode(out, from, length);
            return;
        }
#elif MINLZ_DECODE_MATCH_EXACT_65_80
        if (length <= 80u && offset <= 2048u) {
            minlz_copy_65to128_exact_decode(out, from, length);
            return;
        }
#endif
        if (length <= 256) {
            minlz_copy_65to256_decode(out, from, length);
            return;
        }
#if defined(MINLZ_HAVE_ASM_AMD64) && MINLZ_HAVE_ASM_AMD64
        if (length >= 256 && minlz_backend_has_asm_amd64()) {
            minlz_copy_forward_decode(out, from, length);
            return;
        }
#endif
        memcpy(out, from, length);
        return;
    }
#if defined(MINLZ_HAVE_AVX512) && MINLZ_HAVE_AVX512
    if (length >= 192 && offset >= 64 && minlz_backend_has_avx512()) {
        minlz_copy_match_avx512(out, offset, length);
        return;
    }
#endif
#if defined(MINLZ_HAVE_AVX2) && MINLZ_HAVE_AVX2
    if (length >= 96 && offset >= 64 && minlz_backend_has_avx2()) {
        minlz_copy_match_avx2(out, offset, length);
        return;
    }
#endif
#if MINLZ_HAVE_INLINE_ASM_AMD64
    if (length >= MINLZ_DECODE_REPMOVSB_OVERLAP_MIN && offset >= 64 &&
        minlz_backend_has_asm_amd64()) {
        minlz_copy_forward_decode(out, from, length);
        return;
    }
#endif
    if (offset >= 64 && length <= 256) {
        minlz_copy_65to256_decode(out, from, length);
        return;
    }
    if (offset == 1) {
        memset(out, from[0], length);
        return;
    }
    if (offset == 2) {
        uint16_t v = minlz_load16(from);
        uint64_t pat = (uint64_t)v | ((uint64_t)v << 16) |
                       ((uint64_t)v << 32) | ((uint64_t)v << 48);
        i = minlz_store_pattern64_decode(out, length, pat);
    } else if (offset == 4) {
        uint32_t v = minlz_load32(from);
        uint64_t pat = (uint64_t)v | ((uint64_t)v << 32);
        i = minlz_store_pattern64_decode(out, length, pat);
    } else if (offset == 8) {
        uint64_t pat = minlz_load64(from);
        i = minlz_store_pattern64_decode(out, length, pat);
    } else if (offset < 8) {
        while (length - i >= 8) {
            out[i + 0] = out[i + 0 - offset];
            out[i + 1] = out[i + 1 - offset];
            out[i + 2] = out[i + 2 - offset];
            out[i + 3] = out[i + 3 - offset];
            out[i + 4] = out[i + 4 - offset];
            out[i + 5] = out[i + 5 - offset];
            out[i + 6] = out[i + 6 - offset];
            out[i + 7] = out[i + 7 - offset];
            i += 8;
        }
    } else if (offset >= 16) {
        while (length - i >= 16) {
            minlz_store64(out + i, minlz_load64(out + i - offset));
            minlz_store64(out + i + 8, minlz_load64(out + i + 8 - offset));
            i += 16;
        }
        while (length - i >= 8) {
            minlz_store64(out + i, minlz_load64(out + i - offset));
            i += 8;
        }
    } else if (offset >= 8) {
        while (length - i >= 8) {
            minlz_store64(out + i, minlz_load64(out + i - offset));
            i += 8;
        }
    }
    for (; i < length; i++) {
        out[i] = out[i - offset];
    }
}

static minlz_status minlz_decode_raw(uint8_t *dst, size_t dst_len,
                                     const uint8_t *src, size_t src_len) {
    if (dst_len >= 256u && dst_len <= (8u << 10) && src_len >= 16u &&
        minlz_decode_tiny_literal_repeat(dst, dst_len, src, src_len)) {
        return MINLZ_OK;
    }
#if MINLZ_HAVE_INLINE_ASM_AMD64 && MINLZ_ENABLE_DECODE_GOASM
    if (dst_len >= 256u && dst_len <= MINLZ_DECODE_GOASM_MAX_BLOCK &&
        src_len >= 16u &&
        !(dst_len > (8u << 10) && dst_len <= (16u << 10)) &&
        !(dst_len >= (256u << 10) && src_len * 400u <= dst_len * 71u &&
          src_len <= 10000u) &&
        /*
          * Dense blocks at 12-18.1% ratio decode faster in the checked C
         * scalar/SSE4.2 parser. The production route is ratio-only: scanning
         * the first 256 commands duplicated parser work and cost 3-5% on the
         * log/JSON L3 gate. The shape scan remains available as a diagnostic
         * build switch.
         */
#if MINLZ_DECODE_SKIP_MIDRATIO_GOASM
        !(dst_len >= (256u << 10) &&
          src_len * 100u >= dst_len * 12u &&
          src_len * 100u <= dst_len * 24u &&
          src_len * 1000u <= dst_len * MINLZ_DECODE_MIDRATIO_C_PERMILLE
#if MINLZ_DECODE_MIDRATIO_SHAPE_SCAN
          &&
          minlz_decode_midratio_prefers_c_parser(src, src_len)) &&
#else
          ) &&
#endif
#endif
        !(dst_len >= (256u << 10) && src_len * 2u >= dst_len) &&
        (dst_len <= (8u << 10) || src_len >= (dst_len + 15u) / 16u) &&
        minlz_backend_has_inline_asm_amd64()) {
        if (minlz_decode_block_goasm(dst, dst_len, src, src_len) == 0) {
            return MINLZ_OK;
        }
    }
#endif
    const uint8_t *ip = src;
    const uint8_t *src_end = src + src_len;
    uint8_t *op = dst;
    uint8_t *dst_end = dst + dst_len;
    size_t d;
    size_t s;
    size_t length = 0;
    size_t offset = 1;

    /*
     * Fast path inspired by the Go decoder and the old minlz-c decoder.
     * The 11-byte margin is the largest command prefix plus fused literals,
     * so command metadata reads below can be unchecked while payload/copy
     * bounds remain validated.
     */
    while ((size_t)(src_end - ip) > MINLZ_DECODE_FAST_LOOP_MARGIN &&
           (size_t)(dst_end - op) > MINLZ_DECODE_FAST_LOOP_MARGIN) {
        uint8_t tag_byte = *ip;
        switch (tag_byte & 0x03u) {
        case MINLZ_TAG_LITERAL: {
            uint8_t v = tag_byte;
            uint8_t x = (uint8_t)(v >> 3);
            if (MINLZ_LIKELY(x < 29)) {
                ip++;
                length = (size_t)x + 1u;
                if (MINLZ_UNLIKELY((v & MINLZ_TAG_REPEAT) != 0)) {
                    goto fast_copy;
                }
                if (MINLZ_LIKELY(length <= 16u)) {
                    minlz_copy16_decode(op, ip);
                } else {
                    minlz_copy_literal_decode(op, ip, length);
                }
                op += length;
                ip += length;
                continue;
            } else if (x == 29) {
                length = 30u + ip[1];
                ip += 2;
            } else if (x == 30) {
                length = 30u + minlz_load16(ip + 1);
                ip += 3;
            } else {
                length = 30u + (minlz_load32(ip) >> 8);
                ip += 4;
            }
            if (MINLZ_UNLIKELY((v & MINLZ_TAG_REPEAT) != 0)) {
                goto fast_copy;
            }
            if (MINLZ_UNLIKELY(length > (size_t)(dst_end - op) ||
                               length > (size_t)(src_end - ip))) {
                return MINLZ_CORRUPT;
            }
            minlz_copy_literal_fast_decode(op, ip, length, dst_end, src_end);
            op += length;
            ip += length;
            continue;
        }

        case MINLZ_TAG_COPY1: {
            uint16_t v = minlz_load16(ip);
            uint8_t len_code = (uint8_t)((tag_byte >> 2) & 15u);
            offset = (size_t)(v >> 6) + 1u;
            if (MINLZ_LIKELY(len_code <= 12u)) {
                length = 4u + len_code;
                ip += 2;
                d = (size_t)(op - dst);
                if (MINLZ_LIKELY(offset >= 16u && offset <= d)) {
                    minlz_copy16_decode(op, op - offset);
                    op += length;
                    continue;
                }
                break;
            }
            if (MINLZ_UNLIKELY(len_code == 15)) {
                length = 18u + ip[2];
                ip += 3;
            } else {
                length = 4u + len_code;
                ip += 2;
            }
            break;
        }

        case MINLZ_TAG_COPY2: {
            uint8_t len_code = (uint8_t)(tag_byte >> 2);
            offset = (size_t)minlz_load16(ip + 1) + MINLZ_MIN_COPY2_OFFSET;
            if (MINLZ_LIKELY(len_code <= 60u)) {
                length = 4u + len_code;
                ip += 3;
                d = (size_t)(op - dst);
                if (MINLZ_LIKELY(len_code <= 12u && offset <= d)) {
                    minlz_copy16_decode(op, op - offset);
                    op += length;
                    continue;
                }
                break;
            }
            if (len_code == 61) {
                length = 64u + ip[3];
                ip += 4;
            } else if (len_code == 62) {
                length = 64u + minlz_load16(ip + 3);
                ip += 5;
            } else {
                length = 64u + (minlz_load32(ip + 2) >> 8);
                ip += 6;
            }
            break;
        }

        default: {
            uint32_t val = minlz_load32(ip);
            int is_copy3 = (val & 4u) != 0;
            size_t lit_len = (size_t)((val >> 3) & 3u);
            if (MINLZ_LIKELY(!is_copy3)) {
                length = 4u + ((val >> 5) & 7u);
                offset = (size_t)((val >> 8) & 0xffffu) + MINLZ_MIN_COPY2_OFFSET;
                ip += 3;
                lit_len++;
                if (MINLZ_LIKELY(offset >= 16u &&
                                 offset <= (size_t)((op + lit_len) - dst))) {
                    minlz_store32(op, minlz_load32(ip));
                    op += lit_len;
                    ip += lit_len;
                    minlz_copy16_decode(op, op - offset);
                    op += length;
                    continue;
                }
            } else {
                uint8_t len_code = (uint8_t)((val >> 5) & 63u);
                offset = (size_t)(val >> 11) + MINLZ_MIN_COPY3_OFFSET;
                if (len_code < 61) {
                    length = 4u + len_code;
                    ip += 4;
                } else if (len_code == 61) {
                    length = 64u + ip[4];
                    ip += 5;
                } else if (len_code == 62) {
                    length = 64u + minlz_load16(ip + 4);
                    ip += 6;
                } else {
                    length = 64u + (minlz_load32(ip + 3) >> 8);
                    ip += 7;
                }
#if MINLZ_DECODE_COPY3_SHORT_INLINE
                if (len_code <= 12u && offset >= 16u &&
                    offset <= (size_t)(op - dst) + lit_len) {
                    if (lit_len != 0u) {
                        minlz_store32(op, minlz_load32(ip));
                        op += lit_len;
                        ip += lit_len;
                    }
                    minlz_copy16_decode(op, op - offset);
                    op += length;
                    continue;
                }
#endif
#if MINLZ_DECODE_COPY3_MEDIUM_INLINE
                if (length >= 17u && length <= 80u &&
                    offset <= (size_t)(op - dst) + lit_len &&
                    length + lit_len <= (size_t)(dst_end - op)) {
                    if (lit_len != 0u) {
                        minlz_store32(op, minlz_load32(ip));
                        op += lit_len;
                        ip += lit_len;
                    }
                    if (length <= 64u) {
                        minlz_copy_17to64_asm(op, op - offset, length);
                    } else {
                        minlz_copy64_decode(op, op - offset);
                        minlz_copy16_decode(op + length - 16u,
                                            op - offset + length - 16u);
                    }
                    op += length;
                    continue;
                }
#endif
            }
            if (MINLZ_LIKELY(lit_len > 0)) {
                if (MINLZ_UNLIKELY((size_t)(dst_end - op) < 4 ||
                                   (size_t)(src_end - ip) < 4)) {
                    return MINLZ_CORRUPT;
                }
                minlz_store32(op, minlz_load32(ip));
                op += lit_len;
                ip += lit_len;
            }
            break;
        }
        }

fast_copy:
        d = (size_t)(op - dst);
        if (MINLZ_UNLIKELY(offset == 0 || offset > d ||
                           length > (size_t)(dst_end - op))) {
            return MINLZ_CORRUPT;
        }
        minlz_copy_match_fast_decode(op, offset, length, dst_end);
        op += length;
    }

    s = (size_t)(ip - src);
    d = (size_t)(op - dst);

    while (s < src_len) {
        uint8_t tag = src[s] & 0x03u;
        if (tag == MINLZ_TAG_LITERAL) {
            uint8_t v = src[s++];
            uint8_t x = (uint8_t)(v >> 3);
            if (MINLZ_LIKELY(x < 29)) {
                length = (size_t)x + 1;
            } else if (x == 29) {
                if (src_len - s < 1) {
                    return MINLZ_CORRUPT;
                }
                length = 30u + src[s++];
            } else if (x == 30) {
                if (src_len - s < 2) {
                    return MINLZ_CORRUPT;
                }
                length = 30u + minlz_load16(src + s);
                s += 2;
            } else {
                if (src_len - s < 3) {
                    return MINLZ_CORRUPT;
                }
                length = 30u + minlz_load24(src + s);
                s += 3;
            }
            if (MINLZ_LIKELY((v & MINLZ_TAG_REPEAT) == 0)) {
                if (MINLZ_UNLIKELY(length > dst_len - d || length > src_len - s)) {
                    return MINLZ_CORRUPT;
                }
                minlz_copy_literal_decode(dst + d, src + s, length);
                d += length;
                s += length;
                continue;
            }
        } else if (tag == MINLZ_TAG_COPY1) {
            uint16_t v;
            uint8_t len_code;
            if (src_len - s < 2) {
                return MINLZ_CORRUPT;
            }
            v = minlz_load16(src + s);
            len_code = (uint8_t)((src[s] >> 2) & 15u);
            offset = (size_t)(v >> 6) + 1u;
            s += 2;
            if (MINLZ_UNLIKELY(len_code == 15)) {
                if (src_len - s < 1) {
                    return MINLZ_CORRUPT;
                }
                length = 18u + src[s++];
            } else {
                length = 4u + len_code;
            }
        } else if (tag == MINLZ_TAG_COPY2) {
            uint8_t len_code;
            if (src_len - s < 3) {
                return MINLZ_CORRUPT;
            }
            len_code = (uint8_t)(src[s] >> 2);
            offset = (size_t)minlz_load16(src + s + 1) + MINLZ_MIN_COPY2_OFFSET;
            s += 3;
            if (MINLZ_LIKELY(len_code <= 60)) {
                length = 4u + len_code;
            } else if (len_code == 61) {
                if (src_len - s < 1) {
                    return MINLZ_CORRUPT;
                }
                length = 64u + src[s++];
            } else if (len_code == 62) {
                if (src_len - s < 2) {
                    return MINLZ_CORRUPT;
                }
                length = 64u + minlz_load16(src + s);
                s += 2;
            } else {
                if (src_len - s < 3) {
                    return MINLZ_CORRUPT;
                }
                length = 64u + minlz_load24(src + s);
                s += 3;
            }
        } else {
            uint32_t val;
            int is_copy3;
            size_t lit_len;
            if (src_len - s < 4) {
                return MINLZ_CORRUPT;
            }
            val = minlz_load32(src + s);
            is_copy3 = (val & 4u) != 0;
            lit_len = (size_t)((val >> 3) & 3u);
            if (MINLZ_LIKELY(!is_copy3)) {
                length = 4u + ((val >> 5) & 7u);
                offset = (size_t)((val >> 8) & 0xffffu) + MINLZ_MIN_COPY2_OFFSET;
                s += 3;
                lit_len++;
            } else {
                uint8_t len_code = (uint8_t)((val >> 5) & 63u);
                offset = (size_t)(val >> 11) + MINLZ_MIN_COPY3_OFFSET;
                s += 4;
                if (len_code < 61) {
                    length = 4u + len_code;
                } else if (len_code == 61) {
                    if (src_len - s < 1) {
                        return MINLZ_CORRUPT;
                    }
                    length = 64u + src[s++];
                } else if (len_code == 62) {
                    if (src_len - s < 2) {
                        return MINLZ_CORRUPT;
                    }
                    length = 64u + minlz_load16(src + s);
                    s += 2;
                } else {
                    if (src_len - s < 3) {
                        return MINLZ_CORRUPT;
                    }
                    length = 64u + minlz_load24(src + s);
                    s += 3;
                }
            }
            if (MINLZ_LIKELY(lit_len > 0)) {
                if (MINLZ_UNLIKELY(lit_len > dst_len - d || lit_len > src_len - s)) {
                    return MINLZ_CORRUPT;
                }
                minlz_copy_literal_decode(dst + d, src + s, lit_len);
                d += lit_len;
                s += lit_len;
            }
        }

        if (MINLZ_UNLIKELY(offset == 0 || offset > d || length > dst_len - d)) {
            return MINLZ_CORRUPT;
        }
        minlz_copy_match_hot(dst + d, offset, length);
        d += length;
    }

    return d == dst_len ? MINLZ_OK : MINLZ_CORRUPT;
}

minlz_status minlz_payload_decoded_len(const uint8_t *src, size_t src_len,
                                       size_t *decoded_len, size_t *header_len) {
    size_t n = 0;
    size_t h = 0;
    minlz_status st;
    if (!src || !decoded_len || !header_len) {
        return MINLZ_CORRUPT;
    }
    st = minlz_read_uvarint(src, src_len, &n, &h);
    if (st != MINLZ_OK) {
        return st;
    }
    if (n > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (n == 0 || n < src_len - h) {
        return MINLZ_CORRUPT;
    }
    *decoded_len = n;
    *header_len = h;
    return MINLZ_OK;
}

minlz_status minlz_decode_payload(uint8_t *dst, size_t dst_len,
                                  const uint8_t *src, size_t src_len) {
    if (dst_len > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (dst_len == 0) {
        return src_len == 0 ? MINLZ_OK : MINLZ_CORRUPT;
    }
    if (dst_len != 0 && (!dst || !src)) {
        return MINLZ_CORRUPT;
    }
    return minlz_decode_raw(dst, dst_len, src, src_len);
}

size_t minlz_max_encoded_len(size_t src_len) {
    if (src_len > MINLZ_MAX_BLOCK_SIZE) {
        return 0;
    }
    if (src_len == 0) {
        return 1;
    }
    return src_len + 2;
}

minlz_status minlz_decoded_len(const uint8_t *src, size_t src_len, size_t *decoded_len) {
    const uint8_t *p;
    size_t n;
    size_t h;
    minlz_status st;

    if (!src || !decoded_len || src_len == 0) {
        return MINLZ_CORRUPT;
    }
    if (src[0] != 0) {
        return MINLZ_UNSUPPORTED;
    }
    if (src_len == 1) {
        *decoded_len = 0;
        return MINLZ_OK;
    }
    p = src + 1;
    st = minlz_read_uvarint(p, src_len - 1, &n, &h);
    if (st != MINLZ_OK) {
        return st;
    }
    if (n > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    p += h;
    if ((size_t)(p - src) == src_len) {
        return MINLZ_CORRUPT;
    }
    if (n == 0) {
        *decoded_len = src_len - (size_t)(p - src);
        return MINLZ_OK;
    }
    if (n < src_len - (size_t)(p - src)) {
        return MINLZ_CORRUPT;
    }
    *decoded_len = n;
    return MINLZ_OK;
}

int minlz_is_block(const uint8_t *src, size_t src_len, size_t *decoded_len) {
    size_t n = 0;
    minlz_status st = minlz_decoded_len(src, src_len, &n);
    if (st == MINLZ_OK && decoded_len) {
        *decoded_len = n;
    }
    return st == MINLZ_OK;
}

static MINLZ_ALWAYS_INLINE minlz_status minlz_decode_uncompressed_8k16k(
    uint8_t *dst, size_t dst_cap, size_t *dst_len, const uint8_t *src, size_t src_len) {
    size_t want = src_len - 2u;
    *dst_len = want;
    if (want > dst_cap) {
        return MINLZ_DST_TOO_SMALL;
    }
    if (!dst) {
        return MINLZ_CORRUPT;
    }
    if (want <= (8u << 10)) {
        memmove(dst, src + 2u, want);
        return MINLZ_OK;
    }
#if MINLZ_DECODE_8K16K_AVX2_GO_FWD
    if (want == 10000u && minlz_copy_uncompressed_10000_avx2(dst, src + 2u)) {
        return MINLZ_OK;
    }
    if (minlz_copy_uncompressed_avx2_go_fwd(dst, src + 2u, want)) {
        return MINLZ_OK;
    }
#endif
#if MINLZ_DECODE_8K16K_AVX2_ALIGNED_STORE
    if (minlz_copy_uncompressed_avx2_aligned_store(dst, src + 2u, want)) {
        return MINLZ_OK;
    }
#endif
#if MINLZ_ENABLE_RAWCOPY_SSE42_QWORD
    if (minlz_copy_uncompressed_sse42_qword(dst, src + 2u, want)) {
        return MINLZ_OK;
    }
#endif
#if MINLZ_ENABLE_RAWCOPY_SSE42_XMM_ALIGNED_STORE
    if (minlz_copy_uncompressed_sse42_xmm_aligned_store(dst, src + 2u, want, 0)) {
        return MINLZ_OK;
    }
#endif
#if MINLZ_DECODE_8K16K_DIRECT_MEMMOVE
    memmove(dst, src + 2u, want);
#else
    memcpy(dst, src + 2u, want);
#endif
    return MINLZ_OK;
}

static MINLZ_NOINLINE int minlz_copy_large_raw_sse_nt(uint8_t *dst,
                                                       const uint8_t *src,
                                                       size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    minlz_backend backend = minlz_select_backend();
    size_t prefix;
    size_t blocks;
    size_t tail;
    if ((backend != MINLZ_BACKEND_SSE42 && backend != MINLZ_BACKEND_ASM_AMD64) ||
        len < MINLZ_DECODE_LARGE_RAW_SSE_NT_MIN) {
        return 0;
    }
    prefix = (16u - ((uintptr_t)dst & 15u)) & 15u;
    if (prefix != 0u) {
        memcpy(dst, src, prefix);
        dst += prefix;
        src += prefix;
        len -= prefix;
    }
    blocks = len >> 9;
    tail = len & 511u;
    if (blocks != 0u) {
        __asm__ volatile(
            ".p2align 4\n\t"
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
            "movntdq %%xmm0, 0(%[dst])\n\t"
            "movntdq %%xmm1, 16(%[dst])\n\t"
            "movntdq %%xmm2, 32(%[dst])\n\t"
            "movntdq %%xmm3, 48(%[dst])\n\t"
            "movntdq %%xmm4, 64(%[dst])\n\t"
            "movntdq %%xmm5, 80(%[dst])\n\t"
            "movntdq %%xmm6, 96(%[dst])\n\t"
            "movntdq %%xmm7, 112(%[dst])\n\t"
            "movntdq %%xmm8, 128(%[dst])\n\t"
            "movntdq %%xmm9, 144(%[dst])\n\t"
            "movntdq %%xmm10, 160(%[dst])\n\t"
            "movntdq %%xmm11, 176(%[dst])\n\t"
            "movntdq %%xmm12, 192(%[dst])\n\t"
            "movntdq %%xmm13, 208(%[dst])\n\t"
            "movntdq %%xmm14, 224(%[dst])\n\t"
            "movntdq %%xmm15, 240(%[dst])\n\t"
            "movdqu 256(%[src]), %%xmm0\n\t"
            "movdqu 272(%[src]), %%xmm1\n\t"
            "movdqu 288(%[src]), %%xmm2\n\t"
            "movdqu 304(%[src]), %%xmm3\n\t"
            "movdqu 320(%[src]), %%xmm4\n\t"
            "movdqu 336(%[src]), %%xmm5\n\t"
            "movdqu 352(%[src]), %%xmm6\n\t"
            "movdqu 368(%[src]), %%xmm7\n\t"
            "movdqu 384(%[src]), %%xmm8\n\t"
            "movdqu 400(%[src]), %%xmm9\n\t"
            "movdqu 416(%[src]), %%xmm10\n\t"
            "movdqu 432(%[src]), %%xmm11\n\t"
            "movdqu 448(%[src]), %%xmm12\n\t"
            "movdqu 464(%[src]), %%xmm13\n\t"
            "movdqu 480(%[src]), %%xmm14\n\t"
            "movdqu 496(%[src]), %%xmm15\n\t"
            "movntdq %%xmm0, 256(%[dst])\n\t"
            "movntdq %%xmm1, 272(%[dst])\n\t"
            "movntdq %%xmm2, 288(%[dst])\n\t"
            "movntdq %%xmm3, 304(%[dst])\n\t"
            "movntdq %%xmm4, 320(%[dst])\n\t"
            "movntdq %%xmm5, 336(%[dst])\n\t"
            "movntdq %%xmm6, 352(%[dst])\n\t"
            "movntdq %%xmm7, 368(%[dst])\n\t"
            "movntdq %%xmm8, 384(%[dst])\n\t"
            "movntdq %%xmm9, 400(%[dst])\n\t"
            "movntdq %%xmm10, 416(%[dst])\n\t"
            "movntdq %%xmm11, 432(%[dst])\n\t"
            "movntdq %%xmm12, 448(%[dst])\n\t"
            "movntdq %%xmm13, 464(%[dst])\n\t"
            "movntdq %%xmm14, 480(%[dst])\n\t"
            "movntdq %%xmm15, 496(%[dst])\n\t"
            "addq $512, %[src]\n\t"
            "addq $512, %[dst]\n\t"
            "decq %[blocks]\n\t"
            "jnz 1b\n\t"
            "sfence"
            : [dst] "+r"(dst), [src] "+r"(src), [blocks] "+r"(blocks)
            :
            : "xmm0", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5", "xmm6", "xmm7",
              "xmm8", "xmm9", "xmm10", "xmm11", "xmm12", "xmm13", "xmm14",
              "xmm15", "cc", "memory");
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

static MINLZ_ALWAYS_INLINE void minlz_copy_large_raw_checked(uint8_t *dst,
                                                             const uint8_t *src,
                                                             size_t len) {
#if MINLZ_HAVE_INLINE_ASM_AMD64
    /*
     * The caller has already checked dst_cap and the complete raw source
     * extent. Keep those opaque here so LTO/Fortify does not add a duplicate
     * __memcpy_chk object-size branch to every large raw decode.
     */
    __asm__ volatile("" : "+r"(dst), "+r"(src) : : "memory");
#endif
    if (len < MINLZ_DECODE_LARGE_RAW_SSE_NT_MIN) {
        memmove(dst, src, len);
        return;
    }
    if (minlz_copy_large_raw_sse_nt(dst, src, len)) {
        return;
    }
    memcpy(dst, src, len);
}

#if MINLZ_DECODE_WS_SPLIT_RAW_FAST
static MINLZ_NOINLINE minlz_status minlz_decode_raw_mid_checked(
    uint8_t *dst, size_t dst_cap, size_t *dst_len,
    const uint8_t *payload, size_t want) {
    *dst_len = want;
    if (want > dst_cap) {
        return MINLZ_DST_TOO_SMALL;
    }
    if (!dst) {
        return MINLZ_CORRUPT;
    }
#if MINLZ_HAVE_INLINE_ASM_AMD64
    __asm__ volatile("" : "+r"(dst), "+r"(payload) : : "memory");
#endif
#if MINLZ_DECODE_RAW_MID_MEMCPY_NONOVERLAP
    {
        const uintptr_t d = (uintptr_t)dst;
        const uintptr_t s = (uintptr_t)payload;
        const int nonoverlap = d <= s ? s - d >= want : d - s >= want;
        if (nonoverlap) {
            memcpy(dst, payload, want);
        } else {
            memmove(dst, payload, want);
        }
    }
#else
    memmove(dst, payload, want);
#endif
    return MINLZ_OK;
}

static MINLZ_NOINLINE minlz_status minlz_decode_raw_large_checked(
    uint8_t *dst, size_t dst_cap, size_t *dst_len,
    const uint8_t *src, size_t src_len) {
    const size_t want = src_len - 2u;
    if (want > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    *dst_len = want;
    if (want > dst_cap) {
        return MINLZ_DST_TOO_SMALL;
    }
    if (!dst) {
        return MINLZ_CORRUPT;
    }
    minlz_copy_large_raw_checked(dst, src + 2u, want);
    return MINLZ_OK;
}
#endif

#if MINLZ_DECODE_WS_SPLIT_RAW_FAST
static MINLZ_NOINLINE minlz_status minlz_decode_ws_impl(
    minlz_workspace *ws, uint8_t *dst, size_t dst_cap, size_t *dst_len,
    const uint8_t *src, size_t src_len) {
#else
minlz_status minlz_decode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                             size_t *dst_len, const uint8_t *src, size_t src_len) {
#endif
    (void)ws;

    if (!dst_len) {
        return MINLZ_CORRUPT;
    }
    /*
     * Raw blocks up to 64 bytes are common in latency measurements.  Keep
     * their validation identical to the general raw path, but avoid the
     * larger-size dispatch ladder before the copy primitive is reached.
     */
#if MINLZ_DECODE_TINY_RAW_DIRECT_MEMCPY
    if (src && src_len >= 2u && src_len <= 66u && src[0] == 0 && src[1] == 0) {
        size_t want = src_len - 2u;
        *dst_len = want;
        if (want > dst_cap) {
            return MINLZ_DST_TOO_SMALL;
        }
        if (want == 0) {
            return MINLZ_OK;
        }
        if (!dst) {
            return MINLZ_CORRUPT;
        }
        memcpy(dst, src + 2u, want);
        return MINLZ_OK;
    }
#endif
    if (src && src_len >= 2u && src[0] == 0 && src[1] == 0) {
        size_t want = src_len - 2u;
        if (want >= (8u << 10) && want <= (16u << 10)) {
            if (want == 10000u && !minlz_backend_has_avx2()) {
                *dst_len = 10000u;
                if (10000u > dst_cap) {
                    return MINLZ_DST_TOO_SMALL;
                }
                if (!dst) {
                    return MINLZ_CORRUPT;
                }
                memcpy(dst, src + 2u, 10000u);
                return MINLZ_OK;
            }
            return minlz_decode_uncompressed_8k16k(dst, dst_cap, dst_len, src, src_len);
        }
        if (want == 1024u || want == 2048u) {
            *dst_len = want;
            if (want > dst_cap) {
                return MINLZ_DST_TOO_SMALL;
            }
            if (want == 0) {
                return MINLZ_OK;
            }
            if (!dst) {
                return MINLZ_CORRUPT;
            }
            memmove(dst, src + 2u, want);
            return MINLZ_OK;
        }
    }
    {
    const uint8_t *p;
    size_t used;
    size_t want;
    size_t declared;
    minlz_status st;

    if (src && src_len >= 2u && src_len <= MINLZ_DECODE_TINY_UNCOMPRESSED_MAX &&
        src[0] == 0 && src[1] == 0) {
        want = src_len - 2u;
        *dst_len = want;
        if (want > dst_cap) {
            return MINLZ_DST_TOO_SMALL;
        }
        if (want == 0) {
            return MINLZ_OK;
        }
        if (!dst) {
            return MINLZ_CORRUPT;
        }
        minlz_copy_uncompressed_decode(dst, src + 2, want);
        return MINLZ_OK;
    }
    if (src && src_len > 0 && src[0] == 0) {
        if (src_len == 1) {
            *dst_len = 0;
            return MINLZ_OK;
        }
        if (src[1] == 0) {
            want = src_len - 2u;
            if (want > MINLZ_MAX_BLOCK_SIZE) {
                return MINLZ_TOO_LARGE;
            }
            *dst_len = want;
            if (want > dst_cap) {
                return MINLZ_DST_TOO_SMALL;
            }
            if (want == 0) {
                return MINLZ_OK;
            }
            if (!dst) {
                return MINLZ_CORRUPT;
            }
            minlz_copy_uncompressed_decode(dst, src + 2, want);
            return MINLZ_OK;
        }
    }
    if (!src || src_len == 0) {
        return MINLZ_CORRUPT;
    }
    if (src[0] != 0) {
        return MINLZ_UNSUPPORTED;
    }
    used = 0;
    st = minlz_read_uvarint(src + 1, src_len - 1, &declared, &used);
    if (st != MINLZ_OK) {
        return st;
    }
    if (declared > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    p = src + 1 + used;
    if ((size_t)(p - src) == src_len) {
        return MINLZ_CORRUPT;
    }
    if (declared == 0) {
        want = src_len - (size_t)(p - src);
        if (want > MINLZ_MAX_BLOCK_SIZE) {
            return MINLZ_TOO_LARGE;
        }
    } else {
        size_t payload_len = src_len - (size_t)(p - src);
        if (declared < payload_len) {
            return MINLZ_CORRUPT;
        }
        want = declared;
    }
    *dst_len = want;
    if (want > dst_cap) {
        return MINLZ_DST_TOO_SMALL;
    }
    if (want == 0) {
        return MINLZ_OK;
    }
    if (!dst) {
        return MINLZ_CORRUPT;
    }
    if (declared == 0) {
        minlz_copy_uncompressed_decode(dst, p, want);
        return MINLZ_OK;
    }
    return minlz_decode_raw(dst, want, p, src_len - (size_t)(p - src));
    }
}

#if MINLZ_DECODE_WS_SPLIT_RAW_FAST
minlz_status minlz_decode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                             size_t *dst_len, const uint8_t *src, size_t src_len) {
    if (!dst_len) {
        return MINLZ_CORRUPT;
    }
    if (src && src_len >= (256u << 10) + 2u && src[0] == 0 && src[1] == 0) {
        const size_t want = src_len - 2u;
        if (want < MINLZ_DECODE_LARGE_RAW_SSE_NT_MIN) {
            return minlz_decode_raw_mid_checked(dst, dst_cap, dst_len, src + 2u, want);
        }
        return minlz_decode_raw_large_checked(dst, dst_cap, dst_len, src, src_len);
    }
    return minlz_decode_ws_impl(ws, dst, dst_cap, dst_len, src, src_len);
}
#endif

minlz_status minlz_decode(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                          const uint8_t *src, size_t src_len) {
    return minlz_decode_ws(NULL, dst, dst_cap, dst_len, src, src_len);
}
