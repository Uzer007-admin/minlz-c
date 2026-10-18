/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <inttypes.h>
#include <stdlib.h>

#define MINLZ_SEARCH_MIN_LOG2 8u
#define MINLZ_SEARCH_MAX_LOG2 23u
#define MINLZ_SEARCH_DEFAULT_MATCH_LEN 6u
#define MINLZ_SEARCH_DEFAULT_MAX_POP 70u
#define MINLZ_SEARCH_DEFAULT_MAX_REDUCED_POP 25u
#define MINLZ_SEARCH_PREFIX_MAX_REDUCED_POP 10u
#define MINLZ_SEARCH_DEFAULT_COMPRESS_SKIP_X100 1000u

#define MINLZ_SEARCH_PRIME2 40503u
#define MINLZ_SEARCH_PRIME3 506832829u
#define MINLZ_SEARCH_PRIME4 2654435761u
#define MINLZ_SEARCH_PRIME5 UINT64_C(889523592379)
#define MINLZ_SEARCH_PRIME6 UINT64_C(227718039650203)
#define MINLZ_SEARCH_PRIME7 UINT64_C(58295818150454627)
#define MINLZ_SEARCH_PRIME8 UINT64_C(0xcf1bbcdcb7a56463)

static unsigned minlz_search_ceil_log2(size_t n) {
    unsigned log2 = 0;
    size_t value = 1;
    while (value < n && log2 < MINLZ_SEARCH_MAX_LOG2) {
        value <<= 1;
        log2++;
    }
    return log2;
}

static unsigned minlz_popcount8(uint8_t value) {
    value = (uint8_t)(value - ((value >> 1) & 0x55u));
    value = (uint8_t)((value & 0x33u) + ((value >> 2) & 0x33u));
    return (unsigned)((value + (value >> 4)) & 0x0fu);
}

static unsigned minlz_search_prefix_size(const minlz_search_config *config) {
    switch (config->table_type) {
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX:
        return 8u;
    case MINLZ_SEARCH_TABLE_MASK_PREFIX:
        return 32u;
    case MINLZ_SEARCH_TABLE_LONG_PREFIX:
        return 2u + config->long_prefix_len;
    default:
        return 0u;
    }
}

static int minlz_search_config_valid(const minlz_search_config *config) {
    if (!config || config->match_len < 1u || config->match_len > 8u ||
        config->base_table_log2 < MINLZ_SEARCH_MIN_LOG2 ||
        config->base_table_log2 > MINLZ_SEARCH_MAX_LOG2 ||
        config->max_population_pct > 100u ||
        config->max_reduced_population_pct > 100u ||
        config->compression_skip_pct_x100 > 5000u) {
        return 0;
    }
    switch (config->table_type) {
    case MINLZ_SEARCH_TABLE_NO_PREFIX:
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX:
    case MINLZ_SEARCH_TABLE_MASK_PREFIX:
        return config->extras == 0u;
    case MINLZ_SEARCH_TABLE_LONG_PREFIX:
        return config->long_prefix_len >= 1u &&
               config->long_prefix_len <= 256u &&
               (unsigned)config->match_len + config->extras <= 16u;
    default:
        return 0;
    }
}

void minlz_search_config_init(minlz_search_config *config) {
    if (!config) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->match_len = MINLZ_SEARCH_DEFAULT_MATCH_LEN;
    config->table_type = MINLZ_SEARCH_TABLE_NO_PREFIX;
    config->max_population_pct = MINLZ_SEARCH_DEFAULT_MAX_POP;
    config->max_reduced_population_pct = MINLZ_SEARCH_DEFAULT_MAX_REDUCED_POP;
    config->compress_tables = 1u;
    config->compression_skip_pct_x100 =
        MINLZ_SEARCH_DEFAULT_COMPRESS_SKIP_X100;
}

minlz_status minlz_search_config_set_match_len(minlz_search_config *config,
                                               unsigned match_len) {
    if (!config || match_len < 1u || match_len > 8u ||
        match_len + config->extras > 16u) {
        return MINLZ_CORRUPT;
    }
    config->match_len = (uint8_t)match_len;
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_byte_prefix(minlz_search_config *config,
                                                 const uint8_t *prefixes,
                                                 size_t prefix_count) {
    size_t i;
    if (!config || !prefixes || prefix_count == 0u || prefix_count > 256u) {
        return MINLZ_CORRUPT;
    }
    memset(config->prefix_bytes, 0, sizeof(config->prefix_bytes));
    memset(config->prefix_mask, 0, sizeof(config->prefix_mask));
    config->long_prefix_len = 0;
    config->extras = 0;
    if (prefix_count <= 8u) {
        config->table_type = MINLZ_SEARCH_TABLE_BYTE_PREFIX;
        for (i = 0; i < 8u; i++) {
            config->prefix_bytes[i] =
                prefixes[i < prefix_count ? i : prefix_count - 1u];
        }
    } else {
        config->table_type = MINLZ_SEARCH_TABLE_MASK_PREFIX;
        for (i = 0; i < prefix_count; i++) {
            config->prefix_mask[prefixes[i] >> 3] |=
                (uint8_t)(1u << (prefixes[i] & 7u));
        }
    }
    if (!config->max_reduced_population_explicit) {
        config->max_reduced_population_pct =
            MINLZ_SEARCH_PREFIX_MAX_REDUCED_POP;
    }
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_mask_prefix(minlz_search_config *config,
                                                 const uint8_t mask[32]) {
    if (!config || !mask) {
        return MINLZ_CORRUPT;
    }
    config->table_type = MINLZ_SEARCH_TABLE_MASK_PREFIX;
    memcpy(config->prefix_mask, mask, sizeof(config->prefix_mask));
    memset(config->prefix_bytes, 0, sizeof(config->prefix_bytes));
    config->long_prefix_len = 0;
    config->extras = 0;
    if (!config->max_reduced_population_explicit) {
        config->max_reduced_population_pct =
            MINLZ_SEARCH_PREFIX_MAX_REDUCED_POP;
    }
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_long_prefix(minlz_search_config *config,
                                                 const uint8_t *prefix,
                                                 size_t prefix_len,
                                                 unsigned extras) {
    if (!config || !prefix || prefix_len < 1u || prefix_len > 256u ||
        extras > 15u || (unsigned)config->match_len + extras > 16u) {
        return MINLZ_CORRUPT;
    }
    config->table_type = MINLZ_SEARCH_TABLE_LONG_PREFIX;
    memcpy(config->long_prefix, prefix, prefix_len);
    if (prefix_len < sizeof(config->long_prefix)) {
        memset(config->long_prefix + prefix_len, 0,
               sizeof(config->long_prefix) - prefix_len);
    }
    config->long_prefix_len = (uint16_t)prefix_len;
    config->extras = (uint8_t)extras;
    memset(config->prefix_bytes, 0, sizeof(config->prefix_bytes));
    memset(config->prefix_mask, 0, sizeof(config->prefix_mask));
    if (!config->max_reduced_population_explicit) {
        config->max_reduced_population_pct =
            MINLZ_SEARCH_PREFIX_MAX_REDUCED_POP;
    }
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_extras(minlz_search_config *config,
                                            unsigned extras) {
    if (!config ||
        config->table_type != MINLZ_SEARCH_TABLE_LONG_PREFIX ||
        extras > 15u ||
        (unsigned)config->match_len + extras > 16u) {
        return MINLZ_CORRUPT;
    }
    config->extras = (uint8_t)extras;
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_population(minlz_search_config *config,
                                                unsigned max_population_pct,
                                                unsigned max_reduced_population_pct) {
    if (!config || max_population_pct > 100u ||
        max_reduced_population_pct > 100u) {
        return MINLZ_CORRUPT;
    }
    config->max_population_pct = (uint8_t)max_population_pct;
    config->max_reduced_population_pct =
        (uint8_t)max_reduced_population_pct;
    config->max_reduced_population_explicit = 1u;
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_max_population(
    minlz_search_config *config, unsigned max_population_pct) {
    if (!config || max_population_pct > 100u) {
        return MINLZ_CORRUPT;
    }
    config->max_population_pct = (uint8_t)max_population_pct;
    return MINLZ_OK;
}

minlz_status minlz_search_config_set_max_reduced_population(
    minlz_search_config *config,
    unsigned max_reduced_population_pct) {
    if (!config || max_reduced_population_pct > 100u) {
        return MINLZ_CORRUPT;
    }
    config->max_reduced_population_pct =
        (uint8_t)max_reduced_population_pct;
    config->max_reduced_population_explicit = 1u;
    return MINLZ_OK;
}

void minlz_search_config_set_compression(minlz_search_config *config,
                                         int enabled) {
    if (config) {
        config->compress_tables = enabled != 0;
    }
}

minlz_status minlz_search_config_set_compression_skip(
    minlz_search_config *config, double pct) {
    if (!config || pct != pct) {
        return MINLZ_CORRUPT;
    }
    if (pct < 0.0) {
        pct = 0.0;
    } else if (pct > 50.0) {
        pct = 50.0;
    }
    config->compression_skip_pct_x100 =
        (uint16_t)(pct * 100.0);
    config->compress_tables = 1u;
    return MINLZ_OK;
}

void minlz_search_config_set_compression_force(
    minlz_search_config *config, int force) {
    if (config) {
        config->force_compressed = force != 0;
        config->compress_tables = 1u;
    }
}

void minlz_search_config_set_compression_stats_callback(
    minlz_search_config *config,
    minlz_compressed_search_stats_cb cb, void *cb_ctx) {
    if (config) {
        config->compressed_stats_cb = cb;
        config->compressed_stats_ctx = cb_ctx;
    }
}

static size_t minlz_search_append_quoted_byte(
    char *dst, size_t cap, size_t pos, uint8_t value) {
    static const char hex[] = "0123456789abcdef";
    if (value >= 0x20u && value <= 0x7eu &&
        value != '"' && value != '\\') {
        if (pos < cap) {
            dst[pos] = (char)value;
        }
        return pos + 1u;
    }
    if (value == '"' || value == '\\') {
        if (pos < cap) {
            dst[pos] = '\\';
        }
        if (pos + 1u < cap) {
            dst[pos + 1u] = (char)value;
        }
        return pos + 2u;
    }
    if (pos < cap) {
        dst[pos] = '\\';
    }
    if (pos + 1u < cap) {
        dst[pos + 1u] = 'x';
    }
    if (pos + 2u < cap) {
        dst[pos + 2u] = hex[value >> 4];
    }
    if (pos + 3u < cap) {
        dst[pos + 3u] = hex[value & 15u];
    }
    return pos + 4u;
}

size_t minlz_search_config_snprint(
    char *dst, size_t dst_cap,
    const minlz_search_config *config) {
    char prefix[1100];
    size_t prefix_len = 0u;
    int written;
    size_t i;
    if (!config) {
        if (dst && dst_cap) {
            dst[0] = '\0';
        }
        return 0u;
    }
    switch (config->table_type) {
    case MINLZ_SEARCH_TABLE_NO_PREFIX:
        memcpy(prefix, "no-prefix", 10u);
        prefix_len = 9u;
        break;
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX: {
        uint8_t seen[256];
        memset(seen, 0, sizeof(seen));
        memcpy(prefix, "byte-prefix=\"", 13u);
        prefix_len = 13u;
        for (i = 0u; i < sizeof(config->prefix_bytes); i++) {
            uint8_t value = config->prefix_bytes[i];
            if (!seen[value]) {
                seen[value] = 1u;
                prefix_len = minlz_search_append_quoted_byte(
                    prefix, sizeof(prefix), prefix_len, value);
            }
        }
        if (prefix_len < sizeof(prefix)) {
            prefix[prefix_len] = '"';
        }
        prefix_len++;
        break;
    }
    case MINLZ_SEARCH_TABLE_MASK_PREFIX: {
        unsigned count = 0u;
        for (i = 0u; i < 256u; i++) {
            if ((config->prefix_mask[i >> 3] &
                 (uint8_t)(1u << (i & 7u))) != 0u) {
                count++;
            }
        }
        written = snprintf(prefix, sizeof(prefix),
                           "mask-prefix (%u bytes)", count);
        prefix_len =
            written > 0 ? (size_t)written : 0u;
        break;
    }
    case MINLZ_SEARCH_TABLE_LONG_PREFIX:
        memcpy(prefix, "long-prefix=\"", 13u);
        prefix_len = 13u;
        for (i = 0u; i < config->long_prefix_len; i++) {
            prefix_len = minlz_search_append_quoted_byte(
                prefix, sizeof(prefix), prefix_len,
                config->long_prefix[i]);
        }
        if (prefix_len < sizeof(prefix)) {
            prefix[prefix_len] = '"';
        }
        prefix_len++;
        if (config->extras != 0u) {
            written = snprintf(
                prefix + (prefix_len < sizeof(prefix)
                              ? prefix_len
                              : sizeof(prefix)),
                prefix_len < sizeof(prefix)
                    ? sizeof(prefix) - prefix_len
                    : 0u,
                " extras=%u", config->extras);
            if (written > 0) {
                prefix_len += (size_t)written;
            }
        }
        break;
    default:
        written = snprintf(prefix, sizeof(prefix),
                           "type=%u", config->table_type);
        prefix_len =
            written > 0 ? (size_t)written : 0u;
        break;
    }
    if (prefix_len >= sizeof(prefix)) {
        prefix_len = sizeof(prefix) - 1u;
    }
    prefix[prefix_len] = '\0';
    written = snprintf(
        dst, dst_cap,
        "matchLen=%u baseTableSize=%u (%" PRIu64 " entries) %s",
        config->match_len, config->base_table_log2,
        config->base_table_log2 < 64u
            ? (UINT64_C(1) << config->base_table_log2)
            : 0u,
        prefix);
    return written > 0 ? (size_t)written : 0u;
}

uint32_t minlz_search_hash(uint64_t value, unsigned table_log2,
                           unsigned match_len) {
    if (table_log2 < MINLZ_SEARCH_MIN_LOG2 ||
        table_log2 > MINLZ_SEARCH_MAX_LOG2) {
        return 0;
    }
    switch (match_len) {
    case 1:
        return (uint32_t)(value & 0xffu);
    case 2:
        if (table_log2 >= 16u) {
            return (uint32_t)(value & 0xffffu);
        }
        return ((uint32_t)(value << 16) * MINLZ_SEARCH_PRIME2) >>
               (32u - table_log2);
    case 3:
        return ((uint32_t)(value << 8) * MINLZ_SEARCH_PRIME3) >>
               (32u - table_log2);
    case 4:
        return ((uint32_t)value * MINLZ_SEARCH_PRIME4) >>
               (32u - table_log2);
    case 5:
        return (uint32_t)(((value << 24) * MINLZ_SEARCH_PRIME5) >>
                          (64u - table_log2));
    case 6:
        return (uint32_t)(((value << 16) * MINLZ_SEARCH_PRIME6) >>
                          (64u - table_log2));
    case 7:
        return (uint32_t)(((value << 8) * MINLZ_SEARCH_PRIME7) >>
                          (64u - table_log2));
    case 8:
        return (uint32_t)((value * MINLZ_SEARCH_PRIME8) >>
                          (64u - table_log2));
    default:
        return 0;
    }
}

minlz_status minlz_search_config_resolve(minlz_search_config *dst,
                                         const minlz_search_config *src,
                                         size_t block_size) {
    unsigned table_log2;
    if (!dst || block_size < MINLZ_MIN_BLOCK_SIZE ||
        block_size > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_CORRUPT;
    }
    if (src) {
        *dst = *src;
    } else {
        minlz_search_config_init(dst);
    }
    if (!dst->max_reduced_population_explicit &&
        dst->table_type != MINLZ_SEARCH_TABLE_NO_PREFIX) {
        dst->max_reduced_population_pct =
            MINLZ_SEARCH_PREFIX_MAX_REDUCED_POP;
    }
    if (dst->base_table_log2 == 0u) {
        table_log2 = minlz_search_ceil_log2(block_size);
        if (table_log2 < MINLZ_SEARCH_MIN_LOG2) {
            table_log2 = MINLZ_SEARCH_MIN_LOG2;
        }
        if (table_log2 > MINLZ_SEARCH_MAX_LOG2) {
            table_log2 = MINLZ_SEARCH_MAX_LOG2;
        }
        dst->base_table_log2 = (uint8_t)table_log2;
    }
    return minlz_search_config_valid(dst) ? MINLZ_OK : MINLZ_CORRUPT;
}

size_t minlz_search_overlap_bytes(const minlz_search_config *config) {
    size_t n;
    if (!config) {
        return 0;
    }
    n = (size_t)config->match_len + config->extras;
    if (config->table_type == MINLZ_SEARCH_TABLE_LONG_PREFIX &&
        config->long_prefix_len > 0u) {
        n += config->long_prefix_len - 1u;
    }
    return n;
}

static size_t minlz_search_append_config(uint8_t *dst,
                                         const minlz_search_config *config) {
    size_t n = 3u;
    dst[0] = config->table_type;
    dst[1] = config->match_len;
    dst[2] = config->base_table_log2;
    switch (config->table_type) {
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX:
        memcpy(dst + n, config->prefix_bytes, 8u);
        n += 8u;
        break;
    case MINLZ_SEARCH_TABLE_MASK_PREFIX:
        memcpy(dst + n, config->prefix_mask, 32u);
        n += 32u;
        break;
    case MINLZ_SEARCH_TABLE_LONG_PREFIX:
        dst[n++] = (uint8_t)(config->long_prefix_len - 1u);
        dst[n++] = config->extras;
        memcpy(dst + n, config->long_prefix, config->long_prefix_len);
        n += config->long_prefix_len;
        break;
    default:
        break;
    }
    return n;
}

minlz_status minlz_search_make_info_chunk(const minlz_search_config *config,
                                          uint8_t **dst, size_t *dst_len) {
    size_t payload_len;
    uint8_t *out;
    if (!dst || !dst_len || !minlz_search_config_valid(config)) {
        return MINLZ_CORRUPT;
    }
    *dst = NULL;
    *dst_len = 0;
    payload_len = 3u + minlz_search_prefix_size(config);
    out = (uint8_t *)malloc(4u + payload_len);
    if (!out) {
        return MINLZ_IO;
    }
    out[0] = MINLZ_CHUNK_SEARCH_INFO;
    out[1] = (uint8_t)payload_len;
    out[2] = (uint8_t)(payload_len >> 8);
    out[3] = (uint8_t)(payload_len >> 16);
    if (minlz_search_append_config(out + 4u, config) != payload_len) {
        free(out);
        return MINLZ_CORRUPT;
    }
    *dst = out;
    *dst_len = 4u + payload_len;
    return MINLZ_OK;
}

static uint8_t minlz_search_combined_byte(const uint8_t *block,
                                          size_t block_len,
                                          const uint8_t *overlap,
                                          size_t overlap_len,
                                          size_t pos) {
    if (pos < block_len) {
        return block[pos];
    }
    pos -= block_len;
    return pos < overlap_len ? overlap[pos] : 0u;
}

static uint64_t minlz_search_combined_u64(const uint8_t *block,
                                         size_t block_len,
                                         const uint8_t *overlap,
                                         size_t overlap_len,
                                         size_t pos, size_t available) {
    uint64_t value = 0;
    size_t i;
    if (available > 8u) {
        available = 8u;
    }
    for (i = 0; i < available; i++) {
        value |= (uint64_t)minlz_search_combined_byte(
                     block, block_len, overlap, overlap_len, pos + i)
                 << (8u * i);
    }
    return value;
}

static int minlz_search_combined_equal(const uint8_t *block,
                                       size_t block_len,
                                       const uint8_t *overlap,
                                       size_t overlap_len,
                                       size_t pos,
                                       const uint8_t *want,
                                       size_t want_len) {
    size_t i;
    for (i = 0; i < want_len; i++) {
        if (minlz_search_combined_byte(block, block_len, overlap, overlap_len,
                                       pos + i) != want[i]) {
            return 0;
        }
    }
    return 1;
}

static void minlz_search_set_bit(uint8_t *table, uint32_t hash) {
    table[hash >> 3] |= (uint8_t)(1u << (hash & 7u));
}

static int minlz_search_prefix_byte_matches(
    const minlz_search_config *config, uint8_t value) {
    size_t i;
    if (config->table_type == MINLZ_SEARCH_TABLE_BYTE_PREFIX) {
        for (i = 0; i < 8u; i++) {
            if (config->prefix_bytes[i] == value) {
                return 1;
            }
        }
        return 0;
    }
    return (config->prefix_mask[value >> 3] &
            (uint8_t)(1u << (value & 7u))) != 0;
}

static minlz_status minlz_search_build_table(
    const minlz_search_config *config,
    const uint8_t *block, size_t block_len,
    const uint8_t *overlap, size_t overlap_len,
    uint8_t **table_out, size_t *table_len_out,
    uint8_t *reductions_out) {
    size_t total = block_len + overlap_len;
    size_t table_len = (size_t)1u << (config->base_table_log2 - 3u);
    uint8_t *table;
    size_t pos;
    unsigned set_bits = 0;
    unsigned total_bits;
    uint8_t reductions = 0;
    unsigned ml = config->match_len;

    if ((!block && block_len != 0u) || (!overlap && overlap_len != 0u) ||
        !table_out || !table_len_out || !reductions_out) {
        return MINLZ_CORRUPT;
    }
    table = (uint8_t *)calloc(table_len ? table_len : 1u, 1u);
    if (!table) {
        return MINLZ_IO;
    }

    if (config->table_type == MINLZ_SEARCH_TABLE_NO_PREFIX) {
        for (pos = 0; pos < block_len && pos + ml <= total; pos++) {
            uint64_t value = minlz_search_combined_u64(
                block, block_len, overlap, overlap_len, pos, total - pos);
            minlz_search_set_bit(
                table, minlz_search_hash(value, config->base_table_log2, ml));
        }
    } else if (config->table_type == MINLZ_SEARCH_TABLE_BYTE_PREFIX ||
               config->table_type == MINLZ_SEARCH_TABLE_MASK_PREFIX) {
        for (pos = 1u; pos <= block_len && pos + ml <= total; pos++) {
            uint64_t value;
            if (!minlz_search_prefix_byte_matches(
                    config, minlz_search_combined_byte(
                                block, block_len, overlap, overlap_len, pos - 1u))) {
                continue;
            }
            value = minlz_search_combined_u64(
                block, block_len, overlap, overlap_len, pos, total - pos);
            minlz_search_set_bit(
                table, minlz_search_hash(value, config->base_table_log2, ml));
        }
    } else {
        size_t prefix_len = config->long_prefix_len;
        unsigned extras = config->extras;
        for (pos = 0; pos < block_len; pos++) {
            size_t value_pos;
            unsigned j;
            if (pos + prefix_len + ml + extras > total ||
                !minlz_search_combined_equal(
                    block, block_len, overlap, overlap_len, pos,
                    config->long_prefix, prefix_len)) {
                continue;
            }
            value_pos = pos + prefix_len;
            for (j = 0; j <= extras; j++) {
                uint64_t value = minlz_search_combined_u64(
                    block, block_len, overlap, overlap_len, value_pos + j,
                    total - value_pos - j);
                minlz_search_set_bit(
                    table,
                    minlz_search_hash(value, config->base_table_log2, ml));
            }
        }
    }

    for (pos = 0; pos < table_len; pos++) {
        set_bits += minlz_popcount8(table[pos]);
    }
    total_bits = (unsigned)(table_len * 8u);
    if (total_bits != 0u &&
        set_bits * 100u > total_bits * config->max_population_pct) {
        free(table);
        return MINLZ_UNSUPPORTED;
    }

    while (table_len / 2u >= 32u) {
        size_t half = table_len / 2u;
        unsigned reduced_bits = 0;
        for (pos = 0; pos < half; pos++) {
            reduced_bits += minlz_popcount8(
                (uint8_t)(table[pos] | table[pos + half]));
        }
        if (set_bits != 0u &&
            reduced_bits * 100u >
                (unsigned)(half * 8u) *
                    config->max_reduced_population_pct) {
            break;
        }
        for (pos = 0; pos < half; pos++) {
            table[pos] |= table[pos + half];
        }
        table_len = half;
        set_bits = reduced_bits;
        reductions++;
    }

    *table_out = table;
    *table_len_out = table_len;
    *reductions_out = reductions;
    return MINLZ_OK;
}

typedef struct minlz_search_compressed_part {
    uint8_t kind;
    uint8_t table_index;
    size_t sparse_len;
    size_t wire_len;
    size_t effective_len;
} minlz_search_compressed_part;

enum {
    MINLZ_SEARCH_COMPRESSED_OWN = 19,
    MINLZ_SEARCH_COMPRESSED_GLOBAL = 20
};

static size_t minlz_search_uvarint_len(size_t value) {
    size_t n = 1u;
    while (value >= 0x80u) {
        value >>= 7;
        n++;
    }
    return n;
}

static size_t minlz_search_sparse_measure(
    const uint8_t *bitmap, size_t bitmap_len) {
    size_t bit;
    size_t previous = 0;
    size_t encoded = 0;
    int have_previous = 0;
    for (bit = 0; bit < bitmap_len * 8u; bit++) {
        size_t gap;
        if ((bitmap[bit >> 3] &
             (uint8_t)(1u << (bit & 7u))) == 0u) {
            continue;
        }
        gap = have_previous ? bit - previous - 1u : bit;
        encoded += gap / 255u + 1u;
        previous = bit;
        have_previous = 1;
    }
    return encoded;
}

static uint8_t *minlz_search_sparse_emit(
    uint8_t *dst, const uint8_t *bitmap, size_t bitmap_len) {
    size_t bit;
    size_t previous = 0;
    int have_previous = 0;
    for (bit = 0; bit < bitmap_len * 8u; bit++) {
        size_t gap;
        if ((bitmap[bit >> 3] &
             (uint8_t)(1u << (bit & 7u))) == 0u) {
            continue;
        }
        gap = have_previous ? bit - previous - 1u : bit;
        while (gap >= 255u) {
            *dst++ = 255u;
            gap -= 255u;
        }
        *dst++ = (uint8_t)gap;
        previous = bit;
        have_previous = 1;
    }
    return dst;
}

static int minlz_search_block_is_rle(
    const uint8_t *src, size_t len, uint8_t *value) {
    size_t i;
    uint8_t first;
    if (len == 0u) {
        return 0;
    }
    first = src[0];
    for (i = 1u; i < len; i++) {
        if (src[i] != first) {
            return 0;
        }
    }
    *value = first;
    return 1;
}

static void minlz_search_emit_compressed_stats(
    const minlz_search_config *config,
    const minlz_compressed_search_stats *stats) {
    if (config && config->compressed_stats_cb) {
        config->compressed_stats_cb(
            config->compressed_stats_ctx, stats);
    }
}

static minlz_status minlz_search_make_compressed_table_chunk(
    const minlz_search_config *config, uint8_t reductions,
    const uint8_t *table, size_t table_len,
    uint8_t **dst, size_t *dst_len) {
    minlz_search_compressed_part no_global[16];
    minlz_search_compressed_part with_global[16];
    minlz_search_compressed_part *parts = no_global;
    minlz_huff0_candidate own[16];
    minlz_huff0_shared shared;
    size_t block_log2;
    size_t block_size;
    size_t block_count;
    size_t config_len = 3u + minlz_search_prefix_size(config);
    size_t payload_len;
    size_t uncompressed_len =
        config_len + 1u + 4u + table_len;
    size_t i;
    size_t total_no_global = 0u;
    size_t total_with_global = 0u;
    size_t global_users = 0u;
    size_t table_count = 0u;
    size_t set_bits = 0u;
    int use_global = 0;
    uint8_t *out;
    size_t n;
    minlz_compressed_search_stats report;
    minlz_status st = MINLZ_OK;

    memset(own, 0, sizeof(own));
    memset(&shared, 0, sizeof(shared));
    memset(no_global, 0, sizeof(no_global));
    memset(with_global, 0, sizeof(with_global));
    memset(&report, 0, sizeof(report));
    report.bitmap_bytes = table_len;
    report.reductions = reductions;
    report.total_bits = table_len * 8u;
    report.chunk_0x45_size =
        MINLZ_CHUNK_HEADER_SIZE + uncompressed_len;
    for (i = 0; i < table_len; i++) {
        set_bits += minlz_popcount8(table[i]);
    }
    report.set_bits = set_bits;
    if (table_len < 32u) {
        report.skipped_size = 1;
        minlz_search_emit_compressed_stats(config, &report);
        return MINLZ_UNSUPPORTED;
    }
    if ((uint64_t)(set_bits * 2u > table_len * 8u
                       ? set_bits * 2u - table_len * 8u
                       : table_len * 8u - set_bits * 2u) *
            UINT64_C(5000) <
        (uint64_t)config->compression_skip_pct_x100 *
            (uint64_t)table_len * UINT64_C(8)) {
        report.skipped_band = 1;
        minlz_search_emit_compressed_stats(config, &report);
        return MINLZ_UNSUPPORTED;
    }
    if (table_len <= (32u << 10)) {
        block_log2 = minlz_search_ceil_log2(table_len);
    } else if (table_len <= (512u << 10)) {
        block_log2 = 15u;
    } else {
        block_log2 = 16u;
    }
    block_size = (size_t)1u << block_log2;
    if (table_len % block_size != 0u) {
        return MINLZ_UNSUPPORTED;
    }
    block_count = table_len / block_size;
    if (block_count == 0u || block_count > 16u) {
        minlz_search_emit_compressed_stats(config, &report);
        return MINLZ_UNSUPPORTED;
    }
    report.sub_block_size = block_size;
    report.sub_blocks = block_count;

    for (i = 0; i < block_count; i++) {
        const uint8_t *block = table + i * block_size;
        uint8_t rle_value;
        size_t sparse_len =
            minlz_search_sparse_measure(block, block_size);
        size_t sparse_wire =
            1u + minlz_search_uvarint_len(sparse_len) + sparse_len;
        size_t raw_wire = 1u + block_size;
        minlz_search_compressed_part pick;

        st = minlz_huff0_encode_own(
            block, block_size, &own[i]);
        if (st != MINLZ_OK && st != MINLZ_UNSUPPORTED) {
            goto cleanup;
        }
        memset(&pick, 0, sizeof(pick));
        pick.kind = 16u;
        pick.wire_len = raw_wire;
        pick.effective_len = raw_wire;
        pick.sparse_len = sparse_len;
        if (minlz_search_block_is_rle(
                block, block_size, &rle_value) &&
            2u < pick.effective_len) {
            pick.kind = 17u;
            pick.wire_len = 2u;
            pick.effective_len = 2u;
        }
        if (sparse_wire < pick.effective_len) {
            pick.kind = 18u;
            pick.wire_len = sparse_wire;
            pick.effective_len = sparse_wire;
        }
        if (own[i].data) {
            size_t own_wire =
                1u + minlz_search_uvarint_len(own[i].data_len) +
                own[i].data_len;
            size_t own_effective =
                own_wire + own[i].table_len + 8u;
            if (own_effective < pick.effective_len) {
                pick.kind = MINLZ_SEARCH_COMPRESSED_OWN;
                pick.wire_len = own_wire;
                pick.effective_len = own_effective;
            }
        }
        no_global[i] = pick;
        total_no_global += pick.wire_len;
        if (pick.kind == MINLZ_SEARCH_COMPRESSED_OWN) {
            total_no_global += own[i].table_len;
        }
    }

    st = block_count >= 2u
             ? minlz_huff0_encode_shared(
                   table, block_size, block_count, own, &shared)
             : MINLZ_UNSUPPORTED;
    if (st == MINLZ_OK) {
        total_with_global = shared.table_len;
        for (i = 0; i < block_count; i++) {
            minlz_search_compressed_part pick = no_global[i];
            if (shared.valid[i]) {
                size_t global_wire =
                    1u +
                    minlz_search_uvarint_len(shared.data_len[i]) +
                    shared.data_len[i];
                if (global_wire <= pick.effective_len) {
                    pick.kind = MINLZ_SEARCH_COMPRESSED_GLOBAL;
                    pick.wire_len = global_wire;
                    pick.effective_len = global_wire;
                    global_users++;
                }
            }
            with_global[i] = pick;
            total_with_global += pick.wire_len;
            if (pick.kind == MINLZ_SEARCH_COMPRESSED_OWN) {
                total_with_global += own[i].table_len;
            }
        }
        if (global_users >= 2u &&
            total_with_global < total_no_global) {
            parts = with_global;
            use_global = 1;
        }
    } else if (st != MINLZ_UNSUPPORTED) {
        goto cleanup;
    }
    st = MINLZ_OK;

    table_count = use_global ? 1u : 0u;
    for (i = 0; i < block_count; i++) {
        if (parts[i].kind == MINLZ_SEARCH_COMPRESSED_GLOBAL) {
            parts[i].table_index = 0u;
        } else if (parts[i].kind ==
                   MINLZ_SEARCH_COMPRESSED_OWN) {
            if (table_count >= 16u) {
                st = MINLZ_UNSUPPORTED;
                goto cleanup;
            }
            parts[i].table_index = (uint8_t)table_count++;
        }
    }

    payload_len = config_len + 1u + 4u + 2u;
    if (use_global) {
        payload_len += shared.table_len;
    }
    for (i = 0; i < block_count; i++) {
        payload_len += parts[i].wire_len;
        if (parts[i].kind == MINLZ_SEARCH_COMPRESSED_OWN) {
            payload_len += own[i].table_len;
        }
    }
    report.tables = table_count;
    report.chunk_0x46_size =
        MINLZ_CHUNK_HEADER_SIZE + payload_len;
    if (!config->force_compressed &&
        payload_len >= uncompressed_len) {
        st = MINLZ_UNSUPPORTED;
        goto cleanup;
    }
    if (payload_len > MINLZ_MAX_USER_CHUNK_SIZE) {
        st = MINLZ_TOO_LARGE;
        goto cleanup;
    }
    out = (uint8_t *)malloc(MINLZ_CHUNK_HEADER_SIZE + payload_len);
    if (!out) {
        st = MINLZ_IO;
        goto cleanup;
    }
    out[0] = MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED;
    out[1] = (uint8_t)payload_len;
    out[2] = (uint8_t)(payload_len >> 8);
    out[3] = (uint8_t)(payload_len >> 16);
    n = minlz_search_append_config(
        out + MINLZ_CHUNK_HEADER_SIZE, config);
    out[MINLZ_CHUNK_HEADER_SIZE + n++] = reductions;
    minlz_store32(out + MINLZ_CHUNK_HEADER_SIZE + n,
                  minlz_crc(table, table_len));
    n += 4u;
    out[MINLZ_CHUNK_HEADER_SIZE + n++] = (uint8_t)block_log2;
    out[MINLZ_CHUNK_HEADER_SIZE + n++] = (uint8_t)table_count;
    if (use_global) {
        memcpy(out + MINLZ_CHUNK_HEADER_SIZE + n,
               shared.table, shared.table_len);
        n += shared.table_len;
    }
    for (i = 0; i < block_count; i++) {
        if (parts[i].kind == MINLZ_SEARCH_COMPRESSED_OWN) {
            memcpy(out + MINLZ_CHUNK_HEADER_SIZE + n,
                   own[i].table, own[i].table_len);
            n += own[i].table_len;
        }
    }
    for (i = 0; i < block_count; i++) {
        const uint8_t *block = table + i * block_size;
        out[MINLZ_CHUNK_HEADER_SIZE + n++] =
            parts[i].kind == MINLZ_SEARCH_COMPRESSED_OWN ||
                    parts[i].kind == MINLZ_SEARCH_COMPRESSED_GLOBAL
                ? parts[i].table_index
                : parts[i].kind;
        if (parts[i].kind == MINLZ_SEARCH_COMPRESSED_OWN) {
            n += minlz_put_uvarint(
                out + MINLZ_CHUNK_HEADER_SIZE + n,
                own[i].data_len);
            memcpy(out + MINLZ_CHUNK_HEADER_SIZE + n,
                   own[i].data, own[i].data_len);
            n += own[i].data_len;
        } else if (parts[i].kind ==
                   MINLZ_SEARCH_COMPRESSED_GLOBAL) {
            n += minlz_put_uvarint(
                out + MINLZ_CHUNK_HEADER_SIZE + n,
                shared.data_len[i]);
            memcpy(out + MINLZ_CHUNK_HEADER_SIZE + n,
                   shared.data[i], shared.data_len[i]);
            n += shared.data_len[i];
        } else if (parts[i].kind == 16u) {
            memcpy(out + MINLZ_CHUNK_HEADER_SIZE + n,
                   block, block_size);
            n += block_size;
        } else if (parts[i].kind == 17u) {
            out[MINLZ_CHUNK_HEADER_SIZE + n++] = block[0];
        } else {
            n += minlz_put_uvarint(
                out + MINLZ_CHUNK_HEADER_SIZE + n,
                parts[i].sparse_len);
            n = (size_t)(minlz_search_sparse_emit(
                    out + MINLZ_CHUNK_HEADER_SIZE + n,
                    block, block_size) -
                (out + MINLZ_CHUNK_HEADER_SIZE));
        }
    }
    if (n != payload_len) {
        free(out);
        st = MINLZ_CORRUPT;
        goto cleanup;
    }
    *dst = out;
    *dst_len = MINLZ_CHUNK_HEADER_SIZE + payload_len;
    report.emitted_0x46 = 1;
    st = MINLZ_OK;

cleanup:
    for (i = 0; i < block_count; i++) {
        switch (parts[i].kind) {
        case MINLZ_SEARCH_COMPRESSED_OWN:
            report.blocks_own_table++;
            report.bytes_own_payload +=
                parts[i].wire_len - 1u;
            report.bytes_own_tables += own[i].table_len;
            break;
        case MINLZ_SEARCH_COMPRESSED_GLOBAL:
            report.blocks_global_table++;
            report.bytes_global_payload +=
                parts[i].wire_len - 1u;
            break;
        case 16u:
            report.blocks_raw++;
            report.bytes_raw_payload += block_size;
            break;
        case 17u:
            report.blocks_rle++;
            report.bytes_rle_payload++;
            break;
        case 18u:
            report.blocks_sparse++;
            report.bytes_sparse_payload +=
                parts[i].wire_len - 1u;
            break;
        default:
            break;
        }
    }
    if (use_global) {
        report.bytes_global_table = shared.table_len;
    }
    minlz_search_emit_compressed_stats(config, &report);
    for (i = 0; i < block_count; i++) {
        minlz_huff0_candidate_destroy(&own[i]);
    }
    minlz_huff0_shared_destroy(&shared);
    return st;
}

minlz_status minlz_search_make_table_chunk(const minlz_search_config *config,
                                           const uint8_t *block,
                                           size_t block_len,
                                           const uint8_t *overlap,
                                           size_t overlap_len,
                                           uint8_t **dst,
                                           size_t *dst_len) {
    uint8_t *table = NULL;
    size_t table_len = 0;
    uint8_t reductions = 0;
    size_t prefix_len;
    size_t payload_len;
    uint8_t *out;
    size_t n;
    minlz_status st;
    if (!dst || !dst_len || !minlz_search_config_valid(config)) {
        return MINLZ_CORRUPT;
    }
    *dst = NULL;
    *dst_len = 0;
    st = minlz_search_build_table(config, block, block_len, overlap,
                                  overlap_len, &table, &table_len,
                                  &reductions);
    if (st != MINLZ_OK) {
        return st;
    }
    if (config->compress_tables) {
        st = minlz_search_make_compressed_table_chunk(
            config, reductions, table, table_len, dst, dst_len);
        if (st == MINLZ_OK) {
            free(table);
            return MINLZ_OK;
        }
        if (st != MINLZ_UNSUPPORTED) {
            free(table);
            return st;
        }
    }
    prefix_len = minlz_search_prefix_size(config);
    payload_len = 3u + prefix_len + 1u + 4u + table_len;
    if (payload_len > MINLZ_MAX_USER_CHUNK_SIZE) {
        free(table);
        return MINLZ_TOO_LARGE;
    }
    out = (uint8_t *)malloc(4u + payload_len);
    if (!out) {
        free(table);
        return MINLZ_IO;
    }
    out[0] = MINLZ_CHUNK_SEARCH_TABLE;
    out[1] = (uint8_t)payload_len;
    out[2] = (uint8_t)(payload_len >> 8);
    out[3] = (uint8_t)(payload_len >> 16);
    n = minlz_search_append_config(out + 4u, config);
    out[4u + n++] = reductions;
    minlz_store32(out + 4u + n, minlz_crc(table, table_len));
    n += 4u;
    memcpy(out + 4u + n, table, table_len);
    n += table_len;
    free(table);
    if (n != payload_len) {
        free(out);
        return MINLZ_CORRUPT;
    }
    *dst = out;
    *dst_len = 4u + payload_len;
    return MINLZ_OK;
}

minlz_status minlz_search_parse_info(const uint8_t *payload,
                                     size_t payload_len,
                                     minlz_search_config *config,
                                     size_t *used) {
    size_t n = 3u;
    if (!payload || !config || payload_len < 3u) {
        return MINLZ_CORRUPT;
    }
    minlz_search_config_init(config);
    config->table_type = payload[0];
    config->match_len = payload[1];
    config->base_table_log2 = payload[2];
    switch (config->table_type) {
    case MINLZ_SEARCH_TABLE_NO_PREFIX:
        break;
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX:
        if (payload_len - n < 8u) {
            return MINLZ_CORRUPT;
        }
        memcpy(config->prefix_bytes, payload + n, 8u);
        n += 8u;
        break;
    case MINLZ_SEARCH_TABLE_MASK_PREFIX:
        if (payload_len - n < 32u) {
            return MINLZ_CORRUPT;
        }
        memcpy(config->prefix_mask, payload + n, 32u);
        n += 32u;
        break;
    case MINLZ_SEARCH_TABLE_LONG_PREFIX:
        if (payload_len - n < 2u) {
            return MINLZ_CORRUPT;
        }
        config->long_prefix_len = (uint16_t)payload[n++] + 1u;
        config->extras = payload[n++];
        if (payload_len - n < config->long_prefix_len) {
            return MINLZ_CORRUPT;
        }
        memcpy(config->long_prefix, payload + n,
               config->long_prefix_len);
        n += config->long_prefix_len;
        break;
    default:
        return MINLZ_UNSUPPORTED;
    }
    if (!minlz_search_config_valid(config)) {
        return MINLZ_CORRUPT;
    }
    if (used) {
        *used = n;
    }
    return MINLZ_OK;
}

minlz_status minlz_search_parse_table(const uint8_t *payload,
                                      size_t payload_len,
                                      int ignore_crc,
                                      minlz_search_table_view *view) {
    size_t n = 0;
    size_t expected;
    uint32_t stored_crc;
    minlz_status st;
    if (!view) {
        return MINLZ_CORRUPT;
    }
    memset(view, 0, sizeof(*view));
    st = minlz_search_parse_info(payload, payload_len, &view->config, &n);
    if (st != MINLZ_OK || n >= payload_len) {
        return st == MINLZ_OK ? MINLZ_CORRUPT : st;
    }
    view->reductions = payload[n++];
    if (view->reductions > view->config.base_table_log2 -
                               MINLZ_SEARCH_MIN_LOG2 ||
        payload_len - n < 4u) {
        return MINLZ_CORRUPT;
    }
    stored_crc = minlz_load32(payload + n);
    n += 4u;
    expected = (size_t)1u <<
               (view->config.base_table_log2 - view->reductions - 3u);
    if (payload_len - n != expected) {
        return MINLZ_CORRUPT;
    }
    view->table = payload + n;
    view->table_len = expected;
    if (!ignore_crc &&
        minlz_crc(view->table, view->table_len) != stored_crc) {
        return MINLZ_CRC;
    }
    return MINLZ_OK;
}

static minlz_status minlz_search_sparse_decode(
    uint8_t *dst, size_t dst_len,
    const uint8_t *src, size_t src_len) {
    size_t bit_pos = 0;
    size_t gap = 0;
    size_t i;
    if (dst_len > SIZE_MAX / 8u) {
        return MINLZ_TOO_LARGE;
    }
    for (i = 0; i < src_len; i++) {
        if (gap > SIZE_MAX - src[i]) {
            return MINLZ_TOO_LARGE;
        }
        gap += src[i];
        if (src[i] == 255u) {
            continue;
        }
        if (bit_pos > SIZE_MAX - gap) {
            return MINLZ_TOO_LARGE;
        }
        bit_pos += gap;
        if (bit_pos >= dst_len * 8u) {
            return MINLZ_CORRUPT;
        }
        dst[bit_pos >> 3] |=
            (uint8_t)(1u << (bit_pos & 7u));
        bit_pos++;
        gap = 0u;
    }
    return gap == 0u ? MINLZ_OK : MINLZ_CORRUPT;
}

minlz_status minlz_search_parse_table_compressed(
    const uint8_t *payload, size_t payload_len, int ignore_crc,
    uint8_t **table_storage, minlz_search_table_view *view) {
    size_t n = 0;
    size_t table_len;
    size_t block_size;
    size_t block_count;
    size_t i;
    uint32_t expected_crc;
    uint8_t block_log2;
    uint8_t table_count;
    uint8_t *table;
    minlz_huff0_decoder *huff0 = NULL;
    minlz_status st;
    if (!payload || !table_storage || !view) {
        return MINLZ_CORRUPT;
    }
    *table_storage = NULL;
    memset(view, 0, sizeof(*view));
    st = minlz_search_parse_info(
        payload, payload_len, &view->config, &n);
    if (st != MINLZ_OK || payload_len - n < 7u) {
        return st == MINLZ_OK ? MINLZ_CORRUPT : st;
    }
    view->reductions = payload[n++];
    if (view->reductions >
        view->config.base_table_log2 - MINLZ_SEARCH_MIN_LOG2) {
        return MINLZ_CORRUPT;
    }
    expected_crc = minlz_load32(payload + n);
    n += 4u;
    block_log2 = payload[n++];
    table_count = payload[n++];
    if (block_log2 < 5u || block_log2 > 17u ||
        table_count > 16u) {
        return MINLZ_CORRUPT;
    }
    if (table_count != 0u) {
        st = minlz_huff0_decoder_create(&huff0);
        if (st != MINLZ_OK) {
            return st;
        }
        for (i = 0; i < table_count; i++) {
            size_t used = 0u;
            st = minlz_huff0_decoder_read_table(
                huff0, (unsigned)i, payload + n,
                payload_len - n, &used);
            if (st != MINLZ_OK) {
                minlz_huff0_decoder_destroy(huff0);
                return st;
            }
            n += used;
            view->compressed_bytes_table_headers += used;
        }
    }
    table_len = (size_t)1u <<
                (view->config.base_table_log2 -
                 view->reductions - 3u);
    block_size = (size_t)1u << block_log2;
    if (table_len % block_size != 0u) {
        minlz_huff0_decoder_destroy(huff0);
        return MINLZ_CORRUPT;
    }
    block_count = table_len / block_size;
    if (block_count == 0u || block_count > 16u) {
        minlz_huff0_decoder_destroy(huff0);
        return MINLZ_CORRUPT;
    }
    view->compressed_blocks_total = block_count;
    view->compressed_tables = table_count;
    table = (uint8_t *)calloc(table_len ? table_len : 1u, 1u);
    if (!table) {
        minlz_huff0_decoder_destroy(huff0);
        return MINLZ_IO;
    }
    for (i = 0; i < block_count; i++) {
        uint8_t disposition;
        uint8_t *block = table + i * block_size;
        if (n >= payload_len) {
            free(table);
            minlz_huff0_decoder_destroy(huff0);
            return MINLZ_CORRUPT;
        }
        disposition = payload[n++];
        if (disposition <= 15u) {
            size_t compressed_len = 0u;
            size_t used = 0u;
            if (disposition >= table_count) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return MINLZ_CORRUPT;
            }
            st = minlz_read_uvarint(
                payload + n, payload_len - n,
                &compressed_len, &used);
            if (st != MINLZ_OK ||
                compressed_len > payload_len - n - used) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return st == MINLZ_OK ? MINLZ_CORRUPT : st;
            }
            n += used;
            view->compressed_bytes_tabled += used;
            st = minlz_huff0_decoder_decode(
                huff0, disposition, block, block_size,
                payload + n, compressed_len);
            if (st != MINLZ_OK) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return st;
            }
            n += compressed_len;
            view->compressed_bytes_tabled += compressed_len;
        } else if (disposition == 16u) {
            if (payload_len - n < block_size) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return MINLZ_CORRUPT;
            }
            memcpy(block, payload + n, block_size);
            n += block_size;
            view->compressed_blocks_raw++;
            view->compressed_bytes_raw += block_size;
        } else if (disposition == 17u) {
            if (n >= payload_len) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return MINLZ_CORRUPT;
            }
            memset(block, payload[n++], block_size);
            view->compressed_blocks_rle++;
            view->compressed_bytes_rle++;
        } else if (disposition == 18u) {
            size_t sparse_len = 0;
            size_t used = 0;
            st = minlz_read_uvarint(
                payload + n, payload_len - n,
                &sparse_len, &used);
            if (st != MINLZ_OK ||
                sparse_len > payload_len - n - used) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return st == MINLZ_OK ? MINLZ_CORRUPT : st;
            }
            n += used;
            view->compressed_bytes_sparse += used;
            st = minlz_search_sparse_decode(
                block, block_size, payload + n, sparse_len);
            if (st != MINLZ_OK) {
                free(table);
                minlz_huff0_decoder_destroy(huff0);
                return st;
            }
            n += sparse_len;
            view->compressed_blocks_sparse++;
            view->compressed_bytes_sparse += sparse_len;
        } else {
            free(table);
            minlz_huff0_decoder_destroy(huff0);
            return MINLZ_CORRUPT;
        }
    }
    minlz_huff0_decoder_destroy(huff0);
    if (n != payload_len) {
        free(table);
        return MINLZ_CORRUPT;
    }
    if (!ignore_crc && minlz_crc(table, table_len) != expected_crc) {
        free(table);
        return MINLZ_CRC;
    }
    view->table = table;
    view->table_len = table_len;
    *table_storage = table;
    return MINLZ_OK;
}

static uint64_t minlz_search_pattern_value(const uint8_t *pattern,
                                           size_t pattern_len,
                                           size_t pos) {
    uint64_t value = 0;
    size_t i;
    size_t n = pattern_len - pos;
    if (n > 8u) {
        n = 8u;
    }
    for (i = 0; i < n; i++) {
        value |= (uint64_t)pattern[pos + i] << (8u * i);
    }
    return value;
}

static int minlz_search_table_has(const minlz_search_table_view *view,
                                  const uint8_t *pattern,
                                  size_t pattern_len,
                                  size_t pos) {
    uint32_t mask =
        ((uint32_t)1u <<
         (view->config.base_table_log2 - view->reductions)) -
        1u;
    uint32_t hash = minlz_search_hash(
                        minlz_search_pattern_value(pattern, pattern_len, pos),
                        view->config.base_table_log2,
                        view->config.match_len) &
                    mask;
    return (view->table[hash >> 3] &
            (uint8_t)(1u << (hash & 7u))) != 0;
}

static int minlz_search_table_all_zero(const minlz_search_table_view *view) {
    size_t i;
    for (i = 0; i < view->table_len; i++) {
        if (view->table[i] != 0u) {
            return 0;
        }
    }
    return 1;
}

static int minlz_search_pattern_contains(const uint8_t *haystack,
                                         size_t haystack_len,
                                         const uint8_t *needle,
                                         size_t needle_len) {
    size_t i;
    if (needle_len == 0u || needle_len > haystack_len) {
        return 0;
    }
    for (i = 0; i + needle_len <= haystack_len; i++) {
        if (memcmp(haystack + i, needle, needle_len) == 0) {
            return 1;
        }
    }
    return 0;
}

/*
 * A prefix-only query can use an all-zero long-prefix table to skip any block
 * that was indexed with forward overlap. The final block has no such overlap,
 * so its tail may contain the prefix without setting a table bit. Go v1.2
 * keeps that one block available and scans it at EOF.
 */
static int minlz_search_reject_needs_tail_rescue(
    const minlz_search_table_view *view,
    const uint8_t *pattern, size_t pattern_len) {
    size_t pl;
    size_t ml;
    size_t extras;
    size_t i;

    if (!view || view->config.table_type !=
                     MINLZ_SEARCH_TABLE_LONG_PREFIX ||
        !minlz_search_table_all_zero(view)) {
        return 0;
    }
    pl = view->config.long_prefix_len;
    ml = view->config.match_len;
    extras = view->config.extras;
    if (!minlz_search_pattern_contains(
            pattern, pattern_len, view->config.long_prefix, pl)) {
        return 0;
    }
    for (i = 0; i + pl + ml + extras <= pattern_len; i++) {
        if (memcmp(pattern + i, view->config.long_prefix, pl) == 0) {
            return 0;
        }
    }
    return 1;
}

int minlz_search_pattern_might_match(const minlz_search_table_view *view,
                                     const uint8_t *pattern,
                                     size_t pattern_len,
                                     int *can_use) {
    size_t ml;
    size_t i;
    if (can_use) {
        *can_use = 0;
    }
    if (!view || !view->table || (!pattern && pattern_len != 0u)) {
        return 1;
    }
    ml = view->config.match_len;
    switch (view->config.table_type) {
    case MINLZ_SEARCH_TABLE_NO_PREFIX:
        if (pattern_len < ml) {
            return 1;
        }
        if (can_use) {
            *can_use = 1;
        }
        for (i = 0; i + ml <= pattern_len; i++) {
            if (!minlz_search_table_has(view, pattern, pattern_len, i)) {
                return i != 0u;
            }
        }
        return 1;

    case MINLZ_SEARCH_TABLE_BYTE_PREFIX:
    case MINLZ_SEARCH_TABLE_MASK_PREFIX: {
        int checked = 0;
        int first_present = 0;
        if (pattern_len < ml) {
            return 1;
        }
        for (i = 1u; i + ml <= pattern_len; i++) {
            int prefix = minlz_search_prefix_byte_matches(
                &view->config, pattern[i - 1u]);
            int present;
            if (!prefix) {
                continue;
            }
            present =
                minlz_search_table_has(view, pattern, pattern_len, i);
            if (!checked) {
                first_present = present;
            }
            checked++;
            if (!present) {
                if (can_use) {
                    *can_use = 1;
                }
                if (first_present) {
                    return 1;
                }
                return minlz_search_table_has(
                    view, pattern, pattern_len, 0u);
            }
        }
        if (!checked) {
            return 1;
        }
        if (can_use) {
            *can_use = 1;
        }
        return first_present;
    }

    case MINLZ_SEARCH_TABLE_LONG_PREFIX: {
        size_t pl = view->config.long_prefix_len;
        size_t extras = view->config.extras;
        int checked = 0;
        if (!minlz_search_pattern_contains(pattern, pattern_len,
                                           view->config.long_prefix, pl)) {
            return 1;
        }
        if (can_use) {
            *can_use = 1;
        }
        for (i = 0; i + pl + ml + extras <= pattern_len; i++) {
            size_t j;
            if (memcmp(pattern + i, view->config.long_prefix, pl) != 0) {
                continue;
            }
            checked = 1;
            for (j = 0; j <= extras; j++) {
                if (!minlz_search_table_has(
                        view, pattern, pattern_len, i + pl + j)) {
                    return 0;
                }
            }
        }
        return checked ? 1 : !minlz_search_table_all_zero(view);
    }
    default:
        return 1;
    }
}

typedef struct minlz_search_window_record {
    minlz_search_window_stat stat;
    uint32_t hash;
} minlz_search_window_record;

struct minlz_block_searcher {
    minlz_read_cb read_cb;
    void *read_ctx;
    minlz_workspace *workspace;
    uint8_t *payload;
    size_t payload_cap;
    uint8_t *previous;
    size_t previous_len;
    size_t previous_cap;
    int64_t previous_start;
    uint8_t *boundary_tail;
    size_t boundary_tail_len;
    size_t boundary_tail_cap;
    int64_t boundary_tail_start;
    uint8_t *boundary_join;
    size_t boundary_join_cap;
    uint8_t *forward_context;
    size_t forward_context_len;
    size_t forward_context_cap;
    int64_t forward_context_start;
    int64_t forward_context_offset;
    uint8_t *lazy_payload;
    size_t lazy_payload_len;
    size_t lazy_payload_cap;
    size_t lazy_decoded_len;
    size_t lazy_max_block;
    int64_t lazy_start;
    uint8_t lazy_chunk_type;
    int lazy_valid;
    minlz_workspace *lazy_workspace;
    const uint8_t *lazy_decoded;
    size_t lazy_loaded_len;
    uint8_t *tail_payload;
    size_t tail_payload_len;
    size_t tail_payload_cap;
    size_t tail_decoded_len;
    size_t tail_max_block;
    size_t tail_wire_len;
    int64_t tail_start;
    uint8_t tail_chunk_type;
    int tail_rescue;
    size_t max_block_size;
    int ignore_crc;
    int bail_on_missing;
    int collect_stats;
    minlz_search_info_cb info_cb;
    void *info_ctx;
    minlz_search_stats stats;
    minlz_search_window_record *windows;
    size_t window_count;
    size_t window_cap;
    uint64_t tables_tallied;
    minlz_search_config window_config;
    int windows_initialized;
    int windows_compatible;
};

static minlz_status minlz_searcher_reserve(uint8_t **buf, size_t *cap,
                                           size_t need) {
    uint8_t *next;
    size_t next_cap;
    if (need <= *cap) {
        return MINLZ_OK;
    }
    next_cap = *cap ? *cap : 256u;
    while (next_cap < need) {
        if (next_cap > SIZE_MAX / 2u) {
            next_cap = need;
            break;
        }
        next_cap *= 2u;
    }
    next = (uint8_t *)realloc(*buf, next_cap);
    if (!next) {
        return MINLZ_IO;
    }
    *buf = next;
    *cap = next_cap;
    return MINLZ_OK;
}

static void minlz_searcher_clear_lazy(
    minlz_block_searcher *searcher) {
    searcher->lazy_valid = 0;
    searcher->lazy_decoded = NULL;
    searcher->lazy_loaded_len = 0u;
}

static minlz_status minlz_searcher_hold_lazy(
    minlz_block_searcher *searcher, uint8_t chunk_type,
    size_t payload_len, size_t decoded_len, size_t max_block,
    int64_t block_start) {
    minlz_status st = minlz_searcher_reserve(
        &searcher->lazy_payload, &searcher->lazy_payload_cap,
        payload_len ? payload_len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    memcpy(searcher->lazy_payload, searcher->payload, payload_len);
    searcher->lazy_payload_len = payload_len;
    searcher->lazy_decoded_len = decoded_len;
    searcher->lazy_max_block = max_block;
    searcher->lazy_start = block_start;
    searcher->lazy_chunk_type = chunk_type;
    searcher->lazy_valid = 1;
    searcher->lazy_decoded = NULL;
    searcher->lazy_loaded_len = 0u;
    return MINLZ_OK;
}

static minlz_status minlz_searcher_load_lazy(
    void *ctx, const uint8_t **block, size_t *block_len) {
    minlz_block_searcher *searcher =
        (minlz_block_searcher *)ctx;
    minlz_status st;
    uint8_t *decoded = NULL;
    size_t decoded_len = 0u;

    if (!searcher || !block || !block_len) {
        return MINLZ_CORRUPT;
    }
    *block = NULL;
    *block_len = 0u;
    if (!searcher->lazy_valid) {
        return MINLZ_OK;
    }
    if (searcher->lazy_decoded) {
        *block = searcher->lazy_decoded;
        *block_len = searcher->lazy_loaded_len;
        return MINLZ_OK;
    }
    if (searcher->lazy_payload_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    if (searcher->lazy_chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        uint32_t expected_crc =
            minlz_load32(searcher->lazy_payload);
        decoded =
            searcher->lazy_payload + MINLZ_CHECKSUM_SIZE;
        decoded_len =
            searcher->lazy_payload_len - MINLZ_CHECKSUM_SIZE;
        if (!searcher->ignore_crc &&
            minlz_crc(decoded, decoded_len) != expected_crc) {
            return MINLZ_CRC;
        }
    } else {
        if (!searcher->lazy_workspace) {
            st = minlz_workspace_create(
                &searcher->lazy_workspace);
            if (st != MINLZ_OK) {
                return st;
            }
        }
        st = minlz_decode_stream_chunk(
            searcher->lazy_workspace,
            searcher->lazy_payload,
            searcher->lazy_payload_len,
            &decoded, &decoded_len,
            searcher->lazy_chunk_type ==
                MINLZ_CHUNK_MINLZ_COMP_CRC,
            searcher->ignore_crc,
            searcher->lazy_max_block);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (decoded_len != searcher->lazy_decoded_len ||
        decoded_len > searcher->lazy_max_block) {
        return MINLZ_CORRUPT;
    }
    searcher->lazy_decoded = decoded;
    searcher->lazy_loaded_len = decoded_len;
    *block = decoded;
    *block_len = decoded_len;
    return MINLZ_OK;
}

static int minlz_search_same_layout(
    const minlz_search_config *a,
    const minlz_search_config *b) {
    if (a->table_type != b->table_type ||
        a->match_len != b->match_len ||
        a->base_table_log2 != b->base_table_log2 ||
        a->extras != b->extras) {
        return 0;
    }
    switch (a->table_type) {
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX:
        return memcmp(a->prefix_bytes, b->prefix_bytes,
                      sizeof(a->prefix_bytes)) == 0;
    case MINLZ_SEARCH_TABLE_MASK_PREFIX:
        return memcmp(a->prefix_mask, b->prefix_mask,
                      sizeof(a->prefix_mask)) == 0;
    case MINLZ_SEARCH_TABLE_LONG_PREFIX:
        return a->long_prefix_len == b->long_prefix_len &&
               memcmp(a->long_prefix, b->long_prefix,
                      a->long_prefix_len) == 0;
    default:
        return 1;
    }
}

static minlz_status minlz_searcher_add_window(
    minlz_block_searcher *searcher,
    const minlz_search_config *config,
    const uint8_t *pattern, size_t pattern_len,
    int prefix, size_t position) {
    minlz_search_window_record *next;
    minlz_search_window_record *record;
    size_t next_cap;
    size_t bytes_len;
    if (position >= pattern_len) {
        return MINLZ_CORRUPT;
    }
    if (searcher->window_count == searcher->window_cap) {
        next_cap = searcher->window_cap
                       ? searcher->window_cap * 2u
                       : 16u;
        if (next_cap < searcher->window_count ||
            next_cap > SIZE_MAX / sizeof(*next)) {
            return MINLZ_TOO_LARGE;
        }
        next = (minlz_search_window_record *)realloc(
            searcher->windows, next_cap * sizeof(*next));
        if (!next) {
            return MINLZ_IO;
        }
        searcher->windows = next;
        searcher->window_cap = next_cap;
    }
    record = &searcher->windows[searcher->window_count++];
    memset(record, 0, sizeof(*record));
    record->stat.position = position;
    record->stat.prefix_position = prefix;
    if (prefix >= 0) {
        record->stat.prefix_byte = pattern[(size_t)prefix];
    }
    bytes_len = pattern_len - position;
    if (bytes_len > config->match_len) {
        bytes_len = config->match_len;
    }
    if (bytes_len > sizeof(record->stat.bytes)) {
        bytes_len = sizeof(record->stat.bytes);
    }
    memcpy(record->stat.bytes, pattern + position, bytes_len);
    record->stat.bytes_len = (uint8_t)bytes_len;
    record->hash = minlz_search_hash(
        minlz_search_pattern_value(pattern, pattern_len, position),
        config->base_table_log2, config->match_len);
    return MINLZ_OK;
}

static minlz_status minlz_searcher_init_windows(
    minlz_block_searcher *searcher,
    const minlz_search_config *config,
    const uint8_t *pattern, size_t pattern_len) {
    size_t ml = config->match_len;
    size_t i;
    minlz_status st;
    if (searcher->windows_initialized) {
        if (searcher->windows_compatible &&
            !minlz_search_same_layout(
                &searcher->window_config, config)) {
            searcher->windows_compatible = 0;
        }
        return MINLZ_OK;
    }
    searcher->windows_initialized = 1;
    searcher->windows_compatible = 1;
    searcher->window_config = *config;
    if (ml == 0u || pattern_len < ml) {
        return MINLZ_OK;
    }
    switch (config->table_type) {
    case MINLZ_SEARCH_TABLE_NO_PREFIX:
        for (i = 0u; i + ml <= pattern_len; i++) {
            st = minlz_searcher_add_window(
                searcher, config, pattern, pattern_len, -1, i);
            if (st != MINLZ_OK) {
                return st;
            }
        }
        break;
    case MINLZ_SEARCH_TABLE_BYTE_PREFIX: {
        uint8_t prefix_set[256];
        memset(prefix_set, 0, sizeof(prefix_set));
        for (i = 0u; i < sizeof(config->prefix_bytes); i++) {
            prefix_set[config->prefix_bytes[i]] = 1u;
        }
        for (i = 1u; i + ml <= pattern_len; i++) {
            if (prefix_set[pattern[i - 1u]]) {
                st = minlz_searcher_add_window(
                    searcher, config, pattern, pattern_len,
                    (int)(i - 1u), i);
                if (st != MINLZ_OK) {
                    return st;
                }
            }
        }
        st = minlz_searcher_add_window(
            searcher, config, pattern, pattern_len, -1, 0u);
        if (st != MINLZ_OK) {
            return st;
        }
        break;
    }
    case MINLZ_SEARCH_TABLE_MASK_PREFIX:
        for (i = 1u; i + ml <= pattern_len; i++) {
            uint8_t b = pattern[i - 1u];
            if ((config->prefix_mask[b >> 3] &
                 (uint8_t)(1u << (b & 7u))) != 0u) {
                st = minlz_searcher_add_window(
                    searcher, config, pattern, pattern_len,
                    (int)(i - 1u), i);
                if (st != MINLZ_OK) {
                    return st;
                }
            }
        }
        st = minlz_searcher_add_window(
            searcher, config, pattern, pattern_len, -1, 0u);
        if (st != MINLZ_OK) {
            return st;
        }
        break;
    case MINLZ_SEARCH_TABLE_LONG_PREFIX: {
        size_t pl = config->long_prefix_len;
        size_t extras = config->extras;
        if (pl + ml + extras > pattern_len) {
            break;
        }
        for (i = 0u; i + pl + ml + extras <= pattern_len; i++) {
            size_t j;
            if (memcmp(pattern + i, config->long_prefix, pl) != 0) {
                continue;
            }
            for (j = 0u; j <= extras; j++) {
                st = minlz_searcher_add_window(
                    searcher, config, pattern, pattern_len,
                    (int)i, i + pl + j);
                if (st != MINLZ_OK) {
                    return st;
                }
            }
        }
        break;
    }
    default:
        return MINLZ_UNSUPPORTED;
    }
    return MINLZ_OK;
}

static minlz_status minlz_searcher_tally_table(
    minlz_block_searcher *searcher,
    const minlz_search_table_view *view,
    const uint8_t *pattern, size_t pattern_len,
    int compressed, size_t wire_bytes) {
    uint64_t set_bits = 0u;
    double population;
    size_t i;
    minlz_status st;
    if (!searcher->collect_stats) {
        return MINLZ_OK;
    }
    st = minlz_searcher_init_windows(
        searcher, &view->config, pattern, pattern_len);
    if (st != MINLZ_OK) {
        return st;
    }
    searcher->stats.table_bits_sum +=
        view->config.base_table_log2 - view->reductions;
    searcher->stats.table_reductions_sum += view->reductions;
    for (i = 0u; i < view->table_len; i++) {
        set_bits += minlz_popcount8(view->table[i]);
    }
    population = view->table_len
                     ? (double)set_bits * 100.0 /
                           ((double)view->table_len * 8.0)
                     : 0.0;
    if (searcher->tables_tallied == 0u ||
        population < searcher->stats.table_population_min) {
        searcher->stats.table_population_min = population;
    }
    if (searcher->tables_tallied == 0u ||
        population > searcher->stats.table_population_max) {
        searcher->stats.table_population_max = population;
    }
    searcher->stats.table_population_sum += population;
    searcher->tables_tallied++;
    if (compressed) {
        searcher->stats.tables_compressed_bytes += wire_bytes;
        searcher->stats.table_bitmap_bytes += view->table_len;
        searcher->stats.compressed_blocks_total +=
            view->compressed_blocks_total;
        searcher->stats.compressed_blocks_raw +=
            view->compressed_blocks_raw;
        searcher->stats.compressed_blocks_rle +=
            view->compressed_blocks_rle;
        searcher->stats.compressed_blocks_sparse +=
            view->compressed_blocks_sparse;
        searcher->stats.compressed_tables_sum +=
            view->compressed_tables;
        searcher->stats.compressed_bytes_tabled +=
            view->compressed_bytes_tabled;
        searcher->stats.compressed_bytes_raw +=
            view->compressed_bytes_raw;
        searcher->stats.compressed_bytes_rle +=
            view->compressed_bytes_rle;
        searcher->stats.compressed_bytes_sparse +=
            view->compressed_bytes_sparse;
        searcher->stats.compressed_bytes_table_headers +=
            view->compressed_bytes_table_headers;
    }
    if (searcher->windows_compatible) {
        uint32_t mask =
            ((uint32_t)1u <<
             (view->config.base_table_log2 - view->reductions)) -
            1u;
        for (i = 0u; i < searcher->window_count; i++) {
            minlz_search_window_record *record =
                &searcher->windows[i];
            uint32_t hash = record->hash & mask;
            if ((view->table[hash >> 3] &
                 (uint8_t)(1u << (hash & 7u))) != 0u) {
                record->stat.present++;
            } else {
                record->stat.absent++;
            }
        }
    }
    return MINLZ_OK;
}

static minlz_status minlz_searcher_read_payload(
    minlz_block_searcher *searcher, size_t len) {
    minlz_status st = minlz_searcher_reserve(
        &searcher->payload, &searcher->payload_cap, len ? len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    return minlz_read_exact(searcher->read_cb, searcher->read_ctx,
                            searcher->payload, len, 0, NULL);
}

static minlz_status minlz_search_read_u64varint(
    const uint8_t *src, size_t src_len, uint64_t *value, size_t *used) {
    uint64_t result = 0;
    unsigned shift = 0;
    size_t i;
    if (!value || !used) {
        return MINLZ_CORRUPT;
    }
    for (i = 0; i < src_len && i < 10u; i++) {
        uint8_t byte = src[i];
        if (byte < 0x80u) {
            if (i == 9u && byte > 1u) {
                return MINLZ_CORRUPT;
            }
            result |= (uint64_t)byte << shift;
            *value = result;
            *used = i + 1u;
            return MINLZ_OK;
        }
        result |= (uint64_t)(byte & 0x7fu) << shift;
        shift += 7u;
    }
    return MINLZ_CORRUPT;
}

static minlz_status minlz_searcher_data_len(
    uint8_t chunk_type, const uint8_t *payload, size_t payload_len,
    size_t max_block, size_t *decoded_len) {
    size_t header_len = 0;
    minlz_status st;
    if (!decoded_len || payload_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        *decoded_len = payload_len - MINLZ_CHECKSUM_SIZE;
    } else {
        st = minlz_payload_decoded_len(
            payload + MINLZ_CHECKSUM_SIZE,
            payload_len - MINLZ_CHECKSUM_SIZE,
            decoded_len, &header_len);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (*decoded_len > max_block) {
        return MINLZ_TOO_LARGE;
    }
    return MINLZ_OK;
}

static minlz_status minlz_searcher_decode_data(
    minlz_block_searcher *searcher, uint8_t chunk_type,
    size_t payload_len, size_t max_block,
    uint8_t **decoded, size_t *decoded_len) {
    uint32_t expected_crc;
    if (payload_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        *decoded = searcher->payload + MINLZ_CHECKSUM_SIZE;
        *decoded_len = payload_len - MINLZ_CHECKSUM_SIZE;
        if (*decoded_len > max_block) {
            return MINLZ_TOO_LARGE;
        }
        expected_crc = minlz_load32(searcher->payload);
        if (!searcher->ignore_crc &&
            minlz_crc(*decoded, *decoded_len) != expected_crc) {
            return MINLZ_CRC;
        }
        return MINLZ_OK;
    }
    return minlz_decode_stream_chunk(
        searcher->workspace, searcher->payload, payload_len,
        decoded, decoded_len,
        chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC,
        searcher->ignore_crc, max_block);
}

minlz_status minlz_search_result_load_previous(
    const minlz_search_result *result,
    const uint8_t **block, size_t *block_len) {
    if (!result || !block || !block_len) {
        return MINLZ_CORRUPT;
    }
    *block = result->previous_block;
    *block_len = result->previous_block
                     ? result->previous_block_len
                     : 0u;
    if (result->previous_block ||
        !result->previous_block_loader) {
        return MINLZ_OK;
    }
    return result->previous_block_loader(
        result->previous_block_loader_ctx,
        block, block_len);
}

typedef struct minlz_search_lazy_callback {
    minlz_search_match_cb callback;
    void *callback_ctx;
    minlz_search_previous_loader_cb loader;
    void *loader_ctx;
    size_t previous_len;
    int64_t previous_start;
    int64_t current_start;
} minlz_search_lazy_callback;

static minlz_status minlz_searcher_lazy_callback(
    void *ctx, const minlz_search_result *source) {
    minlz_search_lazy_callback *lazy =
        (minlz_search_lazy_callback *)ctx;
    minlz_search_result result = *source;
    if (!lazy || !lazy->callback) {
        return MINLZ_OK;
    }
    if (!result.previous_block &&
        result.previous_block_len == 0u &&
        result.stream_offset >= lazy->current_start) {
        result.previous_block_len = lazy->previous_len;
        result.block_start = lazy->previous_start;
        result.offset =
            result.stream_offset - result.block_start;
        result.previous_block_loader = lazy->loader;
        result.previous_block_loader_ctx = lazy->loader_ctx;
    }
    return lazy->callback(lazy->callback_ctx, &result);
}

static minlz_status minlz_searcher_callback(
    minlz_block_searcher *searcher,
    minlz_search_match_cb callback, void *callback_ctx,
    const uint8_t *block, size_t block_len,
    int64_t block_start, int64_t stream_offset,
    int64_t result_offset) {
    const uint8_t *previous =
        searcher->previous_len ? searcher->previous : NULL;
    size_t previous_len = searcher->previous_len;
    minlz_search_result result;
    minlz_status st;
    searcher->stats.matches++;
    if (!callback) {
        return MINLZ_OK;
    }
    memset(&result, 0, sizeof(result));
    result.previous_block = previous;
    result.previous_block_len = previous_len;
    result.block = block;
    result.block_len = block_len;
    result.offset = result_offset;
    result.stream_offset = stream_offset;
    result.block_start = block_start;
    st = callback(callback_ctx, &result);
    if (st == MINLZ_SEARCH_STOP) {
        return MINLZ_SEARCH_STOP;
    }
    return st;
}

static minlz_status minlz_searcher_callback_with_context(
    minlz_block_searcher *searcher,
    minlz_search_match_cb callback, void *callback_ctx,
    const uint8_t *previous, size_t previous_len,
    const uint8_t *block, size_t block_len,
    int64_t block_start, int64_t stream_offset,
    int64_t result_offset) {
    minlz_search_result result;
    minlz_status st;
    searcher->stats.matches++;
    if (!callback) {
        return MINLZ_OK;
    }
    memset(&result, 0, sizeof(result));
    result.previous_block = previous_len ? previous : NULL;
    result.previous_block_len = previous_len;
    result.block = block;
    result.block_len = block_len;
    result.offset = result_offset;
    result.stream_offset = stream_offset;
    result.block_start = block_start;
    st = callback(callback_ctx, &result);
    if (st == MINLZ_SEARCH_STOP) {
        return MINLZ_SEARCH_STOP;
    }
    return st;
}

static void minlz_searcher_clear_boundary_tail(
    minlz_block_searcher *searcher) {
    searcher->boundary_tail_len = 0u;
    searcher->boundary_tail_start = 0;
}

static minlz_status minlz_searcher_update_boundary_tail(
    minlz_block_searcher *searcher,
    const uint8_t *block, size_t block_len,
    int64_t block_end, size_t keep) {
    size_t drop = 0u;
    size_t total;
    minlz_status st;
    if (keep == 0u) {
        minlz_searcher_clear_boundary_tail(searcher);
        return MINLZ_OK;
    }
    if (block_len >= keep) {
        st = minlz_searcher_reserve(
            &searcher->boundary_tail,
            &searcher->boundary_tail_cap, keep);
        if (st != MINLZ_OK) {
            return st;
        }
        memcpy(searcher->boundary_tail,
               block + block_len - keep, keep);
        searcher->boundary_tail_len = keep;
    } else {
        if (searcher->boundary_tail_len >
            SIZE_MAX - block_len) {
            return MINLZ_TOO_LARGE;
        }
        total = searcher->boundary_tail_len + block_len;
        if (total > keep) {
            drop = total - keep;
            total = keep;
        }
        st = minlz_searcher_reserve(
            &searcher->boundary_tail,
            &searcher->boundary_tail_cap,
            total ? total : 1u);
        if (st != MINLZ_OK) {
            return st;
        }
        if (drop != 0u) {
            memmove(searcher->boundary_tail,
                    searcher->boundary_tail + drop,
                    searcher->boundary_tail_len - drop);
            searcher->boundary_tail_len -= drop;
        }
        if (block_len != 0u) {
            memcpy(searcher->boundary_tail +
                       searcher->boundary_tail_len,
                   block, block_len);
            searcher->boundary_tail_len += block_len;
        }
    }
    searcher->boundary_tail_start =
        block_end - (int64_t)searcher->boundary_tail_len;
    return MINLZ_OK;
}

static minlz_status minlz_searcher_set_forward_context(
    minlz_block_searcher *searcher,
    const uint8_t *block, size_t block_len,
    int64_t block_start, int64_t match_offset) {
    minlz_status st = minlz_searcher_reserve(
        &searcher->forward_context,
        &searcher->forward_context_cap,
        block_len ? block_len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    if (block_len != 0u) {
        memcpy(searcher->forward_context, block, block_len);
    }
    searcher->forward_context_len = block_len;
    searcher->forward_context_start = block_start;
    searcher->forward_context_offset = match_offset;
    return MINLZ_OK;
}

static minlz_status minlz_searcher_forward_eof(
    minlz_search_match_cb callback, void *callback_ctx,
    const uint8_t *match_block, size_t match_block_len,
    int64_t block_start, int64_t stream_offset) {
    minlz_search_result result;
    minlz_status st;
    if (!callback) {
        return MINLZ_OK;
    }
    memset(&result, 0, sizeof(result));
    result.previous_block = match_block;
    result.previous_block_len = match_block_len;
    result.block = NULL;
    result.block_len = 0u;
    result.offset = stream_offset - block_start;
    result.stream_offset = stream_offset;
    result.block_start = block_start;
    st = callback(callback_ctx, &result);
    /*
     * A second forward request at EOF has nowhere else to go. Go drops the
     * deferred request after re-dispatching with a nil next block.
     */
    return st == MINLZ_SEARCH_FORWARD ? MINLZ_OK : st;
}

static minlz_status minlz_searcher_scan_block(
    minlz_block_searcher *searcher,
    const uint8_t *pattern, size_t pattern_len,
    const uint8_t *block, size_t block_len,
    int64_t current_start,
    minlz_search_match_cb callback, void *callback_ctx,
    uint64_t *matches_found,
    int *forward_requested, int64_t *forward_offset) {
    size_t tail_len = searcher->boundary_tail_len;
    size_t i;
    minlz_status st;
    uint64_t before = searcher->stats.matches;

    if (tail_len != 0u && pattern_len > 1u) {
        size_t head_len = block_len;
        size_t joined_len;
        if (head_len > pattern_len - 1u) {
            head_len = pattern_len - 1u;
        }
        if (tail_len > SIZE_MAX - head_len) {
            return MINLZ_TOO_LARGE;
        }
        joined_len = tail_len + head_len;
        st = minlz_searcher_reserve(
            &searcher->boundary_join,
            &searcher->boundary_join_cap,
            joined_len ? joined_len : 1u);
        if (st != MINLZ_OK) {
            return st;
        }
        memcpy(searcher->boundary_join,
               searcher->boundary_tail, tail_len);
        memcpy(searcher->boundary_join + tail_len,
               block, head_len);
        for (i = 0u; i < tail_len &&
             i + pattern_len <= joined_len; i++) {
            const uint8_t *context;
            size_t context_len;
            int64_t context_start;
            int64_t stream_offset;
            int64_t result_offset;
            if (i + pattern_len <= tail_len ||
                memcmp(searcher->boundary_join + i,
                       pattern, pattern_len) != 0) {
                continue;
            }
            stream_offset =
                searcher->boundary_tail_start + (int64_t)i;
            if (searcher->previous_len != 0u &&
                stream_offset >= searcher->previous_start) {
                context = searcher->previous;
                context_len = searcher->previous_len;
                context_start = searcher->previous_start;
                result_offset = stream_offset - context_start;
            } else {
                context = searcher->boundary_tail;
                context_len = tail_len;
                context_start = searcher->boundary_tail_start;
                result_offset = (int64_t)i;
            }
            st = minlz_searcher_callback_with_context(
                searcher, callback, callback_ctx,
                context, context_len, block, block_len,
                context_start, stream_offset, result_offset);
            if (st == MINLZ_SEARCH_FORWARD) {
                st = minlz_searcher_set_forward_context(
                    searcher, context, context_len,
                    context_start, result_offset);
                if (st != MINLZ_OK) {
                    return st;
                }
                *forward_requested = 1;
                *forward_offset = stream_offset;
            } else if (st != MINLZ_OK) {
                return st;
            }
        }
    }

    if (pattern_len <= block_len) {
        for (i = 0; i + pattern_len <= block_len; i++) {
            int64_t offset;
            if (memcmp(block + i, pattern, pattern_len) != 0) {
                continue;
            }
            offset = searcher->previous_len
                         ? (int64_t)searcher->previous_len + (int64_t)i
                         : (int64_t)i;
            st = minlz_searcher_callback(
                searcher, callback, callback_ctx,
                block, block_len,
                searcher->previous_len
                    ? searcher->previous_start
                    : current_start,
                current_start + (int64_t)i, offset);
            if (st == MINLZ_SEARCH_FORWARD) {
                st = minlz_searcher_set_forward_context(
                    searcher, block, block_len,
                    current_start, (int64_t)i);
                if (st != MINLZ_OK) {
                    return st;
                }
                *forward_requested = 1;
                *forward_offset = current_start + (int64_t)i;
            } else if (st != MINLZ_OK) {
                return st;
            }
        }
    }
    st = minlz_searcher_update_boundary_tail(
        searcher, block, block_len,
        current_start + (int64_t)block_len,
        pattern_len - 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    *matches_found = searcher->stats.matches - before;
    return MINLZ_OK;
}

static minlz_status minlz_searcher_hold_tail(
    minlz_block_searcher *searcher, uint8_t chunk_type,
    size_t payload_len, size_t decoded_len, size_t max_block,
    int64_t block_start) {
    minlz_status st = minlz_searcher_reserve(
        &searcher->tail_payload, &searcher->tail_payload_cap,
        payload_len ? payload_len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    memcpy(searcher->tail_payload, searcher->payload, payload_len);
    searcher->tail_payload_len = payload_len;
    searcher->tail_decoded_len = decoded_len;
    searcher->tail_max_block = max_block;
    searcher->tail_wire_len = MINLZ_CHUNK_HEADER_SIZE + payload_len;
    searcher->tail_start = block_start;
    searcher->tail_chunk_type = chunk_type;
    searcher->tail_rescue = 1;
    searcher->stats.blocks_deferred++;
    searcher->stats.blocks_deferred_skipped++;
    return MINLZ_OK;
}

static minlz_status minlz_searcher_rescue_tail(
    minlz_block_searcher *searcher,
    const uint8_t *pattern, size_t pattern_len,
    minlz_search_match_cb callback, void *callback_ctx) {
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    uint64_t found = 0;
    int forward_requested = 0;
    int64_t forward_offset = 0;
    minlz_status st;

    if (!searcher->tail_rescue) {
        return MINLZ_OK;
    }
    searcher->tail_rescue = 0;
    if (searcher->tail_payload_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    if (searcher->tail_chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        uint32_t expected_crc = minlz_load32(searcher->tail_payload);
        decoded = searcher->tail_payload + MINLZ_CHECKSUM_SIZE;
        decoded_len =
            searcher->tail_payload_len - MINLZ_CHECKSUM_SIZE;
        if (!searcher->ignore_crc &&
            minlz_crc(decoded, decoded_len) != expected_crc) {
            return MINLZ_CRC;
        }
    } else {
        st = minlz_decode_stream_chunk(
            searcher->workspace,
            searcher->tail_payload, searcher->tail_payload_len,
            &decoded, &decoded_len,
            searcher->tail_chunk_type ==
                MINLZ_CHUNK_MINLZ_COMP_CRC,
            searcher->ignore_crc, searcher->tail_max_block);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (decoded_len != searcher->tail_decoded_len ||
        decoded_len > searcher->tail_max_block) {
        return MINLZ_CORRUPT;
    }

    if (searcher->stats.blocks_skipped != 0u) {
        searcher->stats.blocks_skipped--;
    }
    if (searcher->stats.blocks_deferred_skipped != 0u) {
        searcher->stats.blocks_deferred_skipped--;
    }
    if (searcher->stats.compressed_bytes_skipped >=
        searcher->tail_wire_len) {
        searcher->stats.compressed_bytes_skipped -=
            searcher->tail_wire_len;
    }
    searcher->stats.blocks_searched++;
    searcher->stats.uncompressed_bytes_searched += decoded_len;
    searcher->previous_len = 0;
    minlz_searcher_clear_boundary_tail(searcher);
    minlz_searcher_clear_lazy(searcher);
    st = minlz_searcher_scan_block(
        searcher, pattern, pattern_len,
        decoded, decoded_len, searcher->tail_start,
        callback, callback_ctx, &found,
        &forward_requested, &forward_offset);
    if (st != MINLZ_OK) {
        return st;
    }
    if (forward_requested) {
        st = minlz_searcher_forward_eof(
            callback, callback_ctx,
            decoded, decoded_len,
            searcher->tail_start, forward_offset);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (found == 0u) {
        searcher->stats.blocks_false_positive++;
    }
    return MINLZ_OK;
}

minlz_status minlz_block_searcher_create(
    minlz_block_searcher **searcher,
    minlz_read_cb read_cb, void *read_ctx) {
    minlz_block_searcher *value;
    minlz_status st;
    if (!searcher || !read_cb) {
        return MINLZ_CORRUPT;
    }
    *searcher = NULL;
    value = (minlz_block_searcher *)calloc(1, sizeof(*value));
    if (!value) {
        return MINLZ_IO;
    }
    st = minlz_workspace_create(&value->workspace);
    if (st != MINLZ_OK) {
        free(value);
        return st;
    }
    value->read_cb = read_cb;
    value->read_ctx = read_ctx;
    value->max_block_size = MINLZ_MAX_BLOCK_SIZE;
    *searcher = value;
    return MINLZ_OK;
}

void minlz_block_searcher_destroy(minlz_block_searcher *searcher) {
    if (!searcher) {
        return;
    }
    minlz_workspace_destroy(searcher->workspace);
    minlz_workspace_destroy(searcher->lazy_workspace);
    free(searcher->payload);
    free(searcher->previous);
    free(searcher->boundary_tail);
    free(searcher->boundary_join);
    free(searcher->forward_context);
    free(searcher->lazy_payload);
    free(searcher->tail_payload);
    free(searcher->windows);
    free(searcher);
}

void minlz_block_searcher_set_ignore_crc(
    minlz_block_searcher *searcher, int ignore_crc) {
    if (searcher) {
        searcher->ignore_crc = ignore_crc != 0;
    }
}

void minlz_block_searcher_set_bail_on_missing(
    minlz_block_searcher *searcher, int bail) {
    if (searcher) {
        searcher->bail_on_missing = bail != 0;
    }
}

void minlz_block_searcher_set_collect_stats(
    minlz_block_searcher *searcher, int collect) {
    if (searcher) {
        searcher->collect_stats = collect != 0;
    }
}

minlz_status minlz_block_searcher_set_max_block_size(
    minlz_block_searcher *searcher, size_t max_block_size) {
    if (!searcher ||
        max_block_size < MINLZ_MIN_BLOCK_SIZE ||
        max_block_size > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    searcher->max_block_size = max_block_size;
    return MINLZ_OK;
}

void minlz_block_searcher_set_info_callback(
    minlz_block_searcher *searcher,
    minlz_search_info_cb callback, void *callback_ctx) {
    if (searcher) {
        searcher->info_cb = callback;
        searcher->info_ctx = callback_ctx;
    }
}

minlz_status minlz_block_searcher_stats(
    const minlz_block_searcher *searcher,
    minlz_search_stats *stats) {
    if (!searcher || !stats) {
        return MINLZ_CORRUPT;
    }
    *stats = searcher->stats;
    return MINLZ_OK;
}

size_t minlz_block_searcher_window_count(
    const minlz_block_searcher *searcher) {
    return searcher ? searcher->window_count : 0u;
}

minlz_status minlz_block_searcher_window_get(
    const minlz_block_searcher *searcher, size_t index,
    minlz_search_window_stat *window) {
    if (!searcher || !window || index >= searcher->window_count) {
        return MINLZ_CORRUPT;
    }
    *window = searcher->windows[index].stat;
    return MINLZ_OK;
}

void minlz_search_stats_fprint(
    FILE *out, const minlz_search_stats *stats) {
    double total;
    double searched;
    if (!out || !stats) {
        return;
    }
    total = stats->blocks_total
                ? (double)stats->blocks_total
                : 1.0;
    searched = stats->blocks_searched
                   ? (double)stats->blocks_searched
                   : 1.0;
    fprintf(out,
            "Blocks total: %" PRIu64
            ", skipped: %" PRIu64 " (%.1f%%), deferred: %" PRIu64
            " (%.1f%%, %" PRIu64 " skipped)\n",
            stats->blocks_total, stats->blocks_skipped,
            100.0 * (double)stats->blocks_skipped / total,
            stats->blocks_deferred,
            100.0 * (double)stats->blocks_deferred / total,
            stats->blocks_deferred_skipped);
    fprintf(out,
            "Blocks searched: %" PRIu64
            " (%.1f%%), false positive: %" PRIu64 " (%.1f%%)\n",
            stats->blocks_searched,
            100.0 * (double)stats->blocks_searched / total,
            stats->blocks_false_positive,
            100.0 * (double)stats->blocks_false_positive / searched);
    if (stats->blocks_boundary_scanned != 0u) {
        fprintf(out,
                "Blocks boundary-scanned: %" PRIu64
                " (%.1f%% of searched) -- table-absent but decoded "
                "to rule out a prev-block straddle\n",
                stats->blocks_boundary_scanned,
                100.0 * (double)stats->blocks_boundary_scanned /
                    searched);
    }
    fprintf(out,
            "Bytes skipped: %" PRIu64
            " compressed, searched: %" PRIu64 " uncompressed\n",
            stats->compressed_bytes_skipped,
            stats->uncompressed_bytes_searched);
    fprintf(out,
            "Tables: %" PRIu64 " present, %" PRIu64
            " missing, %" PRIu64 " unusable\n",
            stats->tables_present, stats->tables_missing,
            stats->tables_unusable);
    if (stats->tables_present != 0u) {
        double tables = (double)stats->tables_present;
        double bits_per_byte =
            stats->uncompressed_size
                ? (double)stats->table_bytes * 8.0 /
                      (double)stats->uncompressed_size
                : 0.0;
        fprintf(out,
                "Table bits/byte: %.4f, log2: %.1f, "
                "avg reductions: %.1f\n",
                bits_per_byte,
                (double)stats->table_bits_sum / tables,
                (double)stats->table_reductions_sum / tables);
        fprintf(out,
                "Table total: %" PRIu64
                " bytes, avg %" PRIu64 " bytes/table",
                stats->table_bytes,
                stats->table_bytes / stats->tables_present);
        if (stats->uncompressed_size != 0u) {
            fprintf(out, ", %.2f%% of %" PRIu64 " uncompressed",
                    (double)stats->table_bytes * 100.0 /
                        (double)stats->uncompressed_size,
                    stats->uncompressed_size);
        }
        fputc('\n', out);
        fprintf(out,
                "Table population: avg %.1f%%, min %.1f%%, max %.1f%%\n",
                stats->table_population_sum / tables,
                stats->table_population_min,
                stats->table_population_max);
        fprintf(out,
                "Table types: %" PRIu64
                " uncompressed (0x45), %" PRIu64
                " compressed (0x46)\n",
                stats->tables_present - stats->tables_compressed,
                stats->tables_compressed);
        if (stats->tables_compressed != 0u) {
            uint64_t tabled =
                stats->compressed_blocks_total -
                stats->compressed_blocks_raw -
                stats->compressed_blocks_rle -
                stats->compressed_blocks_sparse;
            double ratio =
                stats->table_bitmap_bytes
                    ? (double)stats->tables_compressed_bytes * 100.0 /
                          (double)stats->table_bitmap_bytes
                    : 100.0;
            double share =
                tabled
                    ? (double)stats->compressed_tables_sum /
                          (double)tabled
                    : 0.0;
            fprintf(out,
                    "Compressed tables: %" PRIu64
                    " (%.1f%% of total), %" PRIu64
                    " wire bytes, %" PRIu64
                    " uncompressed bitmap bytes (%.2f%% ratio)\n",
                    stats->tables_compressed,
                    100.0 * (double)stats->tables_compressed /
                        (double)stats->tables_present,
                    stats->tables_compressed_bytes,
                    stats->table_bitmap_bytes, ratio);
            fprintf(out,
                    " Sub-blocks: %" PRIu64 " total (%" PRIu64
                    " tabled, %" PRIu64 " raw, %" PRIu64
                    " RLE, %" PRIu64 " sparse); %" PRIu64
                    " tables emitted "
                    "(share=%.2f tables/tabled-block)\n",
                    stats->compressed_blocks_total, tabled,
                    stats->compressed_blocks_raw,
                    stats->compressed_blocks_rle,
                    stats->compressed_blocks_sparse,
                    stats->compressed_tables_sum, share);
            fprintf(out,
                    " Payload bytes: tabled=%" PRIu64
                    " raw=%" PRIu64 " RLE=%" PRIu64
                    " sparse=%" PRIu64
                    "; table-header bytes=%" PRIu64 "\n",
                    stats->compressed_bytes_tabled,
                    stats->compressed_bytes_raw,
                    stats->compressed_bytes_rle,
                    stats->compressed_bytes_sparse,
                    stats->compressed_bytes_table_headers);
        }
    }
}

static void minlz_search_fprint_quoted(
    FILE *out, const uint8_t *src, size_t len) {
    static const char hex[] = "0123456789abcdef";
    size_t i;
    fputc('"', out);
    for (i = 0u; i < len; i++) {
        uint8_t value = src[i];
        switch (value) {
        case '\a':
            fputs("\\a", out);
            break;
        case '\b':
            fputs("\\b", out);
            break;
        case '\f':
            fputs("\\f", out);
            break;
        case '\n':
            fputs("\\n", out);
            break;
        case '\r':
            fputs("\\r", out);
            break;
        case '\t':
            fputs("\\t", out);
            break;
        case '\v':
            fputs("\\v", out);
            break;
        case '"':
            fputs("\\\"", out);
            break;
        case '\\':
            fputs("\\\\", out);
            break;
        default:
            if (value >= 0x20u && value <= 0x7eu) {
                fputc((int)value, out);
            } else {
                fputs("\\x", out);
                fputc(hex[value >> 4], out);
                fputc(hex[value & 15u], out);
            }
            break;
        }
    }
    fputc('"', out);
}

void minlz_search_stats_fprint_extended(
    FILE *out, const minlz_search_stats *stats,
    const minlz_search_window_stat *windows,
    size_t window_count) {
    size_t i;
    uint64_t denom;
    int has_prefix = 0;
    if (!out || !stats) {
        return;
    }
    minlz_search_stats_fprint(out, stats);
    if (!windows || window_count == 0u) {
        return;
    }
    denom = windows[0].present + windows[0].absent;
    for (i = 0u; i < window_count; i++) {
        if (windows[i].prefix_position >= 0) {
            has_prefix = 1;
            break;
        }
    }
    fprintf(out,
            "Pattern windows (per-hash presence across %" PRIu64
            " tables):\n",
            denom);
    fprintf(out,
            "  win  pat-pos  window            present            role\n");
    for (i = 0u; i < window_count; i++) {
        const char *role =
            i == 0u
                ? "gate"
                : (has_prefix &&
                           windows[i].prefix_position < 0
                       ? "raw fallback"
                       : "");
        double pct = denom
                         ? 100.0 * (double)windows[i].present /
                               (double)denom
                         : 0.0;
        int padding =
            16 - (int)windows[i].bytes_len - 2;
        fprintf(out, "  %-4zu %-8zu ", i, windows[i].position);
        minlz_search_fprint_quoted(
            out, windows[i].bytes, windows[i].bytes_len);
        if (padding < 1) {
            padding = 1;
        }
        fprintf(out, "%*s %7" PRIu64 " (%5.1f%%)   %s\n",
                padding,
                "", windows[i].present, pct, role);
    }
}

minlz_status minlz_block_searcher_search(
    minlz_block_searcher *searcher,
    const uint8_t *pattern, size_t pattern_len,
    minlz_search_match_cb callback, void *callback_ctx) {
    uint8_t header[4];
    size_t max_block = MINLZ_MAX_BLOCK_SIZE;
    int have_header = 0;
    int pending_seen = 0;
    int pending_usable = 0;
    int pending_possible = 1;
    int pending_tail_only = 0;
    int forward_active = 0;
    int64_t forward_stream_offset = 0;
    int64_t current_start = 0;
    uint64_t stream_total = 0;
    minlz_status st = MINLZ_OK;

    if (!searcher || !searcher->read_cb || !pattern ||
        pattern_len == 0u) {
        return MINLZ_CORRUPT;
    }
    memset(&searcher->stats, 0, sizeof(searcher->stats));
    searcher->previous_len = 0;
    minlz_searcher_clear_boundary_tail(searcher);
    searcher->forward_context_len = 0u;
    minlz_searcher_clear_lazy(searcher);
    searcher->tail_rescue = 0;
    searcher->window_count = 0u;
    searcher->tables_tallied = 0u;
    searcher->windows_initialized = 0;
    searcher->windows_compatible = 1;

    for (;;) {
        size_t got = 0;
        uint8_t chunk_type;
        size_t chunk_len;
        st = minlz_read_exact(searcher->read_cb, searcher->read_ctx,
                              header, sizeof(header), 1, &got);
        if (st != MINLZ_OK) {
            return st;
        }
        if (got == 0u) {
            st = minlz_searcher_rescue_tail(
                searcher, pattern, pattern_len,
                callback, callback_ctx);
            if (st == MINLZ_SEARCH_STOP) {
                return MINLZ_OK;
            }
            if (st != MINLZ_OK) {
                return st;
            }
            if (forward_active) {
                st = minlz_searcher_forward_eof(
                    callback, callback_ctx,
                    searcher->forward_context,
                    searcher->forward_context_len,
                    searcher->forward_context_start,
                    forward_stream_offset);
                if (st == MINLZ_SEARCH_STOP) {
                    return MINLZ_OK;
                }
                if (st != MINLZ_OK) {
                    return st;
                }
            }
            return MINLZ_OK;
        }
        chunk_type = header[0];
        chunk_len = (size_t)header[1] |
                    ((size_t)header[2] << 8) |
                    ((size_t)header[3] << 16);

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            if (have_header || chunk_len != 6u) {
                return MINLZ_CORRUPT;
            }
            st = minlz_searcher_read_payload(searcher, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            if (memcmp(searcher->payload, "MinLz", 5u) != 0) {
                return MINLZ_UNSUPPORTED;
            }
            if ((searcher->payload[5] >> 6) != 0u ||
                (searcher->payload[5] & 15u) > 13u) {
                return MINLZ_CORRUPT;
            }
            max_block = (size_t)1u
                        << ((searcher->payload[5] & 15u) + 10u);
            if (max_block > searcher->max_block_size) {
                return MINLZ_TOO_LARGE;
            }
            have_header = 1;
            pending_seen = 0;
            pending_usable = 0;
            pending_possible = 1;
            pending_tail_only = 0;
            current_start = 0;
            stream_total = 0;
            searcher->previous_len = 0;
            minlz_searcher_clear_boundary_tail(searcher);
            searcher->forward_context_len = 0u;
            minlz_searcher_clear_lazy(searcher);
            continue;
        }
        if (!have_header) {
            if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
                (chunk_type >= 0xc0u &&
                 chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
                return MINLZ_CORRUPT;
            }
            st = minlz_skip_exact(
                searcher->read_cb, searcher->read_ctx, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_SEARCH_INFO) {
            minlz_search_config config;
            size_t used = 0;
            st = minlz_searcher_read_payload(searcher, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            st = minlz_search_parse_info(
                searcher->payload, chunk_len, &config, &used);
            if (st == MINLZ_OK && used == chunk_len &&
                searcher->info_cb) {
                searcher->info_cb(searcher->info_ctx, &config);
            }
            if (st != MINLZ_OK || used != chunk_len) {
                return st == MINLZ_OK ? MINLZ_CORRUPT : st;
            }
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_SEARCH_TABLE) {
            minlz_search_table_view view;
            int can_use = 0;
            int possible;
            st = minlz_searcher_read_payload(searcher, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            searcher->stats.tables_present++;
            searcher->stats.table_bytes +=
                MINLZ_CHUNK_HEADER_SIZE + chunk_len;
            pending_seen = 1;
            st = minlz_search_parse_table(
                searcher->payload, chunk_len,
                searcher->ignore_crc, &view);
            if (st == MINLZ_CRC && !searcher->ignore_crc) {
                return st;
            }
            if (st != MINLZ_OK) {
                searcher->stats.tables_unusable++;
                continue;
            }
            st = minlz_searcher_tally_table(
                searcher, &view, pattern, pattern_len,
                0, MINLZ_CHUNK_HEADER_SIZE + chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            possible = minlz_search_pattern_might_match(
                &view, pattern, pattern_len, &can_use);
            if (!can_use) {
                searcher->stats.tables_unusable++;
                continue;
            }
            pending_usable = 1;
            if (!possible) {
                int tail_sensitive =
                    minlz_search_reject_needs_tail_rescue(
                        &view, pattern, pattern_len);
                pending_tail_only = pending_possible
                                        ? tail_sensitive
                                        : pending_tail_only &&
                                              tail_sensitive;
                pending_possible = 0;
            }
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED) {
            minlz_search_table_view view;
            uint8_t *table_storage = NULL;
            int can_use = 0;
            int possible;
            st = minlz_searcher_read_payload(searcher, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            searcher->stats.tables_present++;
            searcher->stats.tables_compressed++;
            searcher->stats.table_bytes +=
                MINLZ_CHUNK_HEADER_SIZE + chunk_len;
            pending_seen = 1;
            st = minlz_search_parse_table_compressed(
                searcher->payload, chunk_len,
                searcher->ignore_crc, &table_storage, &view);
            if (st == MINLZ_CRC && !searcher->ignore_crc) {
                return st;
            }
            if (st != MINLZ_OK) {
                searcher->stats.tables_unusable++;
                free(table_storage);
                continue;
            }
            st = minlz_searcher_tally_table(
                searcher, &view, pattern, pattern_len,
                1, MINLZ_CHUNK_HEADER_SIZE + chunk_len);
            if (st != MINLZ_OK) {
                free(table_storage);
                return st;
            }
            possible = minlz_search_pattern_might_match(
                &view, pattern, pattern_len, &can_use);
            if (!can_use) {
                searcher->stats.tables_unusable++;
                free(table_storage);
                continue;
            }
            pending_usable = 1;
            if (!possible) {
                int tail_sensitive =
                    minlz_search_reject_needs_tail_rescue(
                        &view, pattern, pattern_len);
                pending_tail_only = pending_possible
                                        ? tail_sensitive
                                        : pending_tail_only &&
                                              tail_sensitive;
                pending_possible = 0;
            }
            free(table_storage);
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_EOF) {
            uint64_t expected = 0;
            size_t used = 0;
            st = minlz_searcher_read_payload(searcher, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            if (chunk_len != 0u) {
                st = minlz_search_read_u64varint(
                    searcher->payload, chunk_len, &expected, &used);
                if (st != MINLZ_OK || used != chunk_len ||
                    expected != stream_total) {
                    return MINLZ_CORRUPT;
                }
            }
            st = minlz_searcher_rescue_tail(
                searcher, pattern, pattern_len,
                callback, callback_ctx);
            if (st == MINLZ_SEARCH_STOP) {
                return MINLZ_OK;
            }
            if (st != MINLZ_OK) {
                return st;
            }
            if (forward_active) {
                st = minlz_searcher_forward_eof(
                    callback, callback_ctx,
                    searcher->forward_context,
                    searcher->forward_context_len,
                    searcher->forward_context_start,
                    forward_stream_offset);
                if (st == MINLZ_SEARCH_STOP) {
                    return MINLZ_OK;
                }
                if (st != MINLZ_OK) {
                    return st;
                }
            }
            have_header = 0;
            pending_seen = 0;
            pending_usable = 0;
            pending_possible = 1;
            pending_tail_only = 0;
            searcher->previous_len = 0;
            minlz_searcher_clear_boundary_tail(searcher);
            searcher->forward_context_len = 0u;
            minlz_searcher_clear_lazy(searcher);
            forward_active = 0;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED ||
            chunk_type == MINLZ_CHUNK_MINLZ ||
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            uint8_t *decoded = NULL;
            size_t decoded_len = 0;
            int skip;
            int need_boundary;
            uint64_t found = 0;
            int forward_requested = 0;
            int64_t next_forward_offset = 0;
            minlz_search_lazy_callback lazy_callback;
            minlz_search_match_cb scan_callback = callback;
            void *scan_callback_ctx = callback_ctx;

            /*
             * A previously held candidate now has a successor, so it was
             * indexed with forward overlap and its all-zero table is final.
             */
            searcher->tail_rescue = 0;
            st = minlz_searcher_read_payload(searcher, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            st = minlz_searcher_data_len(
                chunk_type, searcher->payload, chunk_len,
                max_block, &decoded_len);
            if (st != MINLZ_OK) {
                return st;
            }
            searcher->stats.blocks_total++;
            searcher->stats.uncompressed_size += decoded_len;
            if (!pending_seen) {
                searcher->stats.tables_missing++;
            }
            if ((!pending_seen || !pending_usable) &&
                searcher->bail_on_missing) {
                return MINLZ_SEARCH_TABLES_UNUSABLE;
            }
            skip = pending_usable && !pending_possible;
            need_boundary = skip &&
                            searcher->boundary_tail_len != 0u &&
                            pattern_len > 1u;
            if (skip && !need_boundary && !forward_active) {
                if (pending_tail_only) {
                    st = minlz_searcher_hold_tail(
                        searcher, chunk_type, chunk_len,
                        decoded_len, max_block, current_start);
                    if (st != MINLZ_OK) {
                        return st;
                    }
                }
                st = minlz_searcher_hold_lazy(
                    searcher, chunk_type, chunk_len,
                    decoded_len, max_block, current_start);
                if (st != MINLZ_OK) {
                    return st;
                }
                searcher->stats.blocks_skipped++;
                searcher->stats.compressed_bytes_skipped +=
                    MINLZ_CHUNK_HEADER_SIZE + chunk_len;
                searcher->previous_len = 0;
                minlz_searcher_clear_boundary_tail(searcher);
                current_start += (int64_t)decoded_len;
                stream_total += decoded_len;
                pending_seen = 0;
                pending_usable = 0;
                pending_possible = 1;
                pending_tail_only = 0;
                continue;
            }

            st = minlz_searcher_decode_data(
                searcher, chunk_type, chunk_len, max_block,
                &decoded, &decoded_len);
            if (st != MINLZ_OK) {
                return st;
            }
            searcher->stats.blocks_searched++;
            searcher->stats.uncompressed_bytes_searched += decoded_len;
            if (need_boundary) {
                searcher->stats.blocks_boundary_scanned++;
            }

            if (forward_active && callback) {
                minlz_search_result result;
                minlz_status callback_st;
                memset(&result, 0, sizeof(result));
                result.previous_block =
                    searcher->forward_context_len
                        ? searcher->forward_context
                        : NULL;
                result.previous_block_len =
                    searcher->forward_context_len;
                result.block = decoded;
                result.block_len = decoded_len;
                result.offset =
                    searcher->forward_context_offset;
                result.stream_offset = forward_stream_offset;
                result.block_start =
                    searcher->forward_context_start;
                callback_st = callback(callback_ctx, &result);
                if (callback_st == MINLZ_SEARCH_STOP) {
                    return MINLZ_OK;
                }
                if (callback_st == MINLZ_SEARCH_FORWARD) {
                    st = minlz_searcher_set_forward_context(
                        searcher, decoded, decoded_len,
                        searcher->forward_context_start,
                        searcher->forward_context_offset);
                    if (st != MINLZ_OK) {
                        return st;
                    }
                    forward_requested = 1;
                    next_forward_offset = forward_stream_offset;
                } else if (callback_st != MINLZ_OK) {
                    return callback_st;
                }
            }

            if (searcher->lazy_valid && callback) {
                lazy_callback.callback = callback;
                lazy_callback.callback_ctx = callback_ctx;
                lazy_callback.loader = minlz_searcher_load_lazy;
                lazy_callback.loader_ctx = searcher;
                lazy_callback.previous_len =
                    searcher->lazy_decoded_len;
                lazy_callback.previous_start =
                    searcher->lazy_start;
                lazy_callback.current_start = current_start;
                scan_callback = minlz_searcher_lazy_callback;
                scan_callback_ctx = &lazy_callback;
            }
            st = minlz_searcher_scan_block(
                searcher, pattern, pattern_len,
                decoded, decoded_len, current_start,
                scan_callback, scan_callback_ctx, &found,
                &forward_requested, &next_forward_offset);
            if (st == MINLZ_SEARCH_STOP) {
                return MINLZ_OK;
            }
            if (st != MINLZ_OK) {
                return st;
            }
            if (pending_usable && pending_possible && found == 0u) {
                searcher->stats.blocks_false_positive++;
            }

            st = minlz_searcher_reserve(
                &searcher->previous, &searcher->previous_cap,
                decoded_len ? decoded_len : 1u);
            if (st != MINLZ_OK) {
                return st;
            }
            memcpy(searcher->previous, decoded, decoded_len);
            searcher->previous_len = decoded_len;
            searcher->previous_start = current_start;
            minlz_searcher_clear_lazy(searcher);
            current_start += (int64_t)decoded_len;
            stream_total += decoded_len;
            forward_active = forward_requested;
            if (forward_requested) {
                forward_stream_offset = next_forward_offset;
            } else {
                searcher->forward_context_len = 0u;
            }
            pending_seen = 0;
            pending_usable = 0;
            pending_possible = 1;
            pending_tail_only = 0;
            continue;
        }

        if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
            (chunk_type >= 0xc0u &&
             chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            return MINLZ_UNSUPPORTED;
        }
        st = minlz_skip_exact(
            searcher->read_cb, searcher->read_ctx, chunk_len);
        if (st != MINLZ_OK) {
            return st;
        }
    }
}

static minlz_status minlz_search_write_chunk(
    minlz_write_cb write_cb, void *write_ctx,
    uint8_t type, const uint8_t *payload, size_t payload_len,
    int64_t *written) {
    uint8_t header[4];
    minlz_status st;
    if (!write_cb || payload_len > MINLZ_MAX_USER_CHUNK_SIZE) {
        return MINLZ_CORRUPT;
    }
    header[0] = type;
    header[1] = (uint8_t)payload_len;
    header[2] = (uint8_t)(payload_len >> 8);
    header[3] = (uint8_t)(payload_len >> 16);
    st = minlz_write_all(write_cb, write_ctx, header, sizeof(header));
    if (st == MINLZ_OK && payload_len != 0u) {
        st = minlz_write_all(
            write_cb, write_ctx, payload, payload_len);
    }
    if (st == MINLZ_OK && written) {
        *written += (int64_t)(sizeof(header) + payload_len);
    }
    return st;
}

static minlz_status minlz_search_write_stream_header(
    minlz_write_cb write_cb, void *write_ctx,
    size_t max_block, int64_t *written) {
    uint8_t payload[6] = {'M', 'i', 'n', 'L', 'z', 0};
    unsigned log2 = minlz_search_ceil_log2(max_block);
    if (log2 < 10u || log2 > 23u) {
        return MINLZ_TOO_LARGE;
    }
    payload[5] = (uint8_t)(log2 - 10u);
    return minlz_search_write_chunk(
        write_cb, write_ctx, MINLZ_CHUNK_STREAM_ID,
        payload, sizeof(payload), written);
}

static minlz_status minlz_search_write_remote_ref(
    minlz_write_cb write_cb, void *write_ctx,
    uint64_t offset, size_t max_block, size_t actual,
    int64_t *written) {
    uint8_t payload[20];
    size_t n = 0;
    if (actual == 0u || actual > max_block) {
        return MINLZ_CORRUPT;
    }
    n += minlz_put_uvarint(payload + n, offset);
    n += minlz_put_uvarint(
        payload + n, (uint64_t)(max_block - actual));
    return minlz_search_write_chunk(
        write_cb, write_ctx, MINLZ_CHUNK_REMOTE_BLOCK_REF,
        payload, n, written);
}

static minlz_status minlz_search_write_eof(
    minlz_write_cb write_cb, void *write_ctx,
    uint64_t size, int64_t *written) {
    uint8_t payload[10];
    size_t n = minlz_put_uvarint(payload, size);
    return minlz_search_write_chunk(
        write_cb, write_ctx, MINLZ_CHUNK_EOF,
        payload, n, written);
}

static minlz_status minlz_search_read_first_header(
    minlz_read_cb read_cb, void *read_ctx,
    uint8_t header[4], uint8_t body[6],
    size_t *max_block, int64_t *consumed) {
    size_t got = 0;
    minlz_status st = minlz_read_exact(
        read_cb, read_ctx, header, 4u, 0, &got);
    if (st != MINLZ_OK) {
        return st;
    }
    if (got != 4u || header[0] != MINLZ_CHUNK_STREAM_ID ||
        header[1] != 6u || header[2] != 0u || header[3] != 0u) {
        return MINLZ_CORRUPT;
    }
    st = minlz_read_exact(read_cb, read_ctx, body, 6u, 0, NULL);
    if (st != MINLZ_OK) {
        return st;
    }
    if (memcmp(body, "MinLz", 5u) != 0) {
        return MINLZ_UNSUPPORTED;
    }
    if ((body[5] >> 6) != 0u || (body[5] & 15u) > 13u) {
        return MINLZ_CORRUPT;
    }
    *max_block = (size_t)1u << ((body[5] & 15u) + 10u);
    if (consumed) {
        *consumed = 10;
    }
    return MINLZ_OK;
}

static minlz_status minlz_search_decode_owned_block(
    minlz_workspace *workspace, uint8_t chunk_type,
    const uint8_t *payload, size_t payload_len,
    size_t max_block, int ignore_crc,
    uint8_t **owned, size_t *owned_len) {
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    minlz_status st;
    if (!owned || !owned_len || payload_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    *owned = NULL;
    *owned_len = 0;
    if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        uint32_t expected_crc = minlz_load32(payload);
        decoded = (uint8_t *)payload + MINLZ_CHECKSUM_SIZE;
        decoded_len = payload_len - MINLZ_CHECKSUM_SIZE;
        if (decoded_len > max_block) {
            return MINLZ_TOO_LARGE;
        }
        if (!ignore_crc &&
            minlz_crc(decoded, decoded_len) != expected_crc) {
            return MINLZ_CRC;
        }
    } else {
        st = minlz_decode_stream_chunk(
            workspace, payload, payload_len,
            &decoded, &decoded_len,
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC,
            ignore_crc, max_block);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    *owned = (uint8_t *)malloc(decoded_len ? decoded_len : 1u);
    if (!*owned) {
        return MINLZ_IO;
    }
    memcpy(*owned, decoded, decoded_len);
    *owned_len = decoded_len;
    return MINLZ_OK;
}

static minlz_status minlz_build_sidecar_emit_block(
    minlz_write_cb write_cb, void *write_ctx,
    const minlz_search_config *configs, size_t config_count,
    const uint8_t *block, size_t block_len,
    const uint8_t *overlap, size_t overlap_len,
    uint64_t block_offset, size_t max_block,
    int64_t *written) {
    size_t i;
    for (i = 0; i < config_count; i++) {
        size_t config_overlap =
            minlz_search_overlap_bytes(configs + i);
        uint8_t *chunk = NULL;
        size_t chunk_len = 0;
        minlz_status st;
        if (config_overlap > overlap_len) {
            config_overlap = overlap_len;
        }
        st = minlz_search_make_table_chunk(
            configs + i, block, block_len,
            config_overlap ? overlap : NULL, config_overlap,
            &chunk, &chunk_len);
        if (st == MINLZ_UNSUPPORTED) {
            continue;
        }
        if (st != MINLZ_OK) {
            return st;
        }
        st = minlz_write_all(write_cb, write_ctx, chunk, chunk_len);
        if (st == MINLZ_OK && written) {
            *written += (int64_t)chunk_len;
        }
        free(chunk);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    return minlz_search_write_remote_ref(
        write_cb, write_ctx, block_offset,
        max_block, block_len, written);
}

static minlz_status minlz_build_sidecar_begin_stream(
    minlz_write_cb write_cb, void *write_ctx,
    const minlz_search_config *configs,
    minlz_search_config *resolved, size_t config_count,
    size_t max_block, size_t *max_overlap,
    int64_t *written) {
    size_t i;
    minlz_status st;

    *max_overlap = 0u;
    for (i = 0; i < config_count; i++) {
        size_t overlap;
        st = minlz_search_config_resolve(
            resolved + i, configs + i, max_block);
        if (st != MINLZ_OK) {
            return st;
        }
        overlap = minlz_search_overlap_bytes(resolved + i);
        if (overlap > *max_overlap) {
            *max_overlap = overlap;
        }
    }

    st = minlz_search_write_stream_header(
        write_cb, write_ctx, max_block, written);
    if (st != MINLZ_OK) {
        return st;
    }
    for (i = 0; i < config_count; i++) {
        uint8_t *info = NULL;
        size_t info_len = 0;
        st = minlz_search_make_info_chunk(
            resolved + i, &info, &info_len);
        if (st == MINLZ_OK) {
            st = minlz_write_all(
                write_cb, write_ctx, info, info_len);
            if (st == MINLZ_OK) {
                *written += (int64_t)info_len;
            }
        }
        free(info);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    return MINLZ_OK;
}

minlz_status minlz_build_sidecar(
    minlz_write_cb sidecar_write_cb, void *sidecar_write_ctx,
    minlz_read_cb stream_read_cb, void *stream_read_ctx,
    const minlz_search_config *configs, size_t config_count,
    int ignore_crc) {
    uint8_t header[4];
    uint8_t body[6];
    uint8_t *payload = NULL;
    size_t payload_cap = 0;
    uint8_t *previous = NULL;
    size_t previous_len = 0;
    uint64_t previous_offset = 0;
    minlz_search_config *resolved = NULL;
    minlz_workspace *workspace = NULL;
    size_t max_block = 0;
    size_t max_overlap = 0;
    int64_t consumed = 0;
    int64_t sidecar_written = 0;
    uint64_t total = 0;
    int stream_open = 1;
    minlz_status st;

    if (!sidecar_write_cb || !stream_read_cb ||
        !configs || config_count == 0u) {
        return MINLZ_CORRUPT;
    }
    st = minlz_search_read_first_header(
        stream_read_cb, stream_read_ctx,
        header, body, &max_block, &consumed);
    if (st != MINLZ_OK) {
        return st;
    }
    resolved = (minlz_search_config *)calloc(
        config_count, sizeof(*resolved));
    if (!resolved) {
        return MINLZ_IO;
    }
    st = minlz_workspace_create(&workspace);
    if (st != MINLZ_OK) {
        goto done;
    }
    st = minlz_build_sidecar_begin_stream(
        sidecar_write_cb, sidecar_write_ctx,
        configs, resolved, config_count,
        max_block, &max_overlap, &sidecar_written);
    if (st != MINLZ_OK) {
        goto done;
    }

    for (;;) {
        size_t got = 0;
        uint8_t chunk_type;
        size_t chunk_len;
        uint64_t chunk_offset = (uint64_t)consumed;
        st = minlz_read_exact(
            stream_read_cb, stream_read_ctx,
            header, sizeof(header), 1, &got);
        if (st != MINLZ_OK) {
            goto done;
        }
        if (got == 0u) {
            if (stream_open) {
                if (previous) {
                    st = minlz_build_sidecar_emit_block(
                        sidecar_write_cb, sidecar_write_ctx,
                        resolved, config_count,
                        previous, previous_len, NULL, 0,
                        previous_offset, max_block,
                        &sidecar_written);
                    if (st != MINLZ_OK) {
                        goto done;
                    }
                    free(previous);
                    previous = NULL;
                    previous_len = 0;
                }
                st = minlz_search_write_eof(
                    sidecar_write_cb, sidecar_write_ctx, 0,
                    &sidecar_written);
            } else {
                st = MINLZ_OK;
            }
            goto done;
        }
        chunk_type = header[0];
        chunk_len = (size_t)header[1] |
                    ((size_t)header[2] << 8) |
                    ((size_t)header[3] << 16);
        consumed += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + chunk_len);
        st = minlz_searcher_reserve(
            &payload, &payload_cap, chunk_len ? chunk_len : 1u);
        if (st != MINLZ_OK) {
            goto done;
        }
        st = minlz_read_exact(
            stream_read_cb, stream_read_ctx,
            payload, chunk_len, 0, NULL);
        if (st != MINLZ_OK) {
            goto done;
        }

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            if (stream_open || previous || chunk_len != 6u ||
                memcmp(payload, "MinLz", 5u) != 0) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            if ((payload[5] >> 6) != 0u ||
                (payload[5] & 15u) > 13u) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            max_block =
                (size_t)1u << ((payload[5] & 15u) + 10u);
            st = minlz_build_sidecar_begin_stream(
                sidecar_write_cb, sidecar_write_ctx,
                configs, resolved, config_count,
                max_block, &max_overlap, &sidecar_written);
            if (st != MINLZ_OK) {
                goto done;
            }
            total = 0u;
            stream_open = 1;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED ||
            chunk_type == MINLZ_CHUNK_MINLZ ||
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            uint8_t *current = NULL;
            size_t current_len = 0;
            if (!stream_open) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            st = minlz_search_decode_owned_block(
                workspace, chunk_type, payload, chunk_len,
                max_block, ignore_crc, &current, &current_len);
            if (st != MINLZ_OK) {
                free(current);
                goto done;
            }
            if (previous) {
                size_t overlap_len =
                    current_len < max_overlap ? current_len : max_overlap;
                st = minlz_build_sidecar_emit_block(
                    sidecar_write_cb, sidecar_write_ctx,
                    resolved, config_count,
                    previous, previous_len,
                    overlap_len ? current : NULL, overlap_len,
                    previous_offset, max_block, &sidecar_written);
                if (st != MINLZ_OK) {
                    free(current);
                    goto done;
                }
                free(previous);
            }
            previous = current;
            previous_len = current_len;
            previous_offset = chunk_offset;
            total += current_len;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_EOF) {
            uint64_t expected = 0;
            size_t used = 0;
            if (!stream_open) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            if (chunk_len != 0u) {
                st = minlz_search_read_u64varint(
                    payload, chunk_len, &expected, &used);
                if (st != MINLZ_OK || used != chunk_len ||
                    expected != total) {
                    st = MINLZ_CORRUPT;
                    goto done;
                }
            }
            if (previous) {
                st = minlz_build_sidecar_emit_block(
                    sidecar_write_cb, sidecar_write_ctx,
                    resolved, config_count,
                    previous, previous_len, NULL, 0,
                    previous_offset, max_block, &sidecar_written);
                if (st != MINLZ_OK) {
                    goto done;
                }
                free(previous);
                previous = NULL;
                previous_len = 0;
            }
            st = minlz_search_write_eof(
                sidecar_write_cb, sidecar_write_ctx, 0,
                &sidecar_written);
            if (st != MINLZ_OK) {
                goto done;
            }
            total = 0u;
            stream_open = 0;
            continue;
        }

        if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
            (chunk_type >= 0xc0u &&
             chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            st = MINLZ_UNSUPPORTED;
            goto done;
        }
    }

done:
    free(previous);
    free(payload);
    free(resolved);
    minlz_workspace_destroy(workspace);
    return st;
}

minlz_status minlz_extract_sidecar(
    minlz_write_cb sidecar_write_cb, void *sidecar_write_ctx,
    minlz_write_cb stripped_write_cb, void *stripped_write_ctx,
    minlz_read_cb stream_read_cb, void *stream_read_ctx) {
    uint8_t header[4];
    uint8_t body[6];
    uint8_t *payload = NULL;
    size_t payload_cap = 0;
    size_t max_block = 0;
    int64_t consumed = 0;
    int64_t sidecar_written = 0;
    int64_t stripped_written = 0;
    int64_t stripped_uncompressed = 0;
    minlz_index_builder stripped_index;
    int have_stripped_index = 0;
    int stream_open = 1;
    minlz_status st;

    if (!sidecar_write_cb || !stream_read_cb) {
        return MINLZ_CORRUPT;
    }
    st = minlz_search_read_first_header(
        stream_read_cb, stream_read_ctx,
        header, body, &max_block, &consumed);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_search_write_stream_header(
        sidecar_write_cb, sidecar_write_ctx,
        max_block, &sidecar_written);
    if (st != MINLZ_OK) {
        return st;
    }
    if (stripped_write_cb) {
        st = minlz_search_write_stream_header(
            stripped_write_cb, stripped_write_ctx,
            max_block, &stripped_written);
        if (st != MINLZ_OK) {
            return st;
        }
        minlz_index_init(&stripped_index, max_block);
        have_stripped_index = 1;
    }

    for (;;) {
        size_t got = 0;
        uint8_t chunk_type;
        size_t chunk_len;
        uint64_t source_offset = (uint64_t)consumed;
        st = minlz_read_exact(
            stream_read_cb, stream_read_ctx,
            header, sizeof(header), 1, &got);
        if (st != MINLZ_OK) {
            goto extract_done;
        }
        if (got == 0u) {
            st = stream_open ? MINLZ_CORRUPT : MINLZ_OK;
            goto extract_done;
        }
        chunk_type = header[0];
        chunk_len = (size_t)header[1] |
                    ((size_t)header[2] << 8) |
                    ((size_t)header[3] << 16);
        consumed += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + chunk_len);
        st = minlz_searcher_reserve(
            &payload, &payload_cap, chunk_len ? chunk_len : 1u);
        if (st != MINLZ_OK) {
            goto extract_done;
        }
        st = minlz_read_exact(
            stream_read_cb, stream_read_ctx,
            payload, chunk_len, 0, NULL);
        if (st != MINLZ_OK) {
            goto extract_done;
        }

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            if (stream_open || chunk_len != 6u ||
                memcmp(payload, "MinLz", 5u) != 0 ||
                (payload[5] >> 6) != 0u ||
                (payload[5] & 15u) > 13u) {
                st = MINLZ_CORRUPT;
                goto extract_done;
            }
            max_block =
                (size_t)1u << ((payload[5] & 15u) + 10u);
            st = minlz_search_write_stream_header(
                sidecar_write_cb, sidecar_write_ctx,
                max_block, &sidecar_written);
            if (st != MINLZ_OK) {
                goto extract_done;
            }
            if (stripped_write_cb) {
                st = minlz_search_write_stream_header(
                    stripped_write_cb, stripped_write_ctx,
                    max_block, &stripped_written);
                if (st != MINLZ_OK) {
                    goto extract_done;
                }
                minlz_index_init(&stripped_index, max_block);
                have_stripped_index = 1;
            }
            stripped_uncompressed = 0;
            stream_open = 1;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_SEARCH_INFO ||
            chunk_type == MINLZ_CHUNK_SEARCH_TABLE ||
            chunk_type == MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED) {
            st = minlz_search_write_chunk(
                sidecar_write_cb, sidecar_write_ctx,
                chunk_type, payload, chunk_len, &sidecar_written);
            if (st != MINLZ_OK) {
                goto extract_done;
            }
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED ||
            chunk_type == MINLZ_CHUNK_MINLZ ||
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            size_t decoded_len = 0;
            uint64_t ref_offset = source_offset;
            if (!stream_open) {
                st = MINLZ_CORRUPT;
                goto extract_done;
            }
            st = minlz_searcher_data_len(
                chunk_type, payload, chunk_len,
                max_block, &decoded_len);
            if (st != MINLZ_OK || decoded_len == 0u) {
                if (st == MINLZ_OK) {
                    st = MINLZ_CORRUPT;
                }
                goto extract_done;
            }
            if (stripped_write_cb) {
                ref_offset = (uint64_t)stripped_written;
                st = minlz_index_add(
                    &stripped_index, stripped_written,
                    stripped_uncompressed);
                if (st == MINLZ_OK) {
                    st = minlz_search_write_chunk(
                        stripped_write_cb, stripped_write_ctx,
                        chunk_type, payload, chunk_len,
                        &stripped_written);
                }
                if (st != MINLZ_OK) {
                    goto extract_done;
                }
                stripped_uncompressed += (int64_t)decoded_len;
            }
            st = minlz_search_write_remote_ref(
                sidecar_write_cb, sidecar_write_ctx,
                ref_offset, max_block, decoded_len,
                &sidecar_written);
            if (st != MINLZ_OK) {
                goto extract_done;
            }
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_EOF) {
            uint8_t *index_bytes = NULL;
            size_t index_len = 0;
            size_t index_cap = 0;
            if (!stream_open) {
                st = MINLZ_CORRUPT;
                goto extract_done;
            }
            st = minlz_search_write_eof(
                sidecar_write_cb, sidecar_write_ctx,
                0, &sidecar_written);
            if (st != MINLZ_OK) {
                goto extract_done;
            }
            if (stripped_write_cb) {
                st = minlz_search_write_eof(
                    stripped_write_cb, stripped_write_ctx,
                    (uint64_t)stripped_uncompressed,
                    &stripped_written);
                if (st == MINLZ_OK) {
                    st = minlz_index_append(
                        &index_bytes, &index_len, &index_cap,
                        &stripped_index, stripped_uncompressed, -1);
                }
                if (st == MINLZ_OK && index_len != 0u) {
                    st = minlz_write_all(
                        stripped_write_cb, stripped_write_ctx,
                        index_bytes, index_len);
                    if (st == MINLZ_OK) {
                        stripped_written += (int64_t)index_len;
                    }
                }
                free(index_bytes);
                if (st != MINLZ_OK) {
                    goto extract_done;
                }
                minlz_index_free(&stripped_index);
                have_stripped_index = 0;
            }
            stripped_uncompressed = 0;
            stream_open = 0;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_INDEX) {
            continue;
        }
        if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
            (chunk_type >= 0xc0u &&
             chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            st = MINLZ_UNSUPPORTED;
            goto extract_done;
        }
        if (stripped_write_cb) {
            st = minlz_search_write_chunk(
                stripped_write_cb, stripped_write_ctx,
                chunk_type, payload, chunk_len,
                &stripped_written);
            if (st != MINLZ_OK) {
                goto extract_done;
            }
        }
    }

extract_done:
    if (have_stripped_index) {
        minlz_index_free(&stripped_index);
    }
    free(payload);
    return st;
}

struct minlz_sidecar_searcher {
    minlz_read_at_cb main_read_at_cb;
    void *main_read_at_ctx;
    minlz_read_cb sidecar_read_cb;
    void *sidecar_read_ctx;
    minlz_block_searcher scan;
    uint8_t *main_payload;
    size_t main_payload_cap;
    uint8_t *read_ahead;
    size_t read_ahead_cap;
    size_t read_ahead_len;
    size_t read_ahead_target;
    uint64_t read_ahead_offset;
    int read_ahead_valid;
    uint8_t *lazy_payload;
    size_t lazy_payload_cap;
    minlz_workspace *lazy_workspace;
    const uint8_t *lazy_decoded;
    size_t lazy_loaded_len;
    uint64_t lazy_offset;
    size_t lazy_decoded_len;
    size_t lazy_max_block;
    int64_t lazy_start;
    int lazy_valid;
    uint64_t tail_offset;
    size_t tail_decoded_len;
    size_t tail_max_block;
    size_t tail_wire_len;
    int64_t tail_start;
    int tail_rescue;
    int forward_active;
    int64_t forward_stream_offset;
};

#define MINLZ_SIDECAR_READ_AHEAD_MIN (64u << 10)
#define MINLZ_SIDECAR_READ_AHEAD_MAX (16u << 20)

static size_t minlz_sidecar_read_ahead_size(size_t max_block) {
    size_t one = minlz_max_encoded_len(max_block);
    size_t target;
    if (one == 0u || one > SIZE_MAX - 16u) {
        return MINLZ_SIDECAR_READ_AHEAD_MIN;
    }
    one += 16u;
    if (one > MINLZ_SIDECAR_READ_AHEAD_MAX / 4u) {
        target = MINLZ_SIDECAR_READ_AHEAD_MAX;
    } else {
        target = one * 4u;
    }
    if (target < MINLZ_SIDECAR_READ_AHEAD_MIN) {
        target = MINLZ_SIDECAR_READ_AHEAD_MIN;
    }
    return target;
}

static minlz_status minlz_sidecar_read_at_exact(
    minlz_sidecar_searcher *searcher, int64_t offset,
    uint8_t *dst, size_t len) {
    size_t done = 0;
    if (offset < 0 || (!dst && len != 0u)) {
        return MINLZ_CORRUPT;
    }
    while (done < len) {
        uint64_t absolute;
        size_t available = 0u;
        size_t relative = 0u;
        size_t request;
        size_t nread = 0;
        minlz_status st;
        if ((uint64_t)offset > (uint64_t)INT64_MAX - done) {
            return MINLZ_TOO_LARGE;
        }
        absolute = (uint64_t)offset + done;
        if (searcher->read_ahead_valid &&
            absolute >= searcher->read_ahead_offset) {
            uint64_t delta = absolute - searcher->read_ahead_offset;
            if (delta <= searcher->read_ahead_len) {
                relative = (size_t)delta;
                available = searcher->read_ahead_len - relative;
            }
        }
        if (available != 0u) {
            size_t take = len - done;
            if (take > available) {
                take = available;
            }
            memcpy(dst + done, searcher->read_ahead + relative, take);
            done += take;
            continue;
        }
        request = len - done;
        if (request < searcher->read_ahead_target) {
            request = searcher->read_ahead_target;
        }
        if (request == 0u) {
            request = MINLZ_SIDECAR_READ_AHEAD_MIN;
        }
        st = minlz_searcher_reserve(
            &searcher->read_ahead, &searcher->read_ahead_cap, request);
        if (st != MINLZ_OK) {
            return st;
        }
        st = searcher->main_read_at_cb(
            searcher->main_read_at_ctx, (int64_t)absolute,
            searcher->read_ahead, request, &nread);
        if (st != MINLZ_OK) {
            return st;
        }
        if (nread == 0u || nread > request) {
            return MINLZ_IO;
        }
        searcher->read_ahead_offset = absolute;
        searcher->read_ahead_len = nread;
        searcher->read_ahead_valid = 1;
    }
    return MINLZ_OK;
}

static minlz_status minlz_sidecar_main_header(
    minlz_sidecar_searcher *searcher, uint64_t offset,
    uint8_t *chunk_type, size_t *chunk_len) {
    uint8_t header[4];
    minlz_status st;
    if (offset > (uint64_t)INT64_MAX) {
        return MINLZ_TOO_LARGE;
    }
    st = minlz_sidecar_read_at_exact(
        searcher, (int64_t)offset, header, sizeof(header));
    if (st != MINLZ_OK) {
        return st;
    }
    *chunk_type = header[0];
    *chunk_len = (size_t)header[1] |
                 ((size_t)header[2] << 8) |
                 ((size_t)header[3] << 16);
    if (*chunk_type != MINLZ_CHUNK_UNCOMPRESSED &&
        *chunk_type != MINLZ_CHUNK_MINLZ &&
        *chunk_type != MINLZ_CHUNK_MINLZ_COMP_CRC) {
        return MINLZ_CORRUPT;
    }
    return MINLZ_OK;
}

static minlz_status minlz_sidecar_fetch_block(
    minlz_sidecar_searcher *searcher,
    uint64_t offset, size_t expected_len, size_t max_block,
    uint8_t **decoded, size_t *decoded_len,
    size_t *wire_len) {
    uint8_t chunk_type;
    size_t chunk_len;
    minlz_status st = minlz_sidecar_main_header(
        searcher, offset, &chunk_type, &chunk_len);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_searcher_reserve(
        &searcher->main_payload,
        &searcher->main_payload_cap,
        chunk_len ? chunk_len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_sidecar_read_at_exact(
        searcher,
        (int64_t)offset + MINLZ_CHUNK_HEADER_SIZE,
        searcher->main_payload, chunk_len);
    if (st != MINLZ_OK) {
        return st;
    }
    if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        uint32_t expected_crc;
        if (chunk_len < MINLZ_CHECKSUM_SIZE) {
            return MINLZ_CORRUPT;
        }
        *decoded = searcher->main_payload + MINLZ_CHECKSUM_SIZE;
        *decoded_len = chunk_len - MINLZ_CHECKSUM_SIZE;
        expected_crc = minlz_load32(searcher->main_payload);
        if (!searcher->scan.ignore_crc &&
            minlz_crc(*decoded, *decoded_len) != expected_crc) {
            return MINLZ_CRC;
        }
    } else {
        st = minlz_decode_stream_chunk(
            searcher->scan.workspace,
            searcher->main_payload, chunk_len,
            decoded, decoded_len,
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC,
            searcher->scan.ignore_crc, max_block);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (*decoded_len != expected_len || *decoded_len > max_block) {
        return MINLZ_CORRUPT;
    }
    if (wire_len) {
        *wire_len = MINLZ_CHUNK_HEADER_SIZE + chunk_len;
    }
    return MINLZ_OK;
}

static void minlz_sidecar_clear_lazy(
    minlz_sidecar_searcher *searcher) {
    searcher->lazy_valid = 0;
    searcher->lazy_decoded = NULL;
    searcher->lazy_loaded_len = 0u;
}

static void minlz_sidecar_hold_lazy(
    minlz_sidecar_searcher *searcher,
    uint64_t offset, size_t decoded_len,
    size_t max_block, int64_t block_start) {
    searcher->lazy_offset = offset;
    searcher->lazy_decoded_len = decoded_len;
    searcher->lazy_max_block = max_block;
    searcher->lazy_start = block_start;
    searcher->lazy_valid = 1;
    searcher->lazy_decoded = NULL;
    searcher->lazy_loaded_len = 0u;
}

static minlz_status minlz_sidecar_load_lazy(
    void *ctx, const uint8_t **block, size_t *block_len) {
    minlz_sidecar_searcher *searcher =
        (minlz_sidecar_searcher *)ctx;
    uint8_t chunk_type;
    size_t chunk_len;
    uint8_t *decoded = NULL;
    size_t decoded_len = 0u;
    minlz_status st;

    if (!searcher || !block || !block_len) {
        return MINLZ_CORRUPT;
    }
    *block = NULL;
    *block_len = 0u;
    if (!searcher->lazy_valid) {
        return MINLZ_OK;
    }
    if (searcher->lazy_decoded) {
        *block = searcher->lazy_decoded;
        *block_len = searcher->lazy_loaded_len;
        return MINLZ_OK;
    }
    st = minlz_sidecar_main_header(
        searcher, searcher->lazy_offset,
        &chunk_type, &chunk_len);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_searcher_reserve(
        &searcher->lazy_payload,
        &searcher->lazy_payload_cap,
        chunk_len ? chunk_len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_sidecar_read_at_exact(
        searcher,
        (int64_t)searcher->lazy_offset +
            MINLZ_CHUNK_HEADER_SIZE,
        searcher->lazy_payload, chunk_len);
    if (st != MINLZ_OK) {
        return st;
    }
    if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        uint32_t expected_crc;
        if (chunk_len < MINLZ_CHECKSUM_SIZE) {
            return MINLZ_CORRUPT;
        }
        decoded =
            searcher->lazy_payload + MINLZ_CHECKSUM_SIZE;
        decoded_len = chunk_len - MINLZ_CHECKSUM_SIZE;
        expected_crc = minlz_load32(searcher->lazy_payload);
        if (!searcher->scan.ignore_crc &&
            minlz_crc(decoded, decoded_len) != expected_crc) {
            return MINLZ_CRC;
        }
    } else {
        if (!searcher->lazy_workspace) {
            st = minlz_workspace_create(
                &searcher->lazy_workspace);
            if (st != MINLZ_OK) {
                return st;
            }
        }
        st = minlz_decode_stream_chunk(
            searcher->lazy_workspace,
            searcher->lazy_payload, chunk_len,
            &decoded, &decoded_len,
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC,
            searcher->scan.ignore_crc,
            searcher->lazy_max_block);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (decoded_len != searcher->lazy_decoded_len ||
        decoded_len > searcher->lazy_max_block) {
        return MINLZ_CORRUPT;
    }
    searcher->lazy_decoded = decoded;
    searcher->lazy_loaded_len = decoded_len;
    *block = decoded;
    *block_len = decoded_len;
    return MINLZ_OK;
}

minlz_status minlz_sidecar_searcher_create(
    minlz_sidecar_searcher **searcher,
    minlz_read_at_cb main_read_at_cb, void *main_read_at_ctx,
    minlz_read_cb sidecar_read_cb, void *sidecar_read_ctx) {
    minlz_sidecar_searcher *value;
    minlz_status st;
    if (!searcher || !main_read_at_cb || !sidecar_read_cb) {
        return MINLZ_CORRUPT;
    }
    *searcher = NULL;
    value = (minlz_sidecar_searcher *)calloc(1, sizeof(*value));
    if (!value) {
        return MINLZ_IO;
    }
    st = minlz_workspace_create(&value->scan.workspace);
    if (st != MINLZ_OK) {
        free(value);
        return st;
    }
    value->main_read_at_cb = main_read_at_cb;
    value->main_read_at_ctx = main_read_at_ctx;
    value->sidecar_read_cb = sidecar_read_cb;
    value->sidecar_read_ctx = sidecar_read_ctx;
    value->scan.read_cb = sidecar_read_cb;
    value->scan.read_ctx = sidecar_read_ctx;
    value->scan.max_block_size = MINLZ_MAX_BLOCK_SIZE;
    *searcher = value;
    return MINLZ_OK;
}

void minlz_sidecar_searcher_destroy(
    minlz_sidecar_searcher *searcher) {
    if (!searcher) {
        return;
    }
    minlz_workspace_destroy(searcher->scan.workspace);
    minlz_workspace_destroy(searcher->lazy_workspace);
    free(searcher->scan.payload);
    free(searcher->scan.previous);
    free(searcher->scan.boundary_tail);
    free(searcher->scan.boundary_join);
    free(searcher->scan.forward_context);
    free(searcher->scan.windows);
    free(searcher->main_payload);
    free(searcher->read_ahead);
    free(searcher->lazy_payload);
    free(searcher);
}

void minlz_sidecar_searcher_set_ignore_crc(
    minlz_sidecar_searcher *searcher, int ignore_crc) {
    if (searcher) {
        searcher->scan.ignore_crc = ignore_crc != 0;
    }
}

void minlz_sidecar_searcher_set_bail_on_missing(
    minlz_sidecar_searcher *searcher, int bail) {
    if (searcher) {
        searcher->scan.bail_on_missing = bail != 0;
    }
}

void minlz_sidecar_searcher_set_collect_stats(
    minlz_sidecar_searcher *searcher, int collect) {
    if (searcher) {
        searcher->scan.collect_stats = collect != 0;
    }
}

minlz_status minlz_sidecar_searcher_set_max_block_size(
    minlz_sidecar_searcher *searcher, size_t max_block_size) {
    if (!searcher) {
        return MINLZ_CORRUPT;
    }
    return minlz_block_searcher_set_max_block_size(
        &searcher->scan, max_block_size);
}

void minlz_sidecar_searcher_set_info_callback(
    minlz_sidecar_searcher *searcher,
    minlz_search_info_cb callback, void *callback_ctx) {
    if (searcher) {
        searcher->scan.info_cb = callback;
        searcher->scan.info_ctx = callback_ctx;
    }
}

minlz_status minlz_sidecar_searcher_stats(
    const minlz_sidecar_searcher *searcher,
    minlz_search_stats *stats) {
    if (!searcher || !stats) {
        return MINLZ_CORRUPT;
    }
    *stats = searcher->scan.stats;
    return MINLZ_OK;
}

size_t minlz_sidecar_searcher_window_count(
    const minlz_sidecar_searcher *searcher) {
    return searcher
               ? searcher->scan.window_count
               : 0u;
}

minlz_status minlz_sidecar_searcher_window_get(
    const minlz_sidecar_searcher *searcher, size_t index,
    minlz_search_window_stat *window) {
    if (!searcher) {
        return MINLZ_CORRUPT;
    }
    return minlz_block_searcher_window_get(
        &searcher->scan, index, window);
}

static minlz_status minlz_sidecar_process_ref(
    minlz_sidecar_searcher *searcher,
    uint64_t offset, size_t decoded_len, size_t max_block,
    int table_seen, int table_usable, int table_possible,
    int tail_sensitive,
    const uint8_t *pattern, size_t pattern_len,
    minlz_search_match_cb callback, void *callback_ctx,
    int64_t *current_start) {
    uint8_t chunk_type;
    size_t chunk_len;
    uint8_t *decoded = NULL;
    size_t got_len = 0;
    size_t wire_len = 0;
    int skip = table_usable && !table_possible;
    int need_boundary =
        skip && searcher->scan.boundary_tail_len != 0u &&
        pattern_len > 1u;
    uint64_t found = 0;
    int forward_requested = 0;
    int64_t forward_offset = 0;
    minlz_search_lazy_callback lazy_callback;
    minlz_search_match_cb scan_callback = callback;
    void *scan_callback_ctx = callback_ctx;
    minlz_status st;

    searcher->scan.stats.blocks_total++;
    searcher->scan.stats.uncompressed_size += decoded_len;
    if (!table_seen) {
        searcher->scan.stats.tables_missing++;
    }
    if ((!table_seen || !table_usable) &&
        searcher->scan.bail_on_missing) {
        return MINLZ_SEARCH_TABLES_UNUSABLE;
    }
    if (skip && !need_boundary && !searcher->forward_active) {
        st = minlz_sidecar_main_header(
            searcher, offset, &chunk_type, &chunk_len);
        if (st != MINLZ_OK) {
            return st;
        }
        if (max_block > searcher->scan.max_block_size) {
            return MINLZ_TOO_LARGE;
        }
        searcher->scan.stats.blocks_skipped++;
        searcher->scan.stats.compressed_bytes_skipped +=
            MINLZ_CHUNK_HEADER_SIZE + chunk_len;
        if (tail_sensitive) {
            searcher->tail_offset = offset;
            searcher->tail_decoded_len = decoded_len;
            searcher->tail_max_block = max_block;
            searcher->tail_wire_len =
                MINLZ_CHUNK_HEADER_SIZE + chunk_len;
            searcher->tail_start = *current_start;
            searcher->tail_rescue = 1;
            searcher->scan.stats.blocks_deferred++;
            searcher->scan.stats.blocks_deferred_skipped++;
        }
        minlz_sidecar_hold_lazy(
            searcher, offset, decoded_len,
            max_block, *current_start);
        searcher->scan.previous_len = 0;
        minlz_searcher_clear_boundary_tail(&searcher->scan);
        *current_start += (int64_t)decoded_len;
        return MINLZ_OK;
    }

    st = minlz_sidecar_fetch_block(
        searcher, offset, decoded_len, max_block,
        &decoded, &got_len, &wire_len);
    if (st != MINLZ_OK) {
        return st;
    }
    searcher->scan.stats.blocks_searched++;
    searcher->scan.stats.uncompressed_bytes_searched += got_len;
    if (need_boundary) {
        searcher->scan.stats.blocks_boundary_scanned++;
    }
    if (searcher->forward_active && callback) {
        minlz_search_result result;
        minlz_status callback_st;
        memset(&result, 0, sizeof(result));
        result.previous_block =
            searcher->scan.forward_context_len
                ? searcher->scan.forward_context
                : NULL;
        result.previous_block_len =
            searcher->scan.forward_context_len;
        result.block = decoded;
        result.block_len = got_len;
        result.offset =
            searcher->scan.forward_context_offset;
        result.stream_offset =
            searcher->forward_stream_offset;
        result.block_start =
            searcher->scan.forward_context_start;
        callback_st = callback(callback_ctx, &result);
        if (callback_st == MINLZ_SEARCH_STOP) {
            return MINLZ_SEARCH_STOP;
        }
        if (callback_st == MINLZ_SEARCH_FORWARD) {
            st = minlz_searcher_set_forward_context(
                &searcher->scan, decoded, got_len,
                searcher->scan.forward_context_start,
                searcher->scan.forward_context_offset);
            if (st != MINLZ_OK) {
                return st;
            }
            forward_requested = 1;
            forward_offset =
                searcher->forward_stream_offset;
        } else if (callback_st != MINLZ_OK) {
            return callback_st;
        }
    }
    searcher->forward_active = 0;
    if (searcher->lazy_valid && callback) {
        lazy_callback.callback = callback;
        lazy_callback.callback_ctx = callback_ctx;
        lazy_callback.loader = minlz_sidecar_load_lazy;
        lazy_callback.loader_ctx = searcher;
        lazy_callback.previous_len =
            searcher->lazy_decoded_len;
        lazy_callback.previous_start =
            searcher->lazy_start;
        lazy_callback.current_start = *current_start;
        scan_callback = minlz_searcher_lazy_callback;
        scan_callback_ctx = &lazy_callback;
    }
    st = minlz_searcher_scan_block(
        &searcher->scan, pattern, pattern_len,
        decoded, got_len, *current_start,
        scan_callback, scan_callback_ctx, &found,
        &forward_requested, &forward_offset);
    if (st == MINLZ_SEARCH_STOP) {
        return MINLZ_SEARCH_STOP;
    }
    if (st != MINLZ_OK && st != MINLZ_SEARCH_FORWARD) {
        return st;
    }
    if (table_usable && table_possible && found == 0u) {
        searcher->scan.stats.blocks_false_positive++;
    }
    st = minlz_searcher_reserve(
        &searcher->scan.previous,
        &searcher->scan.previous_cap,
        got_len ? got_len : 1u);
    if (st != MINLZ_OK) {
        return st;
    }
    memcpy(searcher->scan.previous, decoded, got_len);
    searcher->scan.previous_len = got_len;
    searcher->scan.previous_start = *current_start;
    minlz_sidecar_clear_lazy(searcher);
    *current_start += (int64_t)got_len;
    searcher->forward_active = forward_requested;
    if (forward_requested) {
        searcher->forward_stream_offset = forward_offset;
    } else {
        searcher->scan.forward_context_len = 0u;
    }
    (void)wire_len;
    return MINLZ_OK;
}

static minlz_status minlz_sidecar_rescue_tail(
    minlz_sidecar_searcher *searcher,
    const uint8_t *pattern, size_t pattern_len,
    minlz_search_match_cb callback, void *callback_ctx) {
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    size_t wire_len = 0;
    uint64_t found = 0;
    int forward_requested = 0;
    int64_t forward_offset = 0;
    minlz_status st;

    if (!searcher->tail_rescue) {
        return MINLZ_OK;
    }
    searcher->tail_rescue = 0;
    st = minlz_sidecar_fetch_block(
        searcher, searcher->tail_offset,
        searcher->tail_decoded_len, searcher->tail_max_block,
        &decoded, &decoded_len, &wire_len);
    if (st != MINLZ_OK) {
        return st;
    }
    if (searcher->scan.stats.blocks_skipped != 0u) {
        searcher->scan.stats.blocks_skipped--;
    }
    if (searcher->scan.stats.blocks_deferred_skipped != 0u) {
        searcher->scan.stats.blocks_deferred_skipped--;
    }
    if (searcher->scan.stats.compressed_bytes_skipped >=
        searcher->tail_wire_len) {
        searcher->scan.stats.compressed_bytes_skipped -=
            searcher->tail_wire_len;
    }
    searcher->scan.stats.blocks_searched++;
    searcher->scan.stats.uncompressed_bytes_searched += decoded_len;
    searcher->scan.previous_len = 0;
    minlz_searcher_clear_boundary_tail(&searcher->scan);
    minlz_sidecar_clear_lazy(searcher);
    st = minlz_searcher_scan_block(
        &searcher->scan, pattern, pattern_len,
        decoded, decoded_len, searcher->tail_start,
        callback, callback_ctx, &found,
        &forward_requested, &forward_offset);
    if (st != MINLZ_OK) {
        return st;
    }
    if (forward_requested) {
        st = minlz_searcher_forward_eof(
            callback, callback_ctx,
            decoded, decoded_len,
            searcher->tail_start, forward_offset);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (found == 0u) {
        searcher->scan.stats.blocks_false_positive++;
    }
    (void)wire_len;
    return MINLZ_OK;
}

minlz_status minlz_sidecar_searcher_search(
    minlz_sidecar_searcher *searcher,
    const uint8_t *pattern, size_t pattern_len,
    minlz_search_match_cb callback, void *callback_ctx) {
    uint8_t header[4];
    size_t max_block = 0;
    int64_t current_start = 0;
    int table_seen = 0;
    int table_usable = 0;
    int table_possible = 1;
    int table_tail_only = 0;
    int stream_open = 1;
    minlz_status st;

    if (!searcher || !pattern || pattern_len == 0u) {
        return MINLZ_CORRUPT;
    }
    memset(&searcher->scan.stats, 0, sizeof(searcher->scan.stats));
    searcher->scan.previous_len = 0;
    minlz_searcher_clear_boundary_tail(&searcher->scan);
    searcher->scan.forward_context_len = 0u;
    searcher->tail_rescue = 0;
    searcher->forward_active = 0;
    minlz_sidecar_clear_lazy(searcher);
    searcher->scan.window_count = 0u;
    searcher->scan.tables_tallied = 0u;
    searcher->scan.windows_initialized = 0;
    searcher->scan.windows_compatible = 1;
    searcher->read_ahead_valid = 0;

    {
        uint8_t body[6];
        int64_t consumed = 0;
        st = minlz_search_read_first_header(
            searcher->sidecar_read_cb,
            searcher->sidecar_read_ctx,
            header, body, &max_block, &consumed);
        if (st != MINLZ_OK) {
            return st;
        }
        if (max_block > searcher->scan.max_block_size) {
            return MINLZ_TOO_LARGE;
        }
        searcher->read_ahead_target =
            minlz_sidecar_read_ahead_size(max_block);
    }

    for (;;) {
        size_t got = 0;
        uint8_t chunk_type;
        size_t chunk_len;
        st = minlz_read_exact(
            searcher->sidecar_read_cb,
            searcher->sidecar_read_ctx,
            header, sizeof(header), 1, &got);
        if (st != MINLZ_OK) {
            return st;
        }
        if (got == 0u) {
            st = minlz_sidecar_rescue_tail(
                searcher, pattern, pattern_len,
                callback, callback_ctx);
            if (st == MINLZ_SEARCH_STOP) {
                return MINLZ_OK;
            }
            if (st != MINLZ_OK) {
                return st;
            }
            if (searcher->forward_active) {
                st = minlz_searcher_forward_eof(
                    callback, callback_ctx,
                    searcher->scan.forward_context,
                    searcher->scan.forward_context_len,
                    searcher->scan.forward_context_start,
                    searcher->forward_stream_offset);
                if (st == MINLZ_SEARCH_STOP) {
                    return MINLZ_OK;
                }
                if (st != MINLZ_OK) {
                    return st;
                }
                searcher->forward_active = 0;
            }
            return MINLZ_OK;
        }
        chunk_type = header[0];
        chunk_len = (size_t)header[1] |
                    ((size_t)header[2] << 8) |
                    ((size_t)header[3] << 16);
        st = minlz_searcher_read_payload(
            &searcher->scan, chunk_len);
        if (st != MINLZ_OK) {
            return st;
        }

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            if (stream_open || chunk_len != 6u ||
                memcmp(searcher->scan.payload, "MinLz", 5u) != 0 ||
                (searcher->scan.payload[5] >> 6) != 0u ||
                (searcher->scan.payload[5] & 15u) > 13u) {
                return MINLZ_CORRUPT;
            }
            max_block =
                (size_t)1u
                << ((searcher->scan.payload[5] & 15u) + 10u);
            if (max_block > searcher->scan.max_block_size) {
                return MINLZ_TOO_LARGE;
            }
            searcher->read_ahead_target =
                minlz_sidecar_read_ahead_size(max_block);
            searcher->read_ahead_valid = 0;
            current_start = 0;
            table_seen = 0;
            table_usable = 0;
            table_possible = 1;
            table_tail_only = 0;
            searcher->scan.previous_len = 0;
            minlz_searcher_clear_boundary_tail(&searcher->scan);
            searcher->scan.forward_context_len = 0u;
            minlz_sidecar_clear_lazy(searcher);
            stream_open = 1;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_SEARCH_INFO) {
            minlz_search_config config;
            size_t used = 0;
            st = minlz_search_parse_info(
                searcher->scan.payload, chunk_len,
                &config, &used);
            if (st != MINLZ_OK || used != chunk_len) {
                return st == MINLZ_OK ? MINLZ_CORRUPT : st;
            }
            if (searcher->scan.info_cb) {
                searcher->scan.info_cb(
                    searcher->scan.info_ctx, &config);
            }
            continue;
        }
        if (chunk_type == MINLZ_CHUNK_SEARCH_TABLE) {
            minlz_search_table_view view;
            int can_use = 0;
            int possible;
            searcher->scan.stats.tables_present++;
            searcher->scan.stats.table_bytes +=
                MINLZ_CHUNK_HEADER_SIZE + chunk_len;
            table_seen = 1;
            st = minlz_search_parse_table(
                searcher->scan.payload, chunk_len,
                searcher->scan.ignore_crc, &view);
            if (st == MINLZ_CRC && !searcher->scan.ignore_crc) {
                return st;
            }
            if (st != MINLZ_OK) {
                searcher->scan.stats.tables_unusable++;
                continue;
            }
            st = minlz_searcher_tally_table(
                &searcher->scan, &view, pattern, pattern_len,
                0, MINLZ_CHUNK_HEADER_SIZE + chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            possible = minlz_search_pattern_might_match(
                &view, pattern, pattern_len, &can_use);
            if (!can_use) {
                searcher->scan.stats.tables_unusable++;
                continue;
            }
            table_usable = 1;
            if (!possible) {
                int tail_sensitive =
                    minlz_search_reject_needs_tail_rescue(
                        &view, pattern, pattern_len);
                table_tail_only = table_possible
                                      ? tail_sensitive
                                      : table_tail_only &&
                                            tail_sensitive;
                table_possible = 0;
            }
            continue;
        }
        if (chunk_type == MINLZ_CHUNK_SEARCH_TABLE_COMPRESSED) {
            minlz_search_table_view view;
            uint8_t *table_storage = NULL;
            int can_use = 0;
            int possible;
            searcher->scan.stats.tables_present++;
            searcher->scan.stats.tables_compressed++;
            searcher->scan.stats.table_bytes +=
                MINLZ_CHUNK_HEADER_SIZE + chunk_len;
            table_seen = 1;
            st = minlz_search_parse_table_compressed(
                searcher->scan.payload, chunk_len,
                searcher->scan.ignore_crc,
                &table_storage, &view);
            if (st == MINLZ_CRC && !searcher->scan.ignore_crc) {
                return st;
            }
            if (st != MINLZ_OK) {
                searcher->scan.stats.tables_unusable++;
                free(table_storage);
                continue;
            }
            st = minlz_searcher_tally_table(
                &searcher->scan, &view, pattern, pattern_len,
                1, MINLZ_CHUNK_HEADER_SIZE + chunk_len);
            if (st != MINLZ_OK) {
                free(table_storage);
                return st;
            }
            possible = minlz_search_pattern_might_match(
                &view, pattern, pattern_len, &can_use);
            if (!can_use) {
                searcher->scan.stats.tables_unusable++;
                free(table_storage);
                continue;
            }
            table_usable = 1;
            if (!possible) {
                int tail_sensitive =
                    minlz_search_reject_needs_tail_rescue(
                        &view, pattern, pattern_len);
                table_tail_only = table_possible
                                      ? tail_sensitive
                                      : table_tail_only &&
                                            tail_sensitive;
                table_possible = 0;
            }
            free(table_storage);
            continue;
        }
        if (chunk_type == MINLZ_CHUNK_REMOTE_BLOCK_REF) {
            const uint8_t *payload = searcher->scan.payload;
            size_t remaining = chunk_len;
            uint64_t previous_offset = 0;
            int first = 1;
            if (!stream_open) {
                return MINLZ_CORRUPT;
            }
            if (remaining == 0u) {
                return MINLZ_CORRUPT;
            }
            while (remaining != 0u) {
                uint64_t encoded_offset;
                uint64_t minus_actual;
                uint64_t absolute_offset;
                size_t used1 = 0;
                size_t used2 = 0;
                size_t actual;
                st = minlz_search_read_u64varint(
                    payload, remaining, &encoded_offset, &used1);
                if (st != MINLZ_OK) {
                    return st;
                }
                payload += used1;
                remaining -= used1;
                st = minlz_search_read_u64varint(
                    payload, remaining, &minus_actual, &used2);
                if (st != MINLZ_OK) {
                    return st;
                }
                payload += used2;
                remaining -= used2;
                if (first) {
                    absolute_offset = encoded_offset;
                } else {
                    if (encoded_offset == 0u ||
                        previous_offset > UINT64_MAX - encoded_offset) {
                        return MINLZ_CORRUPT;
                    }
                    absolute_offset = previous_offset + encoded_offset;
                }
                if (minus_actual >= max_block) {
                    return MINLZ_CORRUPT;
                }
                actual = max_block - (size_t)minus_actual;
                /*
                 * A held candidate followed by another remote reference was
                 * not the stream tail and can remain skipped.
                 */
                searcher->tail_rescue = 0;
                st = minlz_sidecar_process_ref(
                    searcher, absolute_offset, actual, max_block,
                    first ? table_seen : 0,
                    first ? table_usable : 0,
                    first ? table_possible : 1,
                    first ? table_tail_only : 0,
                    pattern, pattern_len, callback, callback_ctx,
                    &current_start);
                if (st == MINLZ_SEARCH_STOP) {
                    return MINLZ_OK;
                }
                if (st != MINLZ_OK) {
                    return st;
                }
                previous_offset = absolute_offset;
                first = 0;
            }
            table_seen = 0;
            table_usable = 0;
            table_possible = 1;
            table_tail_only = 0;
            continue;
        }
        if (chunk_type == MINLZ_CHUNK_EOF) {
            uint64_t size = 0;
            size_t used = 0;
            if (!stream_open) {
                return MINLZ_CORRUPT;
            }
            if (chunk_len != 0u) {
                st = minlz_search_read_u64varint(
                    searcher->scan.payload, chunk_len,
                    &size, &used);
                if (st != MINLZ_OK || used != chunk_len ||
                    size != 0u) {
                    return MINLZ_CORRUPT;
                }
            }
            st = minlz_sidecar_rescue_tail(
                searcher, pattern, pattern_len,
                callback, callback_ctx);
            if (st == MINLZ_SEARCH_STOP) {
                return MINLZ_OK;
            }
            if (st != MINLZ_OK) {
                return st;
            }
            if (searcher->forward_active) {
                st = minlz_searcher_forward_eof(
                    callback, callback_ctx,
                    searcher->scan.forward_context,
                    searcher->scan.forward_context_len,
                    searcher->scan.forward_context_start,
                    searcher->forward_stream_offset);
                if (st == MINLZ_SEARCH_STOP) {
                    return MINLZ_OK;
                }
                if (st != MINLZ_OK) {
                    return st;
                }
                searcher->forward_active = 0;
            }
            current_start = 0;
            table_seen = 0;
            table_usable = 0;
            table_possible = 1;
            table_tail_only = 0;
            searcher->scan.previous_len = 0;
            minlz_searcher_clear_boundary_tail(&searcher->scan);
            searcher->scan.forward_context_len = 0u;
            minlz_sidecar_clear_lazy(searcher);
            stream_open = 0;
            continue;
        }
        if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
            (chunk_type >= 0xc0u &&
             chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            return MINLZ_UNSUPPORTED;
        }
    }
}
