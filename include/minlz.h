/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 */
#ifndef MINLZ_H
#define MINLZ_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MINLZ_SPEC_VERSION "v1.0"
#define MINLZ_VERSION_STRING "1.2.5"
#define MINLZ_SEARCH_SPEC_VERSION "v1.2.0"
#define MINLZ_MAX_BLOCK_SIZE (8u << 20)
#define MINLZ_MIN_BLOCK_SIZE (4u << 10)
#define MINLZ_DEFAULT_BLOCK_SIZE (2u << 20)

typedef enum minlz_status {
    MINLZ_OK = 0,
    MINLZ_CORRUPT,
    MINLZ_CRC,
    MINLZ_TOO_LARGE,
    MINLZ_UNSUPPORTED,
    MINLZ_INVALID_LEVEL,
    MINLZ_DST_TOO_SMALL,
    MINLZ_IO,
    MINLZ_SEARCH_TABLES_UNUSABLE,
    MINLZ_SEARCH_STOP,
    MINLZ_SEARCH_FORWARD
} minlz_status;

enum {
    MINLZ_LEVEL_SUPERFAST = -1,
    MINLZ_LEVEL_UNCOMPRESSED = 0,
    MINLZ_LEVEL_FASTEST = 1,
    MINLZ_LEVEL_BALANCED = 2,
    MINLZ_LEVEL_SMALLEST = 3
};

typedef struct minlz_workspace minlz_workspace;
typedef struct minlz_index minlz_index;
typedef struct minlz_reader minlz_reader;
typedef struct minlz_readseeker minlz_readseeker;
typedef struct minlz_writer minlz_writer;
typedef struct minlz_lz4_converter minlz_lz4_converter;
typedef struct minlz_block_searcher minlz_block_searcher;
typedef struct minlz_sidecar_searcher minlz_sidecar_searcher;

typedef minlz_status (*minlz_read_cb)(void *ctx, uint8_t *dst, size_t cap, size_t *nread);
typedef minlz_status (*minlz_write_cb)(void *ctx, const uint8_t *src, size_t len);
typedef minlz_status (*minlz_seek_cb)(void *ctx, int64_t offset, int whence, int64_t *new_pos);
typedef minlz_status (*minlz_read_at_cb)(void *ctx, int64_t offset, uint8_t *dst,
                                        size_t len, size_t *nread);
typedef minlz_status (*minlz_user_chunk_cb)(void *ctx, uint8_t id,
                                            minlz_read_cb read_cb, void *read_ctx,
                                            size_t len);

enum {
    MINLZ_SEARCH_TABLE_NO_PREFIX = 1,
    MINLZ_SEARCH_TABLE_BYTE_PREFIX = 2,
    MINLZ_SEARCH_TABLE_MASK_PREFIX = 3,
    MINLZ_SEARCH_TABLE_LONG_PREFIX = 4
};

typedef struct minlz_compressed_search_stats {
    size_t bitmap_bytes;
    uint8_t reductions;
    size_t sub_block_size;
    size_t sub_blocks;
    uint64_t set_bits;
    uint64_t total_bits;
    int skipped_band;
    int skipped_size;
    size_t chunk_0x45_size;
    size_t chunk_0x46_size;
    int emitted_0x46;
    size_t tables;
    size_t blocks_own_table;
    size_t blocks_global_table;
    size_t blocks_raw;
    size_t blocks_rle;
    size_t blocks_sparse;
    size_t bytes_own_payload;
    size_t bytes_global_payload;
    size_t bytes_raw_payload;
    size_t bytes_rle_payload;
    size_t bytes_sparse_payload;
    size_t bytes_own_tables;
    size_t bytes_global_table;
} minlz_compressed_search_stats;

typedef void (*minlz_compressed_search_stats_cb)(
    void *ctx, const minlz_compressed_search_stats *stats);

/*
 * Fixed-size, allocation-free equivalent of Go SearchTableConfig.
 * base_table_log2=0 selects the stream block-size default.
 */
typedef struct minlz_search_config {
    uint8_t match_len;
    uint8_t table_type;
    uint8_t base_table_log2;
    uint8_t extras;
    uint8_t prefix_bytes[8];
    uint8_t prefix_mask[32];
    uint8_t long_prefix[256];
    uint16_t long_prefix_len;
    uint8_t max_population_pct;
    uint8_t max_reduced_population_pct;
    uint8_t max_reduced_population_explicit;
    uint8_t compress_tables;
    uint16_t compression_skip_pct_x100;
    uint8_t force_compressed;
    uint8_t reserved[1];
    minlz_compressed_search_stats_cb compressed_stats_cb;
    void *compressed_stats_ctx;
} minlz_search_config;

typedef struct minlz_stream_options {
    int level;
    size_t block_size;
    int threads;
    int add_index;
    size_t padding;
    int ignore_crc;
    int allow_fallback;
    const minlz_search_config *search;
    minlz_write_cb sidecar_write_cb;
    void *sidecar_write_ctx;
} minlz_stream_options;

typedef minlz_status (*minlz_search_previous_loader_cb)(
    void *ctx, const uint8_t **block, size_t *block_len);

typedef struct minlz_search_result {
    const uint8_t *previous_block;
    size_t previous_block_len;
    const uint8_t *block;
    size_t block_len;
    int64_t offset;
    int64_t stream_offset;
    int64_t block_start;
    /*
     * Internal lazy-loader state. Use minlz_search_result_load_previous()
     * instead of accessing these fields. The result and loaded block are
     * valid only for the duration of the search callback.
     */
    minlz_search_previous_loader_cb previous_block_loader;
    void *previous_block_loader_ctx;
} minlz_search_result;

typedef struct minlz_search_stats {
    uint64_t blocks_total;
    uint64_t blocks_searched;
    uint64_t blocks_skipped;
    uint64_t blocks_deferred;
    uint64_t blocks_deferred_skipped;
    uint64_t blocks_false_positive;
    uint64_t blocks_boundary_scanned;
    uint64_t tables_present;
    uint64_t tables_missing;
    uint64_t tables_unusable;
    uint64_t tables_compressed;
    uint64_t table_bytes;
    uint64_t tables_compressed_bytes;
    uint64_t table_bitmap_bytes;
    uint64_t table_bits_sum;
    uint64_t table_reductions_sum;
    uint64_t compressed_bytes_skipped;
    uint64_t uncompressed_bytes_searched;
    uint64_t uncompressed_size;
    uint64_t compressed_blocks_total;
    uint64_t compressed_blocks_raw;
    uint64_t compressed_blocks_rle;
    uint64_t compressed_blocks_sparse;
    uint64_t compressed_tables_sum;
    uint64_t compressed_bytes_tabled;
    uint64_t compressed_bytes_raw;
    uint64_t compressed_bytes_rle;
    uint64_t compressed_bytes_sparse;
    uint64_t compressed_bytes_table_headers;
    double table_population_min;
    double table_population_max;
    double table_population_sum;
    uint64_t matches;
} minlz_search_stats;

typedef struct minlz_search_window_stat {
    size_t position;
    int prefix_position;
    uint8_t prefix_byte;
    uint8_t bytes[8];
    uint8_t bytes_len;
    uint64_t present;
    uint64_t absent;
} minlz_search_window_stat;

typedef minlz_status (*minlz_search_match_cb)(void *ctx,
                                              const minlz_search_result *result);
typedef void (*minlz_search_info_cb)(void *ctx,
                                     const minlz_search_config *config);

typedef struct minlz_index_pair {
    int64_t compressed_offset;
    int64_t uncompressed_offset;
} minlz_index_pair;

typedef struct minlz_index_info {
    int64_t total_uncompressed;
    int64_t total_compressed;
    int64_t est_block_uncompressed;
    size_t entries;
} minlz_index_info;

enum {
    MINLZ_SEEK_SET = 0,
    MINLZ_SEEK_CUR = 1,
    MINLZ_SEEK_END = 2
};

const char *minlz_status_string(minlz_status status);
void minlz_free(void *p);

size_t minlz_max_encoded_len(size_t src_len);
minlz_status minlz_decoded_len(const uint8_t *src, size_t src_len, size_t *decoded_len);
int minlz_is_block(const uint8_t *src, size_t src_len, size_t *decoded_len);

minlz_status minlz_encode(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                          const uint8_t *src, size_t src_len, int level);
minlz_status minlz_try_encode(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                              const uint8_t *src, size_t src_len, int level);
minlz_status minlz_decode(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                          const uint8_t *src, size_t src_len);

minlz_status minlz_workspace_create(minlz_workspace **ws);
void minlz_workspace_destroy(minlz_workspace *ws);
void minlz_workspace_reset(minlz_workspace *ws);

minlz_status minlz_encode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap, size_t *dst_len,
                             const uint8_t *src, size_t src_len, int level);
minlz_status minlz_try_encode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                                 size_t *dst_len, const uint8_t *src, size_t src_len,
                                 int level);
minlz_status minlz_decode_ws(minlz_workspace *ws, uint8_t *dst, size_t dst_cap,
                             size_t *dst_len, const uint8_t *src, size_t src_len);

void minlz_stream_options_init(minlz_stream_options *opts);

void minlz_search_config_init(minlz_search_config *config);
minlz_status minlz_search_config_set_match_len(minlz_search_config *config,
                                               unsigned match_len);
minlz_status minlz_search_config_set_byte_prefix(minlz_search_config *config,
                                                 const uint8_t *prefixes,
                                                 size_t prefix_count);
minlz_status minlz_search_config_set_mask_prefix(minlz_search_config *config,
                                                 const uint8_t mask[32]);
minlz_status minlz_search_config_set_long_prefix(minlz_search_config *config,
                                                 const uint8_t *prefix,
                                                 size_t prefix_len,
                                                 unsigned extras);
minlz_status minlz_search_config_set_extras(minlz_search_config *config,
                                            unsigned extras);
minlz_status minlz_search_config_set_population(minlz_search_config *config,
                                                unsigned max_population_pct,
                                                unsigned max_reduced_population_pct);
minlz_status minlz_search_config_set_max_population(
    minlz_search_config *config, unsigned max_population_pct);
minlz_status minlz_search_config_set_max_reduced_population(
    minlz_search_config *config, unsigned max_reduced_population_pct);
void minlz_search_config_set_compression(minlz_search_config *config, int enabled);
minlz_status minlz_search_config_set_compression_skip(
    minlz_search_config *config, double pct);
void minlz_search_config_set_compression_force(
    minlz_search_config *config, int force);
void minlz_search_config_set_compression_stats_callback(
    minlz_search_config *config,
    minlz_compressed_search_stats_cb cb, void *cb_ctx);
size_t minlz_search_config_snprint(
    char *dst, size_t dst_cap,
    const minlz_search_config *config);
uint32_t minlz_search_hash(uint64_t value, unsigned table_log2,
                           unsigned match_len);

minlz_status minlz_stream_encode(minlz_read_cb read_cb, void *read_ctx,
                                 minlz_write_cb write_cb, void *write_ctx,
                                 const minlz_stream_options *opts);
minlz_status minlz_stream_decode(minlz_read_cb read_cb, void *read_ctx,
                                 minlz_write_cb write_cb, void *write_ctx,
                                 const minlz_stream_options *opts);
minlz_status minlz_stream_encode_ws(minlz_workspace *ws,
                                    minlz_read_cb read_cb, void *read_ctx,
                                    minlz_write_cb write_cb, void *write_ctx,
                                    const minlz_stream_options *opts);
minlz_status minlz_stream_decode_ws(minlz_workspace *ws,
                                    minlz_read_cb read_cb, void *read_ctx,
                                    minlz_write_cb write_cb, void *write_ctx,
                                    const minlz_stream_options *opts);

minlz_status minlz_stream_encode_file(FILE *in, FILE *out, const minlz_stream_options *opts);
minlz_status minlz_stream_decode_file(FILE *in, FILE *out, const minlz_stream_options *opts);
minlz_status minlz_reader_create_file(minlz_reader **reader, FILE *in,
                                      const minlz_stream_options *opts);
minlz_status minlz_reader_create_seekable_file(minlz_reader **reader, FILE *in,
                                               const minlz_stream_options *opts);
minlz_status minlz_writer_create_file(minlz_writer **writer, FILE *out,
                                      const minlz_stream_options *opts);

minlz_status minlz_index_create(minlz_index **idx);
void minlz_index_destroy(minlz_index *idx);
void minlz_index_reset(minlz_index *idx, size_t block_size);
minlz_status minlz_index_add_pair(minlz_index *idx, int64_t compressed_offset,
                                  int64_t uncompressed_offset);
minlz_status minlz_index_serialize(const minlz_index *idx, uint8_t **dst, size_t *dst_len,
                                   int64_t total_uncompressed, int64_t total_compressed);
minlz_status minlz_index_load(minlz_index *idx, const uint8_t *src, size_t src_len,
                              size_t *used);
minlz_status minlz_index_load_stream_file(minlz_index *idx, FILE *in);
minlz_status minlz_index_stream(minlz_read_cb read_cb, void *read_ctx,
                                uint8_t **dst, size_t *dst_len);
minlz_status minlz_index_find(const minlz_index *idx, int64_t offset,
                              minlz_index_pair *pair);
minlz_status minlz_index_info_get(const minlz_index *idx, minlz_index_info *info);
minlz_status minlz_index_entry_get(const minlz_index *idx, size_t n,
                                   minlz_index_pair *pair);
minlz_status minlz_index_json(const minlz_index *idx, char **json, size_t *json_len);
minlz_status minlz_index_remove_headers(const uint8_t *src, size_t src_len,
                                        const uint8_t **trimmed, size_t *trimmed_len);
minlz_status minlz_index_restore_headers(const uint8_t *src, size_t src_len,
                                         uint8_t **dst, size_t *dst_len);

minlz_status minlz_writer_create(minlz_writer **writer, minlz_write_cb write_cb,
                                 void *write_ctx, const minlz_stream_options *opts);
void minlz_writer_destroy(minlz_writer *writer);
minlz_status minlz_writer_reset(minlz_writer *writer, minlz_write_cb write_cb,
                                void *write_ctx);
minlz_status minlz_writer_write(minlz_writer *writer, const uint8_t *src, size_t len,
                                size_t *written);
minlz_status minlz_writer_read_from(minlz_writer *writer, minlz_read_cb read_cb,
                                    void *read_ctx, uint64_t *read_total);
/* With search enabled, the final block may be buffered until the next write,
 * Flush or Close. The writer copies any data retained after this call. */
minlz_status minlz_writer_encode_buffer(minlz_writer *writer, const uint8_t *src,
                                        size_t len);
minlz_status minlz_writer_add_user_chunk(minlz_writer *writer, uint8_t id,
                                         const uint8_t *src, size_t len);
minlz_status minlz_writer_async_flush(minlz_writer *writer);
minlz_status minlz_writer_flush(minlz_writer *writer);
minlz_status minlz_writer_close(minlz_writer *writer);
minlz_status minlz_writer_close_index(minlz_writer *writer, uint8_t **index,
                                      size_t *index_len);
minlz_status minlz_writer_written(const minlz_writer *writer, int64_t *input,
                                  int64_t *output);
minlz_status minlz_writer_set_search_table(minlz_writer *writer,
                                           const minlz_search_config *config);
minlz_status minlz_writer_set_sidecar(minlz_writer *writer,
                                      minlz_write_cb write_cb, void *write_ctx);

minlz_status minlz_reader_create(minlz_reader **reader, minlz_read_cb read_cb,
                                 void *read_ctx, const minlz_stream_options *opts);
minlz_status minlz_reader_create_seekable(minlz_reader **reader, minlz_read_cb read_cb,
                                          minlz_seek_cb seek_cb, void *ctx,
                                          const minlz_stream_options *opts);
void minlz_reader_destroy(minlz_reader *reader);
minlz_status minlz_reader_reset(minlz_reader *reader, minlz_read_cb read_cb,
                                void *read_ctx);
minlz_status minlz_reader_reset_seekable(minlz_reader *reader, minlz_read_cb read_cb,
                                         minlz_seek_cb seek_cb, void *ctx);
minlz_status minlz_reader_read(minlz_reader *reader, uint8_t *dst, size_t cap,
                               size_t *nread);
minlz_status minlz_reader_read_byte(minlz_reader *reader, uint8_t *value);
minlz_status minlz_reader_skip(minlz_reader *reader, int64_t n);
minlz_status minlz_reader_write_to(minlz_reader *reader, minlz_write_cb write_cb,
                                   void *write_ctx, uint64_t *written);
minlz_status minlz_reader_decode_concurrent(minlz_reader *reader, minlz_write_cb write_cb,
                                            void *write_ctx, int threads,
                                            uint64_t *written);
minlz_status minlz_reader_user_chunk_cb(minlz_reader *reader, uint8_t id,
                                        minlz_user_chunk_cb cb, void *cb_ctx);
minlz_status minlz_reader_readseeker(minlz_reader *reader, const uint8_t *index,
                                     size_t index_len, minlz_readseeker **rs);
void minlz_readseeker_destroy(minlz_readseeker *rs);
minlz_status minlz_readseeker_seek(minlz_readseeker *rs, int64_t offset, int whence,
                                   int64_t *new_pos);
minlz_status minlz_readseeker_read(minlz_readseeker *rs, uint8_t *dst, size_t cap,
                                   size_t *nread);
minlz_status minlz_readseeker_read_at(minlz_readseeker *rs, uint8_t *dst, size_t len,
                                      int64_t offset, size_t *nread);
minlz_status minlz_readseeker_index(minlz_readseeker *rs, const minlz_index **idx);

minlz_status minlz_lz4_convert_block(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                                     size_t *decoded_len, const uint8_t *src,
                                     size_t src_len);
minlz_status minlz_lz4_convert_stream(minlz_read_cb read_cb, void *read_ctx,
                                      minlz_write_cb write_cb, void *write_ctx);

minlz_status minlz_block_searcher_create(minlz_block_searcher **searcher,
                                         minlz_read_cb read_cb, void *read_ctx);
void minlz_block_searcher_destroy(minlz_block_searcher *searcher);
void minlz_block_searcher_set_ignore_crc(minlz_block_searcher *searcher,
                                         int ignore_crc);
void minlz_block_searcher_set_bail_on_missing(minlz_block_searcher *searcher,
                                              int bail);
void minlz_block_searcher_set_collect_stats(minlz_block_searcher *searcher,
                                            int collect);
minlz_status minlz_block_searcher_set_max_block_size(
    minlz_block_searcher *searcher, size_t max_block_size);
void minlz_block_searcher_set_info_callback(minlz_block_searcher *searcher,
                                            minlz_search_info_cb cb,
                                            void *cb_ctx);
minlz_status minlz_block_searcher_search(minlz_block_searcher *searcher,
                                         const uint8_t *pattern,
                                         size_t pattern_len,
                                         minlz_search_match_cb match_cb,
                                         void *match_ctx);
/*
 * Return the previous block without forcing searchers to decode skipped
 * blocks eagerly. This mirrors Go SearchResult.PrevBlock(). A NULL block
 * with MINLZ_OK means that no previous block exists.
 */
minlz_status minlz_search_result_load_previous(
    const minlz_search_result *result,
    const uint8_t **block, size_t *block_len);
minlz_status minlz_block_searcher_stats(const minlz_block_searcher *searcher,
                                        minlz_search_stats *stats);
size_t minlz_block_searcher_window_count(
    const minlz_block_searcher *searcher);
minlz_status minlz_block_searcher_window_get(
    const minlz_block_searcher *searcher, size_t index,
    minlz_search_window_stat *window);

minlz_status minlz_sidecar_searcher_create(
    minlz_sidecar_searcher **searcher,
    minlz_read_at_cb main_read_at_cb, void *main_read_at_ctx,
    minlz_read_cb sidecar_read_cb, void *sidecar_read_ctx);
void minlz_sidecar_searcher_destroy(minlz_sidecar_searcher *searcher);
void minlz_sidecar_searcher_set_ignore_crc(
    minlz_sidecar_searcher *searcher, int ignore_crc);
void minlz_sidecar_searcher_set_bail_on_missing(
    minlz_sidecar_searcher *searcher, int bail);
void minlz_sidecar_searcher_set_collect_stats(
    minlz_sidecar_searcher *searcher, int collect);
minlz_status minlz_sidecar_searcher_set_max_block_size(
    minlz_sidecar_searcher *searcher, size_t max_block_size);
void minlz_sidecar_searcher_set_info_callback(
    minlz_sidecar_searcher *searcher,
    minlz_search_info_cb cb, void *cb_ctx);
minlz_status minlz_sidecar_searcher_search(
    minlz_sidecar_searcher *searcher,
    const uint8_t *pattern, size_t pattern_len,
    minlz_search_match_cb match_cb, void *match_ctx);
minlz_status minlz_sidecar_searcher_stats(
    const minlz_sidecar_searcher *searcher,
    minlz_search_stats *stats);
size_t minlz_sidecar_searcher_window_count(
    const minlz_sidecar_searcher *searcher);
minlz_status minlz_sidecar_searcher_window_get(
    const minlz_sidecar_searcher *searcher, size_t index,
    minlz_search_window_stat *window);

void minlz_search_stats_fprint(FILE *out,
                               const minlz_search_stats *stats);
void minlz_search_stats_fprint_extended(
    FILE *out, const minlz_search_stats *stats,
    const minlz_search_window_stat *windows, size_t window_count);

minlz_status minlz_build_sidecar(minlz_write_cb sidecar_write_cb,
                                 void *sidecar_write_ctx,
                                 minlz_read_cb stream_read_cb,
                                 void *stream_read_ctx,
                                 const minlz_search_config *configs,
                                 size_t config_count,
                                 int ignore_crc);
minlz_status minlz_extract_sidecar(minlz_write_cb sidecar_write_cb,
                                   void *sidecar_write_ctx,
                                   minlz_write_cb stripped_write_cb,
                                   void *stripped_write_ctx,
                                   minlz_read_cb stream_read_cb,
                                   void *stream_read_ctx);

#ifdef __cplusplus
}
#endif

#endif
