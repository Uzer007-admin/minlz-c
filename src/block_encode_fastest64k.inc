#if defined(MINLZ_FASTEST64K_FIXED_INLINE_PROBE)
#define MINLZ_FASTEST64K_EXTEND(src, s, candidate, src_len) \
    minlz_extend_match_int_fastest_impl((src), (s), (candidate), (src_len), MINLZ_L1_MATCH_LONG_SSE2)
#elif defined(MINLZ_FASTEST64K_PREFIX_SSE2)
#define MINLZ_FASTEST64K_EXTEND(src, s, candidate, src_len) \
    minlz_extend_match_int_fastest_impl((src), (s), (candidate), (src_len), MINLZ_L1_MATCH_PREFIX_SSE2)
#else
#define MINLZ_FASTEST64K_EXTEND minlz_extend_match_int_fastest
#endif

/* Shared search loop, instantiated with byte or word positions.
 * Keep the word version independent of the tiny-block specialization. */
static MINLZ_ALWAYS_INLINE size_t MINLZ_FASTEST64K_IMPL(minlz_workspace *ws, uint8_t * restrict dst,
                                                 size_t dst_cap,
                                                 const uint8_t * restrict src, size_t src_len,
    const unsigned table_bits, const int skip_log) {
    size_t table_size;
    const int input_margin = 8;
    int s_limit;
    int dst_limit;
    int next_emit = 0;
    int s = 1;
    int repeat = 1;
    int d = 0;
    uint64_t cv;
    MINLZ_FASTEST64K_ENTRY *table;
    uint8_t *table_buf;
    const int check_repeats = MINLZ_FASTEST_CHECK_REPEATS;
#if defined(MINLZ_FASTEST64K_FIXED_INLINE_PROBE)
    const int use_inline_probe = 1;
    const unsigned hash_shift = (64u - table_bits) & 63u;
#else
    int use_inline_probe = 0;
    unsigned hash_shift = 0;
#endif
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    /* Short tables benefit from fewer live hash values. Keep the three
     * multiplies parallel in the larger incompressible search buckets. */
    const int short_word_table = MINLZ_FASTEST64K_ENTRY_BYTES == 2 && table_bits == 9u;
    const int delay_third_hash = table_bits <= 10u;
#else
    const int delay_third_hash = 0;
#endif

    if (src_len <= MINLZ_MIN_NON_LITERAL_BLOCK_SIZE || src_len > 65536u) {
        return 0;
    }
    table_size = (size_t)1u << table_bits;
    (void)dst_cap;
    /* These tables are at most 16 KiB, below the hugepage promotion threshold. */
    if (ws && ws->buffer_caps[2] >= table_size * sizeof(table[0])) {
        table_buf = ws->buffers[2];
    } else if (minlz_workspace_prepare_encode_scratch(ws, 2, table_size * sizeof(table[0]),
                                                    &table_buf) != MINLZ_OK) {
        return 0;
    }
    table = (MINLZ_FASTEST64K_ENTRY *)table_buf;
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    /* A fixed 1 KiB clear does not need its own CPU dispatch. */
    if (!short_word_table && table_size * sizeof(table[0]) <= 2048u) {
        if (minlz_backend_has_avx2()) {
            minlz_clear_l1_table_avx2(table_buf, table_size * sizeof(table[0]));
        } else {
            const __m128i zero = _mm_setzero_si128();
            uint8_t *p = table_buf;
            uint8_t *end = p + table_size * sizeof(table[0]);
            do {
                _mm_storeu_si128((__m128i *)(void *)p, zero);
                _mm_storeu_si128((__m128i *)(void *)(p + 16u), zero);
                _mm_storeu_si128((__m128i *)(void *)(p + 32u), zero);
                _mm_storeu_si128((__m128i *)(void *)(p + 48u), zero);
                p += 64u;
            } while (p != end);
        }
    } else if (table_size * sizeof(table[0]) == 8192u && src_len >= 32768u && minlz_backend_has_avx2()) {
        minlz_clear_l1_table_avx2(table_buf, table_size * sizeof(table[0]));
    } else
#endif
    {
        memset(table, 0, table_size * sizeof(table[0]));
    }
#if MINLZ_FASTEST64K_ENTRY_BYTES == 2 && MINLZ_ENABLE_FASTEST64K_INLINE_PROBE && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(MINLZ_FASTEST64K_FIXED_INLINE_PROBE)
    use_inline_probe = minlz_backend_has_inline_asm_amd64();
    hash_shift = (64u - table_bits) & 63u;
#endif
    s_limit = (int)src_len - input_margin;
    dst_limit = (int)src_len - ((int)src_len >> 5) - 6;
#if MINLZ_ENABLE_INTRINSICS && MINLZ_HAVE_INLINE_ASM_AMD64 && !defined(_WIN32)
    if (short_word_table) {
        /* Before the first match, the last offset is one and no literals
         * have been emitted. Replay the first hit in the general loop. */
        for (;;) {
            const int next_s = s + (s >> skip_log) + 4;
            if (next_s > s_limit) {
                /* One literal covering the whole block would exceed dst_limit. */
                return 0;
            }
            const uint64_t word = minlz_load64(src + s);
            const uint32_t word1 = (uint32_t)(word >> 8);
            const uint32_t word2 = (uint32_t)(word >> 16);
            const uint32_t h0 = minlz_hash5(word, table_bits);
            const uint32_t h1 = minlz_hash5(word >> 8, table_bits);
            const uint32_t h2 = minlz_hash5(word >> 16, table_bits);
            const MINLZ_FASTEST64K_ENTRY c0 = table[h0];
            const MINLZ_FASTEST64K_ENTRY c1 = table[h1];
            if ((check_repeats && word1 == (uint32_t)word) ||
                (uint32_t)word == minlz_load32(src + c0) ||
                word1 == minlz_load32(src + c1)) {
                break;
            }
            /* This lookup must see the first two writes if the hashes
             * collide. Undo them when the main loop needs to replay a hit. */
            table[h0] = (MINLZ_FASTEST64K_ENTRY)s;
            table[h1] = (MINLZ_FASTEST64K_ENTRY)(s + 1);
            const MINLZ_FASTEST64K_ENTRY c2 = table[h2];
            if (word2 == minlz_load32(src + c2)) {
                table[h1] = c1;
                table[h0] = c0;
                break;
            }
            table[h2] = (MINLZ_FASTEST64K_ENTRY)(s + 2);
            s = next_s;
        }
    }
#endif
    cv = minlz_load64(src + s);

    for (;;) {
        int candidate = 0;
        for (;;) {
            int next_s = s + ((s - next_emit) >> skip_log) + 4;
            uint32_t hash0;
            uint32_t hash1;
            uint32_t hash2;
            int candidate2;

            if (next_s > s_limit) {
                goto emit_remainder;
            }
            if (MINLZ_LIKELY(use_inline_probe)) {
                minlz_fastest64k_probe_inline_asm_amd64(
                    (uint16_t *)table, cv, hash_shift, (MINLZ_FASTEST64K_ENTRY)s, &candidate, &candidate2, &hash2);
            } else {
                hash0 = minlz_hash5(cv, table_bits);
                hash1 = minlz_hash5(cv >> 8, table_bits);
                candidate = (int)table[hash0];
                candidate2 = (int)table[hash1];
                table[hash0] = (MINLZ_FASTEST64K_ENTRY)s;
                table[hash1] = (MINLZ_FASTEST64K_ENTRY)(s + 1);
                if (!delay_third_hash) {
                    hash2 = minlz_hash5(cv >> 16, table_bits);
                }

            }

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
                n = minlz_emit_literal_inline_fast(dst + d, src + next_emit,
                                                   (size_t)(base - next_emit));
                if (n == 0 && base != next_emit) {
                    d = 0;
                    goto done;
                }
                d += (int)n;

                candidate = s - repeat + 5;
                s += 5;
                MINLZ_FASTEST64K_EXTEND(src, &s, &candidate, (int)src_len);
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

            if ((uint32_t)cv == minlz_load32(src + candidate)) {
                break;
            }
            if (delay_third_hash && !use_inline_probe) {
                hash2 = minlz_hash5(cv >> 16, table_bits);
            }
            candidate = (int)table[hash2];
            if ((uint32_t)(cv >> 8) == minlz_load32(src + candidate2)) {
                table[hash2] = (MINLZ_FASTEST64K_ENTRY)(s + 2);
                candidate = candidate2;
                s++;
                break;
            }
            table[hash2] = (MINLZ_FASTEST64K_ENTRY)(s + 2);
            if ((uint32_t)(cv >> 16) == minlz_load32(src + candidate)) {
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
            MINLZ_FASTEST64K_EXTEND(src, &s, &candidate, (int)src_len);
            length = s - base;
            if (next_emit != base) {
                int lit_len = base - next_emit;
                if (!MINLZ_FASTEST_FUSE_LITERALS ||
                    lit_len > (int)MINLZ_MAX_COPY2_LITS ||
                    repeat < (int)MINLZ_MIN_COPY2_OFFSET) {
                    if (d + (base - next_emit) > dst_limit) {
                        d = 0;
                        goto done;
                    }
                    n = minlz_emit_literal_inline_fast(dst + d, src + next_emit, (size_t)lit_len);
                    if (n == 0) {
                        d = 0;
                        goto done;
                    }
                    d += (int)n;
                    n = minlz_emit_copy_fast(dst + d, (size_t)repeat, (size_t)length);
                } else {
                    n = minlz_emit_copy_lits2_inline_fast(dst + d, src + next_emit,
                                                          (size_t)lit_len,
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
            m2_hash = minlz_hash5(x, table_bits);
            x >>= 16;
            curr_hash = minlz_hash5(x, table_bits);
            immediate_candidate = (int)table[curr_hash];
            table[m2_hash] = (MINLZ_FASTEST64K_ENTRY)(s - 2);
            table[curr_hash] = (MINLZ_FASTEST64K_ENTRY)s;
            if ((uint32_t)x != minlz_load32(src + immediate_candidate)) {
                cv = minlz_load64(src + s + 1);
                s++;
                break;
            }
            repeat = s - immediate_candidate;
            base = s;
            s += 4;
            immediate_candidate += 4;
            MINLZ_FASTEST64K_EXTEND(
                src, &s, &immediate_candidate, (int)src_len);
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
        n = minlz_emit_literal_inline_fast(dst + d, src + next_emit,
                                           src_len - (size_t)next_emit);
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


#undef MINLZ_FASTEST64K_EXTEND
