/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <stdlib.h>

#ifndef MINLZ_HAVE_THREADS
#define MINLZ_HAVE_THREADS 0
#endif

#if MINLZ_HAVE_THREADS && !defined(_WIN32)
#include <pthread.h>
#include <unistd.h>
#define MINLZ_STREAM_PTHREADS 1
#else
#define MINLZ_STREAM_PTHREADS 0
#endif

#ifndef MINLZ_READSEEKER_CACHE_BLOCKS
#define MINLZ_READSEEKER_CACHE_BLOCKS 8u
#endif

#ifndef MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER
#define MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER 0
#endif

static unsigned minlz_ceil_log2_size(size_t n) {
    unsigned l = 0;
    size_t v = 1;
    while (v < n && l < sizeof(size_t) * CHAR_BIT - 1) {
        v <<= 1;
        l++;
    }
    return l;
}

static minlz_status minlz_normalize_options(minlz_stream_options *out,
                                            const minlz_stream_options *in,
                                            int encoding) {
    minlz_stream_options tmp;
    minlz_search_config search_tmp;
    minlz_stream_options_init(&tmp);
    if (!encoding) {
        tmp.block_size = MINLZ_MAX_BLOCK_SIZE;
    }
    if (in) {
        tmp = *in;
        if (tmp.level == 0 && encoding) {
            tmp.level = in->level;
        }
    }
    if (tmp.block_size == 0) {
        tmp.block_size = MINLZ_DEFAULT_BLOCK_SIZE;
    }
    if (tmp.block_size < MINLZ_MIN_BLOCK_SIZE || tmp.block_size > MINLZ_MAX_BLOCK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (tmp.level < MINLZ_LEVEL_SUPERFAST || tmp.level > MINLZ_LEVEL_SMALLEST) {
        return MINLZ_INVALID_LEVEL;
    }
    if (tmp.threads <= 0) {
        tmp.threads = 1;
    }
    if (tmp.sidecar_write_cb && !tmp.search) {
        return MINLZ_CORRUPT;
    }
    if (tmp.search &&
        minlz_search_config_resolve(&search_tmp, tmp.search,
                                    tmp.block_size) != MINLZ_OK) {
        return MINLZ_CORRUPT;
    }
    *out = tmp;
    return MINLZ_OK;
}

void minlz_stream_options_init(minlz_stream_options *opts) {
    if (!opts) {
        return;
    }
    opts->level = MINLZ_LEVEL_BALANCED;
    opts->block_size = MINLZ_DEFAULT_BLOCK_SIZE;
    opts->threads = 1;
    opts->add_index = 1;
    opts->padding = 0;
    opts->ignore_crc = 0;
    opts->allow_fallback = 0;
    opts->search = NULL;
    opts->sidecar_write_cb = NULL;
    opts->sidecar_write_ctx = NULL;
}

static minlz_status write_chunk_header(minlz_write_cb cb, void *ctx, uint8_t type, size_t len) {
    uint8_t hdr[4];
    if (len > MINLZ_MAX_USER_CHUNK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    hdr[0] = type;
    hdr[1] = (uint8_t)len;
    hdr[2] = (uint8_t)(len >> 8);
    hdr[3] = (uint8_t)(len >> 16);
    return minlz_write_all(cb, ctx, hdr, sizeof(hdr));
}

static minlz_status write_u32le(minlz_write_cb cb, void *ctx, uint32_t v) {
    uint8_t tmp[4];
    tmp[0] = (uint8_t)v;
    tmp[1] = (uint8_t)(v >> 8);
    tmp[2] = (uint8_t)(v >> 16);
    tmp[3] = (uint8_t)(v >> 24);
    return minlz_write_all(cb, ctx, tmp, sizeof(tmp));
}

static minlz_status write_stream_header(minlz_write_cb cb, void *ctx, size_t block_size,
                                        int64_t *written) {
    uint8_t hdr[10] = {
        MINLZ_CHUNK_STREAM_ID, 6, 0, 0, 'M', 'i', 'n', 'L', 'z', 0
    };
    unsigned lg = minlz_ceil_log2_size(block_size);
    if (lg < 10 || lg - 10 > 13) {
        return MINLZ_TOO_LARGE;
    }
    hdr[9] = (uint8_t)(lg - 10);
    *written += (int64_t)sizeof(hdr);
    return minlz_write_all(cb, ctx, hdr, sizeof(hdr));
}

static minlz_status append_padding(uint8_t **buf, size_t *len, size_t *cap,
                                   int64_t written, size_t multiple) {
    size_t leftover;
    size_t to_add;
    uint8_t hdr[4];
    uint8_t *p;
    if (multiple <= 1) {
        return MINLZ_OK;
    }
    leftover = (size_t)(written % (int64_t)multiple);
    if (leftover == 0) {
        return MINLZ_OK;
    }
    to_add = multiple - leftover;
    while (to_add < MINLZ_CHUNK_HEADER_SIZE) {
        to_add += multiple;
    }
    if (to_add >= MINLZ_MAX_BLOCK_SIZE + MINLZ_CHUNK_HEADER_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    if (*len + to_add > *cap) {
        size_t next = *cap ? *cap * 2 : 256;
        while (next < *len + to_add) {
            next *= 2;
        }
        p = (uint8_t *)realloc(*buf, next);
        if (!p) {
            return MINLZ_IO;
        }
        *buf = p;
        *cap = next;
    }
    hdr[0] = MINLZ_CHUNK_PADDING;
    hdr[1] = (uint8_t)(to_add - MINLZ_CHUNK_HEADER_SIZE);
    hdr[2] = (uint8_t)((to_add - MINLZ_CHUNK_HEADER_SIZE) >> 8);
    hdr[3] = (uint8_t)((to_add - MINLZ_CHUNK_HEADER_SIZE) >> 16);
    memcpy(*buf + *len, hdr, sizeof(hdr));
    memset(*buf + *len + sizeof(hdr), 0, to_add - sizeof(hdr));
    *len += to_add;
    return MINLZ_OK;
}

static minlz_status minlz_stream_encode_search(
    minlz_read_cb read_cb, void *read_ctx,
    minlz_write_cb write_cb, void *write_ctx,
    const minlz_stream_options *opts) {
    minlz_writer *writer = NULL;
    uint8_t buf[32768];
    minlz_status st =
        minlz_writer_create(&writer, write_cb, write_ctx, opts);
    if (st != MINLZ_OK) {
        return st;
    }
    for (;;) {
        size_t n = 0;
        size_t written = 0;
        st = read_cb(read_ctx, buf, sizeof(buf), &n);
        if (st != MINLZ_OK || n == 0u) {
            break;
        }
        st = minlz_writer_write(writer, buf, n, &written);
        if (st != MINLZ_OK || written != n) {
            if (st == MINLZ_OK) {
                st = MINLZ_IO;
            }
            break;
        }
    }
    if (st == MINLZ_OK) {
        st = minlz_writer_close(writer);
    }
    minlz_writer_destroy(writer);
    return st;
}

minlz_status minlz_stream_encode_ws(minlz_workspace *ws,
                                    minlz_read_cb read_cb, void *read_ctx,
                                    minlz_write_cb write_cb, void *write_ctx,
                                    const minlz_stream_options *opts_in) {
    minlz_stream_options opts;
    minlz_status st;
    uint8_t *in = NULL;
    uint8_t *out = NULL;
    size_t out_cap;
    int64_t written = 0;
    int64_t uncompressed = 0;
    minlz_index_builder index;
    minlz_workspace tmp;
    int use_tmp = 0;

    st = minlz_normalize_options(&opts, opts_in, 1);
    if (st != MINLZ_OK) {
        return st;
    }
    if (opts.search || opts.sidecar_write_cb) {
        (void)ws;
        return minlz_stream_encode_search(
            read_cb, read_ctx, write_cb, write_ctx, &opts);
    }
    out_cap = opts.block_size + 32;
    if (!ws) {
        memset(&tmp, 0, sizeof(tmp));
        ws = &tmp;
        use_tmp = 1;
    }
    st = minlz_workspace_prepare_buffer(ws, 0, opts.block_size, &in);
    if (st == MINLZ_OK) {
        st = minlz_workspace_prepare_buffer(ws, 1, out_cap, &out);
    }
    if (st != MINLZ_OK) {
        if (use_tmp) {
            minlz_workspace_cleanup(&tmp);
        }
        return st;
    }
    minlz_index_init(&index, opts.block_size);

    st = write_stream_header(write_cb, write_ctx, opts.block_size, &written);
    if (st != MINLZ_OK) {
        goto done;
    }

    for (;;) {
        size_t filled = 0;
        while (filled < opts.block_size) {
            size_t n = 0;
            st = read_cb(read_ctx, in + filled, opts.block_size - filled, &n);
            if (st != MINLZ_OK) {
                goto done;
            }
            if (n == 0) {
                break;
            }
            filled += n;
        }
        if (filled == 0) {
            break;
        }

        st = minlz_index_add(&index, written, uncompressed);
        if (st != MINLZ_OK) {
            goto done;
        }
        {
            uint32_t crc = minlz_crc(in, filled);
            size_t comp_len = 0;
            uint8_t chunk_type = MINLZ_CHUNK_UNCOMPRESSED;
            const uint8_t *payload = in;
            size_t payload_len = filled;

            if (opts.level != MINLZ_LEVEL_UNCOMPRESSED) {
                st = minlz_encode_stream_payload_ws(ws, out, out_cap, &comp_len, in, filled, opts.level);
                if (st == MINLZ_OK) {
                    chunk_type = MINLZ_CHUNK_MINLZ;
                    payload = out;
                    payload_len = comp_len;
                } else if (st != MINLZ_UNSUPPORTED) {
                    goto done;
                }
            }

            st = write_chunk_header(write_cb, write_ctx, chunk_type, payload_len + MINLZ_CHECKSUM_SIZE);
            if (st != MINLZ_OK ||
                (st = write_u32le(write_cb, write_ctx, crc)) != MINLZ_OK ||
                (st = minlz_write_all(write_cb, write_ctx, payload, payload_len)) != MINLZ_OK) {
                goto done;
            }
            written += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + MINLZ_CHECKSUM_SIZE + payload_len);
            uncompressed += (int64_t)filled;
        }
    }

    {
        uint8_t eof_buf[14];
        size_t n;
        eof_buf[0] = MINLZ_CHUNK_EOF;
        n = minlz_put_uvarint(eof_buf + 4, (uint64_t)uncompressed);
        eof_buf[1] = (uint8_t)n;
        eof_buf[2] = 0;
        eof_buf[3] = 0;
        st = minlz_write_all(write_cb, write_ctx, eof_buf, n + 4);
        if (st != MINLZ_OK) {
            goto done;
        }
        written += (int64_t)(n + 4);
    }

    if (opts.add_index || opts.padding > 1) {
        uint8_t *tail = NULL;
        size_t tail_len = 0;
        size_t tail_cap = 0;
        size_t padding_len = 0;
        if (opts.padding > 1) {
            st = append_padding(&tail, &tail_len, &tail_cap, written, opts.padding);
            if (st != MINLZ_OK) {
                free(tail);
                goto done;
            }
            padding_len = tail_len;
        }
        if (opts.add_index) {
            int64_t comp_total = opts.padding > 1 ? -1 : written;
            st = minlz_index_append(&tail, &tail_len, &tail_cap, &index, uncompressed, comp_total);
            if (st != MINLZ_OK) {
                free(tail);
                goto done;
            }
        }
        if (tail_len > 0) {
            st = minlz_write_all(write_cb, write_ctx, tail, tail_len);
            written += (int64_t)tail_len;
        }
        (void)padding_len;
        free(tail);
    }

done:
    minlz_index_free(&index);
    if (use_tmp) {
        minlz_workspace_cleanup(&tmp);
    }
    return st;
}

minlz_status minlz_stream_encode(minlz_read_cb read_cb, void *read_ctx,
                                 minlz_write_cb write_cb, void *write_ctx,
                                 const minlz_stream_options *opts_in) {
    return minlz_stream_encode_ws(NULL, read_cb, read_ctx, write_cb, write_ctx, opts_in);
}

minlz_status minlz_decode_stream_chunk(minlz_workspace *ws,
                                       const uint8_t *chunk, size_t chunk_len,
                                       uint8_t **decoded, size_t *decoded_len,
                                       int compressed_crc, int ignore_crc,
                                       size_t max_block) {
    uint32_t want_crc;
    size_t dlen = 0;
    size_t hlen = 0;
    minlz_status st;
    if (chunk_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    want_crc = minlz_load32(chunk);
    chunk += MINLZ_CHECKSUM_SIZE;
    chunk_len -= MINLZ_CHECKSUM_SIZE;
    st = minlz_payload_decoded_len(chunk, chunk_len, &dlen, &hlen);
    if (st != MINLZ_OK) {
        return st;
    }
    if (dlen > max_block) {
        return MINLZ_TOO_LARGE;
    }
    st = minlz_workspace_prepare_buffer(ws, 1, dlen ? dlen : 1, decoded);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_decode_payload(*decoded, dlen, chunk + hlen, chunk_len - hlen);
    if (st != MINLZ_OK) {
        *decoded = NULL;
        return st;
    }
    if (!ignore_crc) {
        uint32_t got = compressed_crc ? minlz_crc(chunk + hlen, chunk_len - hlen)
                                      : minlz_crc(*decoded, dlen);
        if (got != want_crc) {
            *decoded = NULL;
            return MINLZ_CRC;
        }
    }
    *decoded_len = dlen;
    return MINLZ_OK;
}

minlz_status minlz_stream_decode_ws(minlz_workspace *ws,
                                    minlz_read_cb read_cb, void *read_ctx,
                                    minlz_write_cb write_cb, void *write_ctx,
                                    const minlz_stream_options *opts_in) {
    minlz_stream_options opts;
    minlz_status st;
    uint8_t hdr[4];
    int have_header = 0;
    size_t max_block = MINLZ_MAX_BLOCK_SIZE;
    uint64_t total = 0;
    minlz_workspace tmp;
    int use_tmp = 0;

    st = minlz_normalize_options(&opts, opts_in, 0);
    if (st != MINLZ_OK) {
        return st;
    }
    if (!ws) {
        memset(&tmp, 0, sizeof(tmp));
        ws = &tmp;
        use_tmp = 1;
    }

    for (;;) {
        size_t got = 0;
        uint8_t chunk_type;
        size_t chunk_len;
        st = minlz_read_exact(read_cb, read_ctx, hdr, sizeof(hdr), 1, &got);
        if (st != MINLZ_OK) {
            goto decode_done;
        }
        if (got == 0) {
            st = MINLZ_OK;
            goto decode_done;
        }
        chunk_type = hdr[0];
        chunk_len = (size_t)hdr[1] | ((size_t)hdr[2] << 8) | ((size_t)hdr[3] << 16);

        if (!have_header && chunk_type != MINLZ_CHUNK_STREAM_ID) {
            if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK && chunk_type != MINLZ_CHUNK_EOF) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
        }

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            uint8_t id[6];
            if (chunk_len != 6) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
            st = minlz_read_exact(read_cb, read_ctx, id, sizeof(id), 0, NULL);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            if (memcmp(id, "MinLz", 5) == 0) {
                if ((id[5] >> 6) != 0 || (id[5] & 15u) > 13) {
                    st = MINLZ_CORRUPT;
                    goto decode_done;
                }
                max_block = (size_t)1u << ((id[5] & 15u) + 10u);
                if (max_block > opts.block_size) {
                    max_block = opts.block_size;
                }
                have_header = 1;
                total = 0;
                continue;
            }
            if ((memcmp(id, "sNaPpY", 6) == 0 || memcmp(id, "S2sTwO", 6) == 0) &&
                opts.allow_fallback) {
                st = MINLZ_UNSUPPORTED;
                goto decode_done;
            }
            st = MINLZ_UNSUPPORTED;
            goto decode_done;
        }

        if (chunk_type == MINLZ_CHUNK_EOF) {
            uint8_t eof_buf[10];
            if (chunk_len > sizeof(eof_buf)) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
            if (chunk_len != 0) {
                size_t want = 0;
                size_t used = 0;
                st = minlz_read_exact(
                    read_cb, read_ctx, eof_buf, chunk_len, 0, NULL);
                if (st != MINLZ_OK) {
                    goto decode_done;
                }
                st = minlz_read_uvarint(
                    eof_buf, chunk_len, &want, &used);
                if (st != MINLZ_OK || used != chunk_len || (uint64_t)want != total) {
                    st = MINLZ_CORRUPT;
                    goto decode_done;
                }
            }
            have_header = 0;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
            uint8_t *chunk;
            uint32_t want_crc;
            size_t n;
            if (chunk_len < MINLZ_CHECKSUM_SIZE) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
            n = chunk_len - MINLZ_CHECKSUM_SIZE;
            if (n > max_block) {
                st = MINLZ_TOO_LARGE;
                goto decode_done;
            }
            st = minlz_workspace_prepare_buffer(ws, 2, chunk_len ? chunk_len : 1, &chunk);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            st = minlz_read_exact(read_cb, read_ctx, chunk, chunk_len, 0, NULL);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            want_crc = minlz_load32(chunk);
            if (!opts.ignore_crc && minlz_crc(chunk + MINLZ_CHECKSUM_SIZE, n) != want_crc) {
                st = MINLZ_CRC;
                goto decode_done;
            }
            st = minlz_write_all(write_cb, write_ctx, chunk + MINLZ_CHECKSUM_SIZE, n);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            total += n;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_MINLZ || chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            uint8_t *chunk;
            uint8_t *decoded = NULL;
            size_t decoded_len = 0;
            st = minlz_workspace_prepare_buffer(ws, 2, chunk_len ? chunk_len : 1, &chunk);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            st = minlz_read_exact(read_cb, read_ctx, chunk, chunk_len, 0, NULL);
            if (st == MINLZ_OK) {
                st = minlz_decode_stream_chunk(
                    ws, chunk, chunk_len, &decoded, &decoded_len,
                    chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC,
                    opts.ignore_crc, max_block);
            }
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            st = minlz_write_all(write_cb, write_ctx, decoded, decoded_len);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            total += decoded_len;
            continue;
        }

        if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
            (chunk_type >= 0xc0u && chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            st = MINLZ_UNSUPPORTED;
            goto decode_done;
        }
        st = minlz_skip_exact(read_cb, read_ctx, chunk_len);
        if (st != MINLZ_OK) {
            goto decode_done;
        }
    }

decode_done:
    if (use_tmp) {
        minlz_workspace_cleanup(&tmp);
    }
    return st;
}

minlz_status minlz_stream_decode(minlz_read_cb read_cb, void *read_ctx,
                                 minlz_write_cb write_cb, void *write_ctx,
                                 const minlz_stream_options *opts_in) {
    return minlz_stream_decode_ws(NULL, read_cb, read_ctx, write_cb, write_ctx, opts_in);
}

struct minlz_writer {
    minlz_stream_options opts;
    minlz_write_cb write_cb;
    void *write_ctx;
    minlz_workspace *ws;
    minlz_workspace own_ws;
    int use_own_ws;
    uint8_t *in;
    size_t in_len;
    size_t in_cap;
    uint8_t *out;
    size_t out_cap;
    int header_written;
    int closed;
    int64_t written;
    int64_t uncompressed;
    minlz_index_builder index;
    minlz_search_config search_config;
    size_t search_overlap;
    size_t stream_block_size;
    int has_search;
    minlz_write_cb sidecar_write_cb;
    void *sidecar_write_ctx;
    int sidecar_header_written;
    int64_t sidecar_written;
};

typedef struct minlz_mem_write {
    uint8_t *data;
    size_t len;
    size_t cap;
} minlz_mem_write;

typedef struct minlz_const_mem_read {
    const uint8_t *data;
    size_t len;
    size_t pos;
} minlz_const_mem_read;

typedef struct minlz_readseeker_cache_block {
    int64_t start;
    uint64_t tick;
    uint8_t *data;
    size_t len;
    size_t cap;
#if MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER
    uint8_t *storage;
#endif
} minlz_readseeker_cache_block;

struct minlz_reader {
    minlz_stream_options opts;
    minlz_read_cb read_cb;
    minlz_seek_cb seek_cb;
    void *read_ctx;
    minlz_workspace *ws;
    minlz_workspace own_ws;
    int use_own_ws;
    uint8_t *decoded;
    size_t decoded_len;
    size_t decoded_cap;
    size_t pos;
    int decoded_ready;
    minlz_status err;
    minlz_user_chunk_cb user_cb[MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK -
                                MINLZ_MIN_USER_SKIPPABLE_CHUNK + 1u];
    void *user_ctx[MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK -
                   MINLZ_MIN_USER_SKIPPABLE_CHUNK + 1u];
};

struct minlz_readseeker {
    minlz_reader *reader;
    minlz_index *index;
    int seekable;
    int64_t pos;
    int64_t total_uncompressed;
    int64_t block_start;
    uint8_t *block;
    size_t block_len;
    size_t block_cap;
    uint64_t cache_tick;
    minlz_readseeker_cache_block cache[MINLZ_READSEEKER_CACHE_BLOCKS];
};

static minlz_status mem_write_cb(void *ctx, const uint8_t *src, size_t len) {
    minlz_mem_write *w = (minlz_mem_write *)ctx;
    if (w->len + len < w->len) {
        return MINLZ_TOO_LARGE;
    }
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
    if (len) {
        memcpy(w->data + w->len, src, len);
        w->len += len;
    }
    return MINLZ_OK;
}

static minlz_status const_mem_read_cb(void *ctx, uint8_t *dst, size_t cap, size_t *nread) {
    minlz_const_mem_read *r = (minlz_const_mem_read *)ctx;
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

static minlz_status writer_prepare(minlz_writer *w) {
    minlz_status st;
    size_t input_cap;
    if (!w || !w->write_cb) {
        return MINLZ_IO;
    }
    if (!w->ws) {
        memset(&w->own_ws, 0, sizeof(w->own_ws));
        w->ws = &w->own_ws;
        w->use_own_ws = 1;
    }
    input_cap = w->opts.block_size + w->search_overlap;
    if (input_cap < w->opts.block_size) {
        return MINLZ_TOO_LARGE;
    }
    st = minlz_workspace_prepare_buffer(w->ws, 0, input_cap, &w->in);
    if (st != MINLZ_OK) {
        return st;
    }
    w->in_cap = input_cap;
    w->out_cap = w->opts.block_size + 32;
    return minlz_workspace_prepare_buffer(w->ws, 1, w->out_cap, &w->out);
}

static minlz_status writer_write_search_info(minlz_writer *w,
                                             minlz_write_cb write_cb,
                                             void *write_ctx,
                                             int64_t *written) {
    uint8_t *chunk = NULL;
    size_t chunk_len = 0;
    minlz_status st =
        minlz_search_make_info_chunk(&w->search_config, &chunk, &chunk_len);
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_write_all(write_cb, write_ctx, chunk, chunk_len);
    if (st == MINLZ_OK && written) {
        *written += (int64_t)chunk_len;
    }
    free(chunk);
    return st;
}

static minlz_status writer_ensure_sidecar_header(minlz_writer *w) {
    minlz_status st;
    if (!w->sidecar_write_cb || w->sidecar_header_written) {
        return MINLZ_OK;
    }
    st = write_stream_header(w->sidecar_write_cb, w->sidecar_write_ctx,
                             w->opts.block_size, &w->sidecar_written);
    if (st == MINLZ_OK) {
        st = writer_write_search_info(
            w, w->sidecar_write_cb, w->sidecar_write_ctx,
            &w->sidecar_written);
    }
    if (st == MINLZ_OK) {
        w->sidecar_header_written = 1;
    }
    return st;
}

static minlz_status writer_ensure_header(minlz_writer *w) {
    minlz_status st;
    if (w->header_written) {
        return MINLZ_OK;
    }
    st = write_stream_header(w->write_cb, w->write_ctx, w->opts.block_size, &w->written);
    if (st == MINLZ_OK && w->has_search && !w->sidecar_write_cb) {
        st = writer_write_search_info(
            w, w->write_cb, w->write_ctx, &w->written);
    }
    if (st == MINLZ_OK) {
        w->header_written = 1;
    }
    return st;
}

static minlz_status writer_emit_search_table(minlz_writer *w,
                                             const uint8_t *src, size_t len,
                                             const uint8_t *overlap,
                                             size_t overlap_len) {
    uint8_t *chunk = NULL;
    size_t chunk_len = 0;
    minlz_write_cb write_cb =
        w->sidecar_write_cb ? w->sidecar_write_cb : w->write_cb;
    void *write_ctx =
        w->sidecar_write_cb ? w->sidecar_write_ctx : w->write_ctx;
    int64_t *written =
        w->sidecar_write_cb ? &w->sidecar_written : &w->written;
    minlz_status st = minlz_search_make_table_chunk(
        &w->search_config, src, len, overlap, overlap_len,
        &chunk, &chunk_len);
    if (st == MINLZ_UNSUPPORTED) {
        return MINLZ_OK;
    }
    if (st != MINLZ_OK) {
        return st;
    }
    st = minlz_write_all(write_cb, write_ctx, chunk, chunk_len);
    if (st == MINLZ_OK) {
        *written += (int64_t)chunk_len;
    }
    free(chunk);
    return st;
}

static minlz_status writer_emit_remote_ref(minlz_writer *w,
                                           int64_t block_offset,
                                           size_t block_len) {
    uint8_t payload[20];
    size_t n = 0;
    minlz_status st;
    if (!w->sidecar_write_cb || block_offset < 0 ||
        block_len == 0 || block_len > w->stream_block_size) {
        return MINLZ_CORRUPT;
    }
    n += minlz_put_uvarint(payload + n, (uint64_t)block_offset);
    n += minlz_put_uvarint(payload + n,
                           (uint64_t)(w->stream_block_size - block_len));
    st = write_chunk_header(w->sidecar_write_cb, w->sidecar_write_ctx,
                            MINLZ_CHUNK_REMOTE_BLOCK_REF, n);
    if (st == MINLZ_OK) {
        st = minlz_write_all(w->sidecar_write_cb, w->sidecar_write_ctx,
                             payload, n);
    }
    if (st == MINLZ_OK) {
        w->sidecar_written += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + n);
    }
    return st;
}

static minlz_status writer_flush_block_ex(minlz_writer *w,
                                          const uint8_t *src, size_t len,
                                          const uint8_t *overlap,
                                          size_t overlap_len,
                                          int allow_search_table) {
    uint32_t crc;
    size_t comp_len = 0;
    uint8_t chunk_type = MINLZ_CHUNK_UNCOMPRESSED;
    const uint8_t *payload = src;
    size_t payload_len = len;
    int64_t main_block_offset;
    minlz_status st;

    if (len == 0) {
        return MINLZ_OK;
    }
    st = writer_ensure_header(w);
    if (st != MINLZ_OK) {
        return st;
    }
    crc = minlz_crc(src, len);
    if (w->opts.level != MINLZ_LEVEL_UNCOMPRESSED) {
        st = minlz_encode_stream_payload_ws(w->ws, w->out, w->out_cap, &comp_len,
                                            src, len, w->opts.level);
        if (st == MINLZ_OK) {
            chunk_type = MINLZ_CHUNK_MINLZ;
            payload = w->out;
            payload_len = comp_len;
        } else if (st != MINLZ_UNSUPPORTED) {
            return st;
        }
    }
    main_block_offset = w->written;
    if (w->has_search && w->sidecar_write_cb) {
        st = writer_ensure_sidecar_header(w);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (allow_search_table && w->has_search &&
        (chunk_type == MINLZ_CHUNK_MINLZ ||
         w->search_config.table_type != MINLZ_SEARCH_TABLE_NO_PREFIX)) {
        st = writer_emit_search_table(w, src, len, overlap, overlap_len);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    if (w->sidecar_write_cb) {
        st = writer_emit_remote_ref(w, main_block_offset, len);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    st = minlz_index_add(&w->index, w->written, w->uncompressed);
    if (st != MINLZ_OK) {
        return st;
    }
    st = write_chunk_header(w->write_cb, w->write_ctx, chunk_type,
                            payload_len + MINLZ_CHECKSUM_SIZE);
    if (st != MINLZ_OK ||
        (st = write_u32le(w->write_cb, w->write_ctx, crc)) != MINLZ_OK ||
        (st = minlz_write_all(w->write_cb, w->write_ctx, payload, payload_len)) != MINLZ_OK) {
        return st;
    }
    w->written += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + MINLZ_CHECKSUM_SIZE + payload_len);
    w->uncompressed += (int64_t)len;
    return MINLZ_OK;
}

static minlz_status writer_flush_block(minlz_writer *w,
                                       const uint8_t *src, size_t len) {
    return writer_flush_block_ex(w, src, len, NULL, 0, 0);
}

static minlz_status writer_flush_pending(minlz_writer *w, int final) {
    while (w->in_len != 0u) {
        size_t block_len =
            w->in_len > w->opts.block_size ? w->opts.block_size : w->in_len;
        size_t remaining = w->in_len - block_len;
        size_t overlap_len = remaining;
        int allow_table;
        minlz_status st;
        if (overlap_len > w->search_overlap) {
            overlap_len = w->search_overlap;
        }
        allow_table = final ||
                      !w->has_search ||
                      overlap_len == w->search_overlap;
        st = writer_flush_block_ex(
            w, w->in, block_len,
            overlap_len ? w->in + block_len : NULL,
            overlap_len, allow_table);
        if (st != MINLZ_OK) {
            return st;
        }
        if (remaining != 0u) {
            memmove(w->in, w->in + block_len, remaining);
        }
        w->in_len = remaining;
    }
    return MINLZ_OK;
}

minlz_status minlz_writer_create(minlz_writer **writer, minlz_write_cb write_cb,
                                 void *write_ctx, const minlz_stream_options *opts_in) {
    minlz_writer *w;
    minlz_status st;
    if (!writer) {
        return MINLZ_CORRUPT;
    }
    *writer = NULL;
    w = (minlz_writer *)calloc(1, sizeof(*w));
    if (!w) {
        return MINLZ_IO;
    }
    st = minlz_normalize_options(&w->opts, opts_in, 1);
    if (st != MINLZ_OK) {
        free(w);
        return st;
    }
    if (w->opts.search) {
        st = minlz_search_config_resolve(
            &w->search_config, w->opts.search, w->opts.block_size);
        if (st != MINLZ_OK) {
            free(w);
            return st;
        }
        w->has_search = 1;
        w->search_overlap =
            minlz_search_overlap_bytes(&w->search_config);
        w->opts.search = &w->search_config;
    }
    w->sidecar_write_cb = w->opts.sidecar_write_cb;
    w->sidecar_write_ctx = w->opts.sidecar_write_ctx;
    w->stream_block_size =
        (size_t)1u << minlz_ceil_log2_size(w->opts.block_size);
    w->write_cb = write_cb;
    w->write_ctx = write_ctx;
    minlz_index_init(&w->index, w->opts.block_size);
    st = writer_prepare(w);
    if (st != MINLZ_OK) {
        minlz_index_free(&w->index);
        free(w);
        return st;
    }
    *writer = w;
    return MINLZ_OK;
}

void minlz_writer_destroy(minlz_writer *writer) {
    if (!writer) {
        return;
    }
    minlz_index_free(&writer->index);
    if (writer->use_own_ws) {
        minlz_workspace_cleanup(&writer->own_ws);
    }
    free(writer);
}

minlz_status minlz_writer_reset(minlz_writer *writer, minlz_write_cb write_cb,
                                void *write_ctx) {
    if (!writer) {
        return MINLZ_CORRUPT;
    }
    writer->write_cb = write_cb;
    writer->write_ctx = write_ctx;
    writer->in_len = 0;
    writer->header_written = 0;
    writer->closed = 0;
    writer->written = 0;
    writer->uncompressed = 0;
    writer->sidecar_header_written = 0;
    writer->sidecar_written = 0;
    minlz_index_free(&writer->index);
    minlz_index_init(&writer->index, writer->opts.block_size);
    return writer_prepare(writer);
}

minlz_status minlz_writer_write(minlz_writer *writer, const uint8_t *src, size_t len,
                                size_t *written) {
    size_t done = 0;
    minlz_status st = MINLZ_OK;
    if (written) {
        *written = 0;
    }
    if (!writer || (!src && len != 0) || writer->closed) {
        return MINLZ_CORRUPT;
    }
    if (writer->has_search) {
        while (done < len) {
            size_t ready = writer->opts.block_size +
                           writer->search_overlap;
            if (writer->in_len == 0u && len - done >= ready) {
                st = writer_flush_block_ex(
                    writer, src + done, writer->opts.block_size,
                    src + done + writer->opts.block_size,
                    writer->search_overlap, 1);
                if (st != MINLZ_OK) {
                    return st;
                }
                done += writer->opts.block_size;
                continue;
            }
            {
                size_t n = writer->in_cap - writer->in_len;
                if (n > len - done) {
                    n = len - done;
                }
                memcpy(writer->in + writer->in_len, src + done, n);
                writer->in_len += n;
                done += n;
            }
            if (writer->in_len >= ready) {
                size_t remaining;
                st = writer_flush_block_ex(
                    writer, writer->in, writer->opts.block_size,
                    writer->in + writer->opts.block_size,
                    writer->search_overlap, 1);
                if (st != MINLZ_OK) {
                    return st;
                }
                remaining = writer->in_len - writer->opts.block_size;
                memmove(writer->in,
                        writer->in + writer->opts.block_size,
                        remaining);
                writer->in_len = remaining;
            }
        }
        if (written) {
            *written = done;
        }
        return MINLZ_OK;
    }
    while (done < len) {
        if (writer->in_len == 0 && len - done >= writer->opts.block_size) {
            st = writer_flush_block(writer, src + done, writer->opts.block_size);
            if (st != MINLZ_OK) {
                return st;
            }
            done += writer->opts.block_size;
            continue;
        }
        {
            size_t n = writer->opts.block_size - writer->in_len;
            if (n > len - done) {
                n = len - done;
            }
            memcpy(writer->in + writer->in_len, src + done, n);
            writer->in_len += n;
            done += n;
            if (writer->in_len == writer->opts.block_size) {
                st = writer_flush_block(writer, writer->in, writer->in_len);
                if (st != MINLZ_OK) {
                    return st;
                }
                writer->in_len = 0;
            }
        }
    }
    if (written) {
        *written = done;
    }
    return st;
}

minlz_status minlz_writer_read_from(minlz_writer *writer, minlz_read_cb read_cb,
                                    void *read_ctx, uint64_t *read_total) {
    uint8_t tmp[32768];
    uint64_t total = 0;
    if (read_total) {
        *read_total = 0;
    }
    for (;;) {
        size_t n = 0;
        size_t wrote = 0;
        minlz_status st = read_cb(read_ctx, tmp, sizeof(tmp), &n);
        if (st != MINLZ_OK) {
            return st;
        }
        if (n == 0) {
            break;
        }
        st = minlz_writer_write(writer, tmp, n, &wrote);
        if (st != MINLZ_OK || wrote != n) {
            return st == MINLZ_OK ? MINLZ_IO : st;
        }
        total += n;
    }
    if (read_total) {
        *read_total = total;
    }
    return MINLZ_OK;
}

minlz_status minlz_writer_encode_buffer(minlz_writer *writer, const uint8_t *src,
                                        size_t len) {
    minlz_status st;
    if (!writer) {
        return MINLZ_CORRUPT;
    }
    st = minlz_writer_flush(writer);
    if (st != MINLZ_OK) {
        return st;
    }
    if (writer->has_search) {
        while (len > 0) {
            size_t n =
                len > writer->opts.block_size ? writer->opts.block_size : len;
            size_t overlap_len = len - n;
            if (overlap_len > writer->search_overlap) {
                overlap_len = writer->search_overlap;
            }
            st = writer_flush_block_ex(
                writer, src, n,
                overlap_len ? src + n : NULL, overlap_len, 1);
            if (st != MINLZ_OK) {
                return st;
            }
            src += n;
            len -= n;
        }
        return MINLZ_OK;
    }
    while (len > 0) {
        size_t n = len > writer->opts.block_size ? writer->opts.block_size : len;
        st = writer_flush_block(writer, src, n);
        if (st != MINLZ_OK) {
            return st;
        }
        src += n;
        len -= n;
    }
    return MINLZ_OK;
}

minlz_status minlz_writer_add_user_chunk(minlz_writer *writer, uint8_t id,
                                         const uint8_t *src, size_t len) {
    minlz_status st;
    if (!writer || (!src && len != 0) || id < MINLZ_MIN_USER_SKIPPABLE_CHUNK ||
        id > MINLZ_CHUNK_PADDING || len > MINLZ_MAX_USER_CHUNK_SIZE) {
        return MINLZ_CORRUPT;
    }
    st = minlz_writer_flush(writer);
    if (st != MINLZ_OK || (st = writer_ensure_header(writer)) != MINLZ_OK) {
        return st;
    }
    if (writer->uncompressed > 0) {
        st = minlz_index_add(&writer->index, writer->written, writer->uncompressed);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    st = write_chunk_header(writer->write_cb, writer->write_ctx, id, len);
    if (st != MINLZ_OK ||
        (st = minlz_write_all(writer->write_cb, writer->write_ctx, src, len)) != MINLZ_OK) {
        return st;
    }
    writer->written += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + len);
    return MINLZ_OK;
}

minlz_status minlz_writer_async_flush(minlz_writer *writer) {
    return minlz_writer_flush(writer);
}

minlz_status minlz_writer_flush(minlz_writer *writer) {
    minlz_status st;
    if (!writer || writer->closed) {
        return !writer ? MINLZ_CORRUPT : MINLZ_OK;
    }
    st = writer->has_search
             ? writer_flush_pending(writer, 0)
             : writer_flush_block(writer, writer->in, writer->in_len);
    if (st == MINLZ_OK) {
        writer->in_len = 0;
    }
    return st;
}

minlz_status minlz_writer_close_index(minlz_writer *writer, uint8_t **index,
                                      size_t *index_len) {
    minlz_status st;
    uint8_t tmp[14];
    size_t n;
    uint8_t *tail = NULL;
    size_t tail_len = 0;
    size_t tail_cap = 0;
    size_t pad_len = 0;
    if (index) {
        *index = NULL;
    }
    if (index_len) {
        *index_len = 0;
    }
    if (!writer) {
        return MINLZ_CORRUPT;
    }
    if (writer->closed) {
        return MINLZ_OK;
    }
    st = writer->has_search
             ? writer_flush_pending(writer, 1)
             : minlz_writer_flush(writer);
    if (st != MINLZ_OK || (st = writer_ensure_header(writer)) != MINLZ_OK) {
        return st;
    }
    tmp[0] = MINLZ_CHUNK_EOF;
    n = minlz_put_uvarint(tmp + 4, (uint64_t)writer->uncompressed);
    tmp[1] = (uint8_t)n;
    tmp[2] = 0;
    tmp[3] = 0;
    st = minlz_write_all(writer->write_cb, writer->write_ctx, tmp, n + 4);
    if (st != MINLZ_OK) {
        return st;
    }
    writer->written += (int64_t)(n + 4);
    if (writer->sidecar_write_cb) {
        st = writer_ensure_sidecar_header(writer);
        if (st != MINLZ_OK) {
            return st;
        }
        tmp[0] = MINLZ_CHUNK_EOF;
        n = minlz_put_uvarint(tmp + 4, 0);
        tmp[1] = (uint8_t)n;
        tmp[2] = 0;
        tmp[3] = 0;
        st = minlz_write_all(writer->sidecar_write_cb,
                             writer->sidecar_write_ctx, tmp, n + 4);
        if (st != MINLZ_OK) {
            return st;
        }
        writer->sidecar_written += (int64_t)(n + 4);
    }
    if (writer->opts.padding > 1) {
        st = append_padding(&tail, &tail_len, &tail_cap, writer->written, writer->opts.padding);
        if (st != MINLZ_OK) {
            free(tail);
            return st;
        }
        pad_len = tail_len;
    }
    if (writer->opts.add_index || index) {
        int64_t comp_total = writer->opts.padding > 1 ? -1 : writer->written;
        st = minlz_index_append(&tail, &tail_len, &tail_cap, &writer->index,
                                writer->uncompressed, comp_total);
        if (st != MINLZ_OK) {
            free(tail);
            return st;
        }
        if (index && index_len) {
            size_t idx_len = tail_len - pad_len;
            *index = (uint8_t *)malloc(idx_len ? idx_len : 1);
            if (!*index) {
                free(tail);
                return MINLZ_IO;
            }
            memcpy(*index, tail + pad_len, idx_len);
            *index_len = idx_len;
        }
    }
    if (tail_len > 0) {
        size_t to_write = writer->opts.add_index ? tail_len : pad_len;
        if (to_write > 0) {
            st = minlz_write_all(writer->write_cb, writer->write_ctx, tail, to_write);
            if (st != MINLZ_OK) {
                free(tail);
                return st;
            }
            writer->written += (int64_t)to_write;
        }
    }
    free(tail);
    writer->closed = 1;
    return MINLZ_OK;
}

minlz_status minlz_writer_close(minlz_writer *writer) {
    return minlz_writer_close_index(writer, NULL, NULL);
}

minlz_status minlz_writer_written(const minlz_writer *writer, int64_t *input,
                                  int64_t *output) {
    if (!writer) {
        return MINLZ_CORRUPT;
    }
    if (input) {
        *input = writer->uncompressed + (int64_t)writer->in_len;
    }
    if (output) {
        *output = writer->written;
    }
    return MINLZ_OK;
}

minlz_status minlz_writer_set_search_table(
    minlz_writer *writer, const minlz_search_config *config) {
    minlz_status st;
    if (!writer || writer->closed || writer->header_written ||
        writer->in_len != 0u || writer->uncompressed != 0) {
        return MINLZ_CORRUPT;
    }
    if (!config) {
        if (writer->sidecar_write_cb) {
            return MINLZ_CORRUPT;
        }
        writer->has_search = 0;
        writer->search_overlap = 0;
        writer->opts.search = NULL;
        return writer_prepare(writer);
    }
    st = minlz_search_config_resolve(
        &writer->search_config, config, writer->opts.block_size);
    if (st != MINLZ_OK) {
        return st;
    }
    writer->has_search = 1;
    writer->search_overlap =
        minlz_search_overlap_bytes(&writer->search_config);
    writer->opts.search = &writer->search_config;
    return writer_prepare(writer);
}

minlz_status minlz_writer_set_sidecar(minlz_writer *writer,
                                      minlz_write_cb write_cb,
                                      void *write_ctx) {
    if (!writer || writer->closed || writer->header_written ||
        writer->in_len != 0u || writer->uncompressed != 0 ||
        (write_cb && !writer->has_search)) {
        return MINLZ_CORRUPT;
    }
    writer->sidecar_write_cb = write_cb;
    writer->sidecar_write_ctx = write_ctx;
    writer->opts.sidecar_write_cb = write_cb;
    writer->opts.sidecar_write_ctx = write_ctx;
    writer->sidecar_header_written = 0;
    writer->sidecar_written = 0;
    return MINLZ_OK;
}

#if MINLZ_STREAM_PTHREADS
typedef struct minlz_decode_job {
    struct minlz_decode_job *next;
    uint64_t seq;
    uint8_t chunk_type;
    uint8_t *input;
    size_t input_len;
    uint8_t *output;
    size_t output_len;
    int output_owned;
    int ignore_crc;
    size_t max_block;
    minlz_status status;
} minlz_decode_job;

typedef struct minlz_decode_pool {
    pthread_t *threads;
    int thread_count;
    pthread_mutex_t mu;
    pthread_cond_t work_cv;
    pthread_cond_t done_cv;
    minlz_decode_job *work_head;
    minlz_decode_job *work_tail;
    minlz_decode_job *done_head;
    minlz_decode_job *done_tail;
    int stop;
} minlz_decode_pool;
#else
typedef struct minlz_decode_job {
    struct minlz_decode_job *next;
    uint64_t seq;
    uint8_t chunk_type;
    uint8_t *input;
    size_t input_len;
    uint8_t *output;
    size_t output_len;
    int output_owned;
    int ignore_crc;
    size_t max_block;
    minlz_status status;
} minlz_decode_job;
#endif

static void minlz_decode_job_free(minlz_decode_job *job) {
    if (!job) {
        return;
    }
    if (job->output_owned) {
        free(job->output);
    }
    free(job->input);
    free(job);
}

static void minlz_decode_job_list_free(minlz_decode_job *job) {
    while (job) {
        minlz_decode_job *next = job->next;
        minlz_decode_job_free(job);
        job = next;
    }
}

static minlz_status minlz_decode_job_process(minlz_decode_job *job) {
    uint32_t want_crc;
    if (!job || !job->input || job->input_len < MINLZ_CHECKSUM_SIZE) {
        return MINLZ_CORRUPT;
    }
    want_crc = minlz_load32(job->input);
    if (job->chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
        size_t n = job->input_len - MINLZ_CHECKSUM_SIZE;
        if (n > job->max_block) {
            return MINLZ_TOO_LARGE;
        }
        if (!job->ignore_crc &&
            minlz_crc(job->input + MINLZ_CHECKSUM_SIZE, n) != want_crc) {
            return MINLZ_CRC;
        }
        job->output = job->input + MINLZ_CHECKSUM_SIZE;
        job->output_len = n;
        job->output_owned = 0;
        return MINLZ_OK;
    }
    if (job->chunk_type == MINLZ_CHUNK_MINLZ ||
        job->chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
        const uint8_t *payload = job->input + MINLZ_CHECKSUM_SIZE;
        size_t payload_len = job->input_len - MINLZ_CHECKSUM_SIZE;
        size_t decoded_len = 0;
        size_t header_len = 0;
        minlz_status st = minlz_payload_decoded_len(payload, payload_len,
                                                    &decoded_len, &header_len);
        uint8_t *out;
        if (st != MINLZ_OK) {
            return st;
        }
        if (decoded_len > job->max_block) {
            return MINLZ_TOO_LARGE;
        }
        out = (uint8_t *)malloc(decoded_len ? decoded_len : 1u);
        if (!out) {
            return MINLZ_IO;
        }
        st = minlz_decode_payload(out, decoded_len, payload + header_len,
                                  payload_len - header_len);
        if (st != MINLZ_OK) {
            free(out);
            return st;
        }
        if (!job->ignore_crc) {
            const uint8_t *crc_data = out;
            size_t crc_len = decoded_len;
            if (job->chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
                crc_data = payload + header_len;
                crc_len = payload_len - header_len;
            }
            if (minlz_crc(crc_data, crc_len) != want_crc) {
                free(out);
                return MINLZ_CRC;
            }
        }
        job->output = out;
        job->output_len = decoded_len;
        job->output_owned = 1;
        return MINLZ_OK;
    }
    return MINLZ_CORRUPT;
}

#if MINLZ_STREAM_PTHREADS
static void minlz_decode_pool_push_done(minlz_decode_pool *pool, minlz_decode_job *job) {
    job->next = NULL;
    if (pool->done_tail) {
        pool->done_tail->next = job;
    } else {
        pool->done_head = job;
    }
    pool->done_tail = job;
    pthread_cond_signal(&pool->done_cv);
}

static void *minlz_decode_worker_main(void *arg) {
    minlz_decode_pool *pool = (minlz_decode_pool *)arg;
    for (;;) {
        minlz_decode_job *job;
        pthread_mutex_lock(&pool->mu);
        while (!pool->work_head && !pool->stop) {
            pthread_cond_wait(&pool->work_cv, &pool->mu);
        }
        if (!pool->work_head && pool->stop) {
            pthread_mutex_unlock(&pool->mu);
            break;
        }
        job = pool->work_head;
        pool->work_head = job->next;
        if (!pool->work_head) {
            pool->work_tail = NULL;
        }
        pthread_mutex_unlock(&pool->mu);

        job->next = NULL;
        job->status = minlz_decode_job_process(job);

        pthread_mutex_lock(&pool->mu);
        minlz_decode_pool_push_done(pool, job);
        pthread_mutex_unlock(&pool->mu);
    }
    return NULL;
}

static minlz_status minlz_decode_pool_start(minlz_decode_pool *pool, int threads) {
    int i;
    memset(pool, 0, sizeof(*pool));
    if (threads < 2) {
        return MINLZ_CORRUPT;
    }
    pool->threads = (pthread_t *)calloc((size_t)threads, sizeof(pool->threads[0]));
    if (!pool->threads) {
        return MINLZ_IO;
    }
    if (pthread_mutex_init(&pool->mu, NULL) != 0 ||
        pthread_cond_init(&pool->work_cv, NULL) != 0 ||
        pthread_cond_init(&pool->done_cv, NULL) != 0) {
        free(pool->threads);
        memset(pool, 0, sizeof(*pool));
        return MINLZ_IO;
    }
    for (i = 0; i < threads; i++) {
        if (pthread_create(&pool->threads[i], NULL, minlz_decode_worker_main, pool) != 0) {
            pthread_mutex_lock(&pool->mu);
            pool->stop = 1;
            pthread_cond_broadcast(&pool->work_cv);
            pthread_mutex_unlock(&pool->mu);
            while (i-- > 0) {
                pthread_join(pool->threads[i], NULL);
            }
            pthread_mutex_destroy(&pool->mu);
            pthread_cond_destroy(&pool->work_cv);
            pthread_cond_destroy(&pool->done_cv);
            free(pool->threads);
            memset(pool, 0, sizeof(*pool));
            return MINLZ_IO;
        }
        pool->thread_count++;
    }
    return MINLZ_OK;
}

static void minlz_decode_pool_submit(minlz_decode_pool *pool, minlz_decode_job *job) {
    job->next = NULL;
    pthread_mutex_lock(&pool->mu);
    if (pool->work_tail) {
        pool->work_tail->next = job;
    } else {
        pool->work_head = job;
    }
    pool->work_tail = job;
    pthread_cond_signal(&pool->work_cv);
    pthread_mutex_unlock(&pool->mu);
}

static minlz_decode_job *minlz_decode_pool_wait_done(minlz_decode_pool *pool) {
    minlz_decode_job *job;
    pthread_mutex_lock(&pool->mu);
    while (!pool->done_head) {
        pthread_cond_wait(&pool->done_cv, &pool->mu);
    }
    job = pool->done_head;
    pool->done_head = job->next;
    if (!pool->done_head) {
        pool->done_tail = NULL;
    }
    pthread_mutex_unlock(&pool->mu);
    job->next = NULL;
    return job;
}

static void minlz_decode_pool_destroy(minlz_decode_pool *pool) {
    int i;
    if (!pool || !pool->threads) {
        return;
    }
    pthread_mutex_lock(&pool->mu);
    pool->stop = 1;
    pthread_cond_broadcast(&pool->work_cv);
    pthread_mutex_unlock(&pool->mu);
    for (i = 0; i < pool->thread_count; i++) {
        pthread_join(pool->threads[i], NULL);
    }
    minlz_decode_job_list_free(pool->work_head);
    minlz_decode_job_list_free(pool->done_head);
    pthread_mutex_destroy(&pool->mu);
    pthread_cond_destroy(&pool->work_cv);
    pthread_cond_destroy(&pool->done_cv);
    free(pool->threads);
    memset(pool, 0, sizeof(*pool));
}
#endif

static minlz_decode_job *minlz_decode_completed_pop(minlz_decode_job **head,
                                                    uint64_t seq) {
    minlz_decode_job *prev = NULL;
    minlz_decode_job *cur = *head;
    while (cur) {
        if (cur->seq == seq) {
            if (prev) {
                prev->next = cur->next;
            } else {
                *head = cur->next;
            }
            cur->next = NULL;
            return cur;
        }
        prev = cur;
        cur = cur->next;
    }
    return NULL;
}

static void minlz_decode_completed_push(minlz_decode_job **head, minlz_decode_job *job) {
    job->next = *head;
    *head = job;
}

static minlz_status minlz_reader_write_job(minlz_decode_job *job, minlz_write_cb write_cb,
                                           void *write_ctx, uint64_t *total,
                                           uint64_t *written) {
    minlz_status st;
    if (job->status != MINLZ_OK) {
        return job->status;
    }
    st = minlz_write_all(write_cb, write_ctx, job->output, job->output_len);
    if (st != MINLZ_OK) {
        return st;
    }
    *total += (uint64_t)job->output_len;
    *written += (uint64_t)job->output_len;
    return MINLZ_OK;
}

static minlz_status minlz_reader_handle_user_or_skip(minlz_reader *r, uint8_t chunk_type,
                                                     size_t chunk_len) {
    if (chunk_type >= MINLZ_MIN_USER_SKIPPABLE_CHUNK &&
        chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK) {
        size_t idx = (size_t)(chunk_type - MINLZ_MIN_USER_SKIPPABLE_CHUNK);
        minlz_user_chunk_cb cb = r->user_cb[idx];
        if (cb) {
            uint8_t *buf = (uint8_t *)malloc(chunk_len ? chunk_len : 1u);
            minlz_const_mem_read mr;
            minlz_status st;
            if (!buf) {
                return MINLZ_IO;
            }
            st = minlz_read_exact(r->read_cb, r->read_ctx, buf, chunk_len, 0, NULL);
            if (st != MINLZ_OK) {
                free(buf);
                return st;
            }
            mr.data = buf;
            mr.len = chunk_len;
            mr.pos = 0;
            st = cb(r->user_ctx[idx], chunk_type, const_mem_read_cb, &mr, chunk_len);
            free(buf);
            return st;
        }
        if (chunk_type >= 0xc0u) {
            return MINLZ_UNSUPPORTED;
        }
    }
    if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK) {
        return MINLZ_UNSUPPORTED;
    }
    return minlz_skip_exact(r->read_cb, r->read_ctx, chunk_len);
}

static int minlz_reader_default_threads(void) {
#if MINLZ_STREAM_PTHREADS
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    if (n <= 0) {
        n = 1;
    }
    if (n > 8) {
        n = 8;
    }
    return (int)n;
#else
    return 1;
#endif
}

static minlz_status reader_decode_stream_to_writer(minlz_reader *r, minlz_write_cb write_cb,
                                                   void *write_ctx, int threads,
                                                   uint64_t *written) {
    uint8_t hdr[4];
    int have_header = 0;
    size_t max_block = MINLZ_MAX_BLOCK_SIZE;
    uint64_t total = 0;
    uint64_t out_written = 0;
    uint64_t next_seq = 0;
    uint64_t write_seq = 0;
    size_t inflight = 0;
    minlz_decode_job *completed = NULL;
    minlz_status st = MINLZ_OK;
#if MINLZ_STREAM_PTHREADS
    minlz_decode_pool pool;
    size_t window = 0;
    int use_threads = 0;
#endif

    if (written) {
        *written = 0;
    }
    if (!r || !write_cb) {
        return MINLZ_CORRUPT;
    }
    if (threads <= 0) {
        threads = minlz_reader_default_threads();
    }
    if (threads > 64) {
        threads = 64;
    }
    if (r->seek_cb) {
        st = r->seek_cb(r->read_ctx, 0, MINLZ_SEEK_SET, NULL);
        if (st != MINLZ_OK) {
            return st;
        }
    }

#if MINLZ_STREAM_PTHREADS
    memset(&pool, 0, sizeof(pool));
    if (threads > 1) {
        st = minlz_decode_pool_start(&pool, threads);
        if (st == MINLZ_OK) {
            use_threads = 1;
            window = (size_t)threads * 2u;
        } else if (st != MINLZ_IO) {
            return st;
        } else {
            threads = 1;
            st = MINLZ_OK;
        }
    }
#endif

#define MINLZ_DRAIN_READY()                                                            \
    do {                                                                               \
        for (;;) {                                                                     \
            minlz_decode_job *ready = minlz_decode_completed_pop(&completed, write_seq); \
            if (!ready) {                                                              \
                break;                                                                 \
            }                                                                          \
            st = minlz_reader_write_job(ready, write_cb, write_ctx, &total, &out_written); \
            minlz_decode_job_free(ready);                                              \
            if (st != MINLZ_OK) {                                                      \
                goto decode_done;                                                      \
            }                                                                          \
            write_seq++;                                                               \
            inflight--;                                                                \
        }                                                                              \
    } while (0)

    for (;;) {
        size_t got = 0;
        uint8_t chunk_type;
        size_t chunk_len;
        st = minlz_read_exact(r->read_cb, r->read_ctx, hdr, sizeof(hdr), 1, &got);
        if (st != MINLZ_OK) {
            goto decode_done;
        }
        if (got == 0) {
            break;
        }
        chunk_type = hdr[0];
        chunk_len = (size_t)hdr[1] | ((size_t)hdr[2] << 8) | ((size_t)hdr[3] << 16);

        if (!have_header && chunk_type != MINLZ_CHUNK_STREAM_ID) {
            if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK &&
                chunk_type != MINLZ_CHUNK_EOF) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
        }

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            uint8_t id[6];
            while (inflight != 0) {
#if MINLZ_STREAM_PTHREADS
                minlz_decode_job *done_job;
                if (!use_threads) {
                    break;
                }
                done_job = minlz_decode_pool_wait_done(&pool);
                minlz_decode_completed_push(&completed, done_job);
                MINLZ_DRAIN_READY();
#else
                break;
#endif
            }
            if (chunk_len != 6) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
            st = minlz_read_exact(r->read_cb, r->read_ctx, id, sizeof(id), 0, NULL);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            if (memcmp(id, "MinLz", 5) == 0) {
                if ((id[5] >> 6) != 0 || (id[5] & 15u) > 13) {
                    st = MINLZ_CORRUPT;
                    goto decode_done;
                }
                max_block = (size_t)1u << ((id[5] & 15u) + 10u);
                if (max_block > r->opts.block_size) {
                    max_block = r->opts.block_size;
                }
                have_header = 1;
                total = 0;
                continue;
            }
            st = MINLZ_UNSUPPORTED;
            goto decode_done;
        }

        if (chunk_type == MINLZ_CHUNK_EOF) {
            uint8_t tmp[10];
            while (inflight != 0) {
#if MINLZ_STREAM_PTHREADS
                minlz_decode_job *done_job;
                if (!use_threads) {
                    break;
                }
                done_job = minlz_decode_pool_wait_done(&pool);
                minlz_decode_completed_push(&completed, done_job);
                MINLZ_DRAIN_READY();
#else
                break;
#endif
            }
            if (chunk_len > sizeof(tmp)) {
                st = MINLZ_CORRUPT;
                goto decode_done;
            }
            if (chunk_len != 0) {
                size_t want = 0;
                size_t used = 0;
                st = minlz_read_exact(r->read_cb, r->read_ctx, tmp, chunk_len, 0, NULL);
                if (st != MINLZ_OK) {
                    goto decode_done;
                }
                st = minlz_read_uvarint(tmp, chunk_len, &want, &used);
                if (st != MINLZ_OK || used != chunk_len || (uint64_t)want != total) {
                    st = MINLZ_CORRUPT;
                    goto decode_done;
                }
            }
            have_header = 0;
            continue;
        }

        if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED ||
            chunk_type == MINLZ_CHUNK_MINLZ ||
            chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            minlz_decode_job *job = (minlz_decode_job *)calloc(1, sizeof(*job));
            if (!job) {
                st = MINLZ_IO;
                goto decode_done;
            }
            job->input = (uint8_t *)malloc(chunk_len ? chunk_len : 1u);
            if (!job->input) {
                minlz_decode_job_free(job);
                st = MINLZ_IO;
                goto decode_done;
            }
            job->seq = next_seq++;
            job->chunk_type = chunk_type;
            job->input_len = chunk_len;
            job->ignore_crc = r->opts.ignore_crc;
            job->max_block = max_block;
            st = minlz_read_exact(r->read_cb, r->read_ctx, job->input, chunk_len, 0, NULL);
            if (st != MINLZ_OK) {
                minlz_decode_job_free(job);
                goto decode_done;
            }
#if MINLZ_STREAM_PTHREADS
            if (use_threads) {
                minlz_decode_pool_submit(&pool, job);
                inflight++;
                while (inflight >= window) {
                    minlz_decode_job *done_job = minlz_decode_pool_wait_done(&pool);
                    minlz_decode_completed_push(&completed, done_job);
                    MINLZ_DRAIN_READY();
                }
                continue;
            }
#endif
            job->status = minlz_decode_job_process(job);
            st = minlz_reader_write_job(job, write_cb, write_ctx, &total, &out_written);
            minlz_decode_job_free(job);
            if (st != MINLZ_OK) {
                goto decode_done;
            }
            write_seq++;
            continue;
        }

        while (inflight != 0) {
#if MINLZ_STREAM_PTHREADS
            minlz_decode_job *done_job;
            if (!use_threads) {
                break;
            }
            done_job = minlz_decode_pool_wait_done(&pool);
            minlz_decode_completed_push(&completed, done_job);
            MINLZ_DRAIN_READY();
#else
            break;
#endif
        }
        st = minlz_reader_handle_user_or_skip(r, chunk_type, chunk_len);
        if (st != MINLZ_OK) {
            goto decode_done;
        }
    }

    while (inflight != 0) {
#if MINLZ_STREAM_PTHREADS
        minlz_decode_job *done_job;
        if (!use_threads) {
            break;
        }
        done_job = minlz_decode_pool_wait_done(&pool);
        minlz_decode_completed_push(&completed, done_job);
        MINLZ_DRAIN_READY();
#else
        break;
#endif
    }

decode_done:
#undef MINLZ_DRAIN_READY
#if MINLZ_STREAM_PTHREADS
    if (use_threads) {
        minlz_decode_pool_destroy(&pool);
    }
#endif
    minlz_decode_job_list_free(completed);
    if (written) {
        *written = st == MINLZ_OK ? out_written : 0;
    }
    return st;
}

static minlz_status reader_decode_all(minlz_reader *r) {
    minlz_mem_write out = {0};
    minlz_status st;
    uint64_t written = 0;
    if (r->decoded_ready) {
        return r->err;
    }
    st = reader_decode_stream_to_writer(r, mem_write_cb, &out, 1, &written);
    (void)written;
    if (st != MINLZ_OK) {
        free(out.data);
        r->err = st;
        r->decoded_ready = 1;
        return st;
    }
    free(r->decoded);
    r->decoded = out.data;
    r->decoded_len = out.len;
    r->decoded_cap = out.cap;
    r->pos = 0;
    r->err = MINLZ_OK;
    r->decoded_ready = 1;
    return MINLZ_OK;
}

minlz_status minlz_reader_create(minlz_reader **reader, minlz_read_cb read_cb,
                                 void *read_ctx, const minlz_stream_options *opts_in) {
    minlz_reader *r;
    minlz_status st;
    if (!reader) {
        return MINLZ_CORRUPT;
    }
    *reader = NULL;
    r = (minlz_reader *)calloc(1, sizeof(*r));
    if (!r) {
        return MINLZ_IO;
    }
    st = minlz_normalize_options(&r->opts, opts_in, 0);
    if (st != MINLZ_OK) {
        free(r);
        return st;
    }
    memset(&r->own_ws, 0, sizeof(r->own_ws));
    r->ws = &r->own_ws;
    r->use_own_ws = 1;
    r->read_cb = read_cb;
    r->read_ctx = read_ctx;
    *reader = r;
    return MINLZ_OK;
}

minlz_status minlz_reader_create_seekable(minlz_reader **reader, minlz_read_cb read_cb,
                                          minlz_seek_cb seek_cb, void *ctx,
                                          const minlz_stream_options *opts_in) {
    minlz_status st = minlz_reader_create(reader, read_cb, ctx, opts_in);
    if (st == MINLZ_OK) {
        (*reader)->seek_cb = seek_cb;
    }
    return st;
}

void minlz_reader_destroy(minlz_reader *reader) {
    if (!reader) {
        return;
    }
    free(reader->decoded);
    if (reader->use_own_ws) {
        minlz_workspace_cleanup(&reader->own_ws);
    }
    free(reader);
}

minlz_status minlz_reader_reset(minlz_reader *reader, minlz_read_cb read_cb,
                                void *read_ctx) {
    if (!reader) {
        return MINLZ_CORRUPT;
    }
    reader->read_cb = read_cb;
    reader->read_ctx = read_ctx;
    reader->seek_cb = NULL;
    reader->pos = 0;
    reader->decoded_len = 0;
    reader->decoded_ready = 0;
    reader->err = MINLZ_OK;
    free(reader->decoded);
    reader->decoded = NULL;
    reader->decoded_cap = 0;
    return MINLZ_OK;
}

minlz_status minlz_reader_reset_seekable(minlz_reader *reader, minlz_read_cb read_cb,
                                         minlz_seek_cb seek_cb, void *ctx) {
    minlz_status st = minlz_reader_reset(reader, read_cb, ctx);
    if (st == MINLZ_OK) {
        reader->seek_cb = seek_cb;
    }
    return st;
}

minlz_status minlz_reader_read(minlz_reader *reader, uint8_t *dst, size_t cap,
                               size_t *nread) {
    size_t n;
    minlz_status st;
    if (nread) {
        *nread = 0;
    }
    if (!reader || (!dst && cap != 0)) {
        return MINLZ_CORRUPT;
    }
    st = reader_decode_all(reader);
    if (st != MINLZ_OK) {
        return st;
    }
    n = reader->decoded_len - reader->pos;
    if (n > cap) {
        n = cap;
    }
    if (n) {
        memcpy(dst, reader->decoded + reader->pos, n);
        reader->pos += n;
    }
    if (nread) {
        *nread = n;
    }
    return MINLZ_OK;
}

minlz_status minlz_reader_read_byte(minlz_reader *reader, uint8_t *value) {
    size_t n = 0;
    if (!value) {
        return MINLZ_CORRUPT;
    }
    return minlz_reader_read(reader, value, 1, &n);
}

minlz_status minlz_reader_skip(minlz_reader *reader, int64_t n) {
    minlz_status st;
    if (!reader || n < 0) {
        return MINLZ_CORRUPT;
    }
    st = reader_decode_all(reader);
    if (st != MINLZ_OK) {
        return st;
    }
    if ((uint64_t)n > (uint64_t)(reader->decoded_len - reader->pos)) {
        reader->pos = reader->decoded_len;
        return MINLZ_CORRUPT;
    }
    reader->pos += (size_t)n;
    return MINLZ_OK;
}

minlz_status minlz_reader_write_to(minlz_reader *reader, minlz_write_cb write_cb,
                                   void *write_ctx, uint64_t *written) {
    minlz_status st;
    size_t n;
    if (written) {
        *written = 0;
    }
    if (!reader || !write_cb) {
        return MINLZ_CORRUPT;
    }
    st = reader_decode_all(reader);
    if (st != MINLZ_OK) {
        return st;
    }
    n = reader->decoded_len - reader->pos;
    st = minlz_write_all(write_cb, write_ctx, reader->decoded + reader->pos, n);
    if (st == MINLZ_OK) {
        reader->pos += n;
        if (written) {
            *written = n;
        }
    }
    return st;
}

minlz_status minlz_reader_decode_concurrent(minlz_reader *reader, minlz_write_cb write_cb,
                                            void *write_ctx, int threads,
                                            uint64_t *written) {
    minlz_status st;
    if (written) {
        *written = 0;
    }
    if (!reader || !write_cb) {
        return MINLZ_CORRUPT;
    }
    if (reader->decoded_ready) {
        return minlz_reader_write_to(reader, write_cb, write_ctx, written);
    }
    st = reader_decode_stream_to_writer(reader, write_cb, write_ctx, threads, written);
    if (st == MINLZ_OK) {
        free(reader->decoded);
        reader->decoded = NULL;
        reader->decoded_len = 0;
        reader->decoded_cap = 0;
        reader->pos = 0;
        reader->err = MINLZ_OK;
        reader->decoded_ready = 1;
    } else {
        reader->err = st;
    }
    return st;
}

minlz_status minlz_reader_user_chunk_cb(minlz_reader *reader, uint8_t id,
                                        minlz_user_chunk_cb cb, void *cb_ctx) {
    size_t n;
    if (!reader || id < MINLZ_MIN_USER_SKIPPABLE_CHUNK ||
        id > MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK) {
        return MINLZ_CORRUPT;
    }
    n = (size_t)(id - MINLZ_MIN_USER_SKIPPABLE_CHUNK);
    reader->user_cb[n] = cb;
    reader->user_ctx[n] = cb_ctx;
    return MINLZ_OK;
}

static int readseeker_use_cached_block(minlz_readseeker *rs, int64_t offset) {
    size_t i;
    if (!rs) {
        return 0;
    }
    if (offset >= rs->block_start &&
        offset < rs->block_start + (int64_t)rs->block_len) {
        return 1;
    }
    for (i = 0; i < MINLZ_READSEEKER_CACHE_BLOCKS; i++) {
        minlz_readseeker_cache_block *b = &rs->cache[i];
        if (b->len != 0 &&
            offset >= b->start && offset < b->start + (int64_t)b->len) {
            b->tick = ++rs->cache_tick;
            rs->block_start = b->start;
            rs->block = b->data;
            rs->block_len = b->len;
            rs->block_cap = b->cap;
            return 1;
        }
    }
    return 0;
}

static minlz_readseeker_cache_block *readseeker_cache_slot(minlz_readseeker *rs) {
    minlz_readseeker_cache_block *slot = &rs->cache[0];
    size_t i;
    for (i = 0; i < MINLZ_READSEEKER_CACHE_BLOCKS; i++) {
        if (rs->cache[i].data == NULL) {
            return &rs->cache[i];
        }
        if (rs->cache[i].tick < slot->tick) {
            slot = &rs->cache[i];
        }
    }
    return slot;
}

static minlz_status readseeker_store_cached_block(minlz_readseeker *rs, int64_t start,
                                                  const uint8_t *decoded, size_t decoded_len,
                                                  unsigned decoded_slot) {
    minlz_readseeker_cache_block *slot;
    size_t i;
#if MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER
    minlz_workspace *ws = rs->reader->ws;
    size_t view_offset = decoded_slot == 2u ? MINLZ_CHECKSUM_SIZE : 0u;
    if (decoded_slot >= 3u || ws->buffer_caps[decoded_slot] < view_offset ||
        decoded_len > ws->buffer_caps[decoded_slot] - view_offset ||
        !ws->buffers[decoded_slot] || decoded != ws->buffers[decoded_slot] + view_offset) {
        return MINLZ_CORRUPT;
    }
    if (decoded_slot == 2u &&
        ws->buffer_caps[decoded_slot] > 2u * (decoded_len + MINLZ_CHECKSUM_SIZE)) {
        uint8_t *copy = NULL;
        minlz_status st = minlz_workspace_prepare_buffer(ws, 0u, decoded_len, &copy);
        if (st != MINLZ_OK) {
            return st;
        }
        /* Do not multiply oversized compressed-input scratch across cache entries. */
        memcpy(copy, decoded, decoded_len);
        decoded_slot = 0u;
        view_offset = 0u;
    }
#else
    (void)decoded_slot;
#endif
    for (i = 0; i < MINLZ_READSEEKER_CACHE_BLOCKS; i++) {
        if (rs->cache[i].data != NULL &&
            rs->cache[i].start == start && rs->cache[i].len == decoded_len) {
            slot = &rs->cache[i];
            goto commit_slot;
        }
    }
    slot = readseeker_cache_slot(rs);
#if !MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER
    if (slot->cap < decoded_len) {
        uint8_t *next = (uint8_t *)realloc(slot->data, decoded_len ? decoded_len : 1);
        if (!next) {
            return MINLZ_IO;
        }
        slot->data = next;
        slot->cap = decoded_len;
    }
#endif

commit_slot:
#if MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER
    /* Retain verified data; give the evicted allocation to the next decode. */
    if (minlz_workspace_exchange_buffer(ws, decoded_slot, &slot->storage, &slot->cap) !=
        MINLZ_OK) {
        return MINLZ_CORRUPT;
    }
    slot->data = slot->storage + view_offset;
#else
    if (decoded_len) {
        memcpy(slot->data, decoded, decoded_len);
    }
#endif
    slot->start = start;
    slot->len = decoded_len;
    slot->tick = ++rs->cache_tick;
    rs->block_start = slot->start;
    rs->block = slot->data;
    rs->block_len = slot->len;
    rs->block_cap = slot->cap;
    return MINLZ_OK;
}

static minlz_status readseeker_decode_indexed_block(minlz_readseeker *rs, int64_t offset) {
    minlz_index_pair pair;
    minlz_reader *r;
    uint8_t hdr[4];
    uint8_t *chunk = NULL;
    uint8_t *decoded = NULL;
    size_t decoded_len = 0;
    unsigned decoded_slot = 1u;
    minlz_status st;

    if (!rs || !rs->reader || !rs->index || !rs->reader->seek_cb) {
        return MINLZ_CORRUPT;
    }
    if (readseeker_use_cached_block(rs, offset)) {
        return MINLZ_OK;
    }
    if (rs->total_uncompressed < 0) {
        return MINLZ_CORRUPT;
    }
    if (offset < 0 || offset >= rs->total_uncompressed) {
        return offset == rs->total_uncompressed ? MINLZ_OK : MINLZ_CORRUPT;
    }
    st = minlz_index_find(rs->index, offset, &pair);
    if (st != MINLZ_OK) {
        return st;
    }
    r = rs->reader;
    st = r->seek_cb(r->read_ctx, pair.compressed_offset, MINLZ_SEEK_SET, NULL);
    if (st != MINLZ_OK) {
        return st;
    }

    for (;;) {
        uint8_t chunk_type;
        size_t chunk_len;
        st = minlz_read_exact(r->read_cb, r->read_ctx, hdr, sizeof(hdr), 0, NULL);
        if (st != MINLZ_OK) {
            return st;
        }
        chunk_type = hdr[0];
        chunk_len = (size_t)hdr[1] | ((size_t)hdr[2] << 8) | ((size_t)hdr[3] << 16);

        if (chunk_type == MINLZ_CHUNK_STREAM_ID) {
            st = minlz_skip_exact(r->read_cb, r->read_ctx, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            continue;
        }
        if (chunk_type == MINLZ_CHUNK_PADDING || chunk_type == MINLZ_CHUNK_INDEX) {
            st = minlz_skip_exact(r->read_cb, r->read_ctx, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            continue;
        }
        if (chunk_type == MINLZ_CHUNK_EOF) {
            return MINLZ_CORRUPT;
        }
        if (chunk_type == MINLZ_CHUNK_UNCOMPRESSED) {
            uint32_t want_crc;
            if (chunk_len < MINLZ_CHECKSUM_SIZE ||
                chunk_len - MINLZ_CHECKSUM_SIZE > r->opts.block_size) {
                return chunk_len < MINLZ_CHECKSUM_SIZE ? MINLZ_CORRUPT : MINLZ_TOO_LARGE;
            }
            st = minlz_workspace_prepare_buffer(r->ws, 2, chunk_len ? chunk_len : 1, &chunk);
            if (st != MINLZ_OK) {
                return st;
            }
            st = minlz_read_exact(r->read_cb, r->read_ctx, chunk, chunk_len, 0, NULL);
            if (st != MINLZ_OK) {
                return st;
            }
            want_crc = minlz_load32(chunk);
            decoded = chunk + MINLZ_CHECKSUM_SIZE;
            decoded_len = chunk_len - MINLZ_CHECKSUM_SIZE;
            decoded_slot = 2u;
            if (!r->opts.ignore_crc && minlz_crc(decoded, decoded_len) != want_crc) {
                return MINLZ_CRC;
            }
        } else if (chunk_type == MINLZ_CHUNK_MINLZ ||
                   chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            decoded_slot = 1u;
            st = minlz_workspace_prepare_buffer(r->ws, 2, chunk_len ? chunk_len : 1, &chunk);
            if (st != MINLZ_OK) {
                return st;
            }
            st = minlz_read_exact(r->read_cb, r->read_ctx, chunk, chunk_len, 0, NULL);
            if (st != MINLZ_OK) {
                return st;
            }
            st = minlz_decode_stream_chunk(
                r->ws, chunk, chunk_len, &decoded, &decoded_len,
                chunk_type == MINLZ_CHUNK_MINLZ_COMP_CRC,
                r->opts.ignore_crc, r->opts.block_size);
            if (st != MINLZ_OK) {
                return st;
            }
        } else if (chunk_type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
                   (chunk_type >= 0xc0u && chunk_type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            return MINLZ_UNSUPPORTED;
        } else {
            st = minlz_skip_exact(r->read_cb, r->read_ctx, chunk_len);
            if (st != MINLZ_OK) {
                return st;
            }
            continue;
        }

        if (pair.uncompressed_offset + (int64_t)decoded_len <= offset) {
            pair.uncompressed_offset += (int64_t)decoded_len;
            continue;
        }
        return readseeker_store_cached_block(rs, pair.uncompressed_offset, decoded,
                                             decoded_len, decoded_slot);
    }
}

minlz_status minlz_reader_readseeker(minlz_reader *reader, const uint8_t *index,
                                     size_t index_len, minlz_readseeker **rs) {
    minlz_readseeker *p;
    minlz_status st;
    if (!reader || !rs) {
        return MINLZ_CORRUPT;
    }
    p = (minlz_readseeker *)calloc(1, sizeof(*p));
    if (!p) {
        return MINLZ_IO;
    }
    p->reader = reader;
    p->total_uncompressed = -1;
    p->block_start = -1;
    if (index && index_len) {
        st = minlz_index_create(&p->index);
        if (st == MINLZ_OK) {
            st = minlz_index_load(p->index, index, index_len, NULL);
        }
        if (st != MINLZ_OK) {
            minlz_readseeker_destroy(p);
            return st;
        }
    }
    p->seekable = reader->seek_cb && p->index;
    if (p->seekable) {
        minlz_index_info info;
        st = minlz_index_info_get(p->index, &info);
        if (st != MINLZ_OK) {
            minlz_readseeker_destroy(p);
            return st;
        }
        p->total_uncompressed = info.total_uncompressed;
    }
    if (!p->seekable) {
        st = reader_decode_all(reader);
        if (st != MINLZ_OK) {
            minlz_readseeker_destroy(p);
            return st;
        }
        p->pos = (int64_t)reader->pos;
    }
    *rs = p;
    return MINLZ_OK;
}

void minlz_readseeker_destroy(minlz_readseeker *rs) {
    size_t i;
    if (!rs) {
        return;
    }
    minlz_index_destroy(rs->index);
    for (i = 0; i < MINLZ_READSEEKER_CACHE_BLOCKS; i++) {
#if MINLZ_ENABLE_READSEEKER_BUFFER_TRANSFER
        minlz_workspace_release_buffer(rs->cache[i].storage);
#else
        free(rs->cache[i].data);
#endif
    }
    free(rs);
}

minlz_status minlz_readseeker_seek(minlz_readseeker *rs, int64_t offset, int whence,
                                   int64_t *new_pos) {
    int64_t base;
    int64_t pos;
    if (!rs || !rs->reader) {
        return MINLZ_CORRUPT;
    }
    if (rs->seekable) {
        if (rs->total_uncompressed < 0) {
            return MINLZ_CORRUPT;
        }
        switch (whence) {
        case MINLZ_SEEK_SET:
            base = 0;
            break;
        case MINLZ_SEEK_CUR:
            base = rs->pos;
            break;
        case MINLZ_SEEK_END:
            base = rs->total_uncompressed;
            break;
        default:
            return MINLZ_UNSUPPORTED;
        }
        pos = base + offset;
        if (pos < 0 || pos > rs->total_uncompressed) {
            return MINLZ_CORRUPT;
        }
        rs->pos = pos;
        if (new_pos) {
            *new_pos = pos;
        }
        return MINLZ_OK;
    } else {
        if (reader_decode_all(rs->reader) != MINLZ_OK) {
            return rs->reader->err;
        }
    }
    switch (whence) {
    case MINLZ_SEEK_SET:
        base = 0;
        break;
    case MINLZ_SEEK_CUR:
        base = (int64_t)rs->reader->pos;
        break;
    case MINLZ_SEEK_END:
        base = (int64_t)rs->reader->decoded_len;
        break;
    default:
        return MINLZ_UNSUPPORTED;
    }
    pos = base + offset;
    if (pos < 0 || (uint64_t)pos > (uint64_t)rs->reader->decoded_len) {
        return MINLZ_CORRUPT;
    }
    rs->reader->pos = (size_t)pos;
    if (new_pos) {
        *new_pos = pos;
    }
    return MINLZ_OK;
}

minlz_status minlz_readseeker_read(minlz_readseeker *rs, uint8_t *dst, size_t cap,
                                   size_t *nread) {
    size_t done = 0;
    if (!rs) {
        return MINLZ_CORRUPT;
    }
    if (nread) {
        *nread = 0;
    }
    if (rs->seekable) {
        minlz_status st;
        if (rs->total_uncompressed < 0) {
            return MINLZ_CORRUPT;
        }
        while (done < cap && rs->pos < rs->total_uncompressed) {
            size_t off;
            size_t n;
            st = readseeker_decode_indexed_block(rs, rs->pos);
            if (st != MINLZ_OK) {
                return st;
            }
            if (rs->pos < rs->block_start ||
                rs->pos >= rs->block_start + (int64_t)rs->block_len) {
                return MINLZ_CORRUPT;
            }
            off = (size_t)(rs->pos - rs->block_start);
            n = rs->block_len - off;
            if (n > cap - done) {
                n = cap - done;
            }
            if ((int64_t)n > rs->total_uncompressed - rs->pos) {
                n = (size_t)(rs->total_uncompressed - rs->pos);
            }
            if (n == 0) {
                break;
            }
            memcpy(dst + done, rs->block + off, n);
            done += n;
            rs->pos += (int64_t)n;
        }
        if (nread) {
            *nread = done;
        }
        return MINLZ_OK;
    }
    return minlz_reader_read(rs->reader, dst, cap, nread);
}

minlz_status minlz_readseeker_read_at(minlz_readseeker *rs, uint8_t *dst, size_t len,
                                      int64_t offset, size_t *nread) {
    minlz_status st;
    size_t done = 0;
    if (!rs) {
        return MINLZ_CORRUPT;
    }
    if (nread) {
        *nread = 0;
    }
    if (rs->seekable) {
        if (rs->total_uncompressed < 0) {
            return MINLZ_CORRUPT;
        }
        if (offset < 0 || offset > rs->total_uncompressed) {
            return MINLZ_CORRUPT;
        }
        if (MINLZ_LIKELY(len != 0u && rs->block &&
                         offset >= rs->block_start &&
                         offset < rs->block_start + (int64_t)rs->block_len)) {
            size_t off = (size_t)(offset - rs->block_start);
            size_t n = rs->block_len - off;
            if ((int64_t)n > rs->total_uncompressed - offset) {
                n = (size_t)(rs->total_uncompressed - offset);
            }
            if (n >= len) {
                memcpy(dst, rs->block + off, len);
                if (nread) {
                    *nread = len;
                }
                return MINLZ_OK;
            }
        }
        while (done < len && offset < rs->total_uncompressed) {
            size_t off;
            size_t n;
            st = readseeker_decode_indexed_block(rs, offset);
            if (st != MINLZ_OK) {
                return st;
            }
            if (offset < rs->block_start ||
                offset >= rs->block_start + (int64_t)rs->block_len) {
                return MINLZ_CORRUPT;
            }
            off = (size_t)(offset - rs->block_start);
            n = rs->block_len - off;
            if (n > len - done) {
                n = len - done;
            }
            if ((int64_t)n > rs->total_uncompressed - offset) {
                n = (size_t)(rs->total_uncompressed - offset);
            }
            if (n == 0) {
                break;
            }
            memcpy(dst + done, rs->block + off, n);
            done += n;
            offset += (int64_t)n;
        }
        if (nread) {
            *nread = done;
        }
        return MINLZ_OK;
    }
    st = minlz_readseeker_seek(rs, offset, MINLZ_SEEK_SET, NULL);
    if (st != MINLZ_OK) {
        return st;
    }
    return minlz_reader_read(rs->reader, dst, len, nread);
}

minlz_status minlz_readseeker_index(minlz_readseeker *rs, const minlz_index **idx) {
    if (!rs || !idx) {
        return MINLZ_CORRUPT;
    }
    *idx = rs->index;
    return MINLZ_OK;
}

struct minlz_lz4_converter {
    uint8_t unused;
};

minlz_status minlz_lz4_convert_block(uint8_t *dst, size_t dst_cap, size_t *dst_len,
                                     size_t *decoded_len, const uint8_t *src,
                                     size_t src_len) {
    (void)dst;
    (void)dst_cap;
    (void)dst_len;
    (void)decoded_len;
    (void)src;
    (void)src_len;
    return MINLZ_UNSUPPORTED;
}

minlz_status minlz_lz4_convert_stream(minlz_read_cb read_cb, void *read_ctx,
                                      minlz_write_cb write_cb, void *write_ctx) {
    (void)read_cb;
    (void)read_ctx;
    (void)write_cb;
    (void)write_ctx;
    return MINLZ_UNSUPPORTED;
}
