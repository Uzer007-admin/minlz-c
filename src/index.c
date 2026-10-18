/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <stdlib.h>

#define MINLZ_INDEX_HEADER "s2idx\0"
#define MINLZ_INDEX_HEADER_LEN 6u
#define MINLZ_INDEX_TRAILER "\0xdi2s"
#define MINLZ_INDEX_TRAILER_LEN 6u
#define MINLZ_MIN_INDEX_DIST (1u << 20)
#define MINLZ_MAX_INDEX_ENTRIES (1u << 16)
#define MINLZ_LEGACY_INDEX_CHUNK 0x99u

static minlz_status append_bytes(uint8_t **buf, size_t *len, size_t *cap,
                                 const uint8_t *src, size_t n) {
    if (*len + n < *len) {
        return MINLZ_TOO_LARGE;
    }
    if (*len + n > *cap) {
        size_t next = *cap ? *cap * 2 : 256;
        uint8_t *p;
        while (next < *len + n) {
            next *= 2;
        }
        p = (uint8_t *)realloc(*buf, next);
        if (!p) {
            return MINLZ_IO;
        }
        *buf = p;
        *cap = next;
    }
    memcpy(*buf + *len, src, n);
    *len += n;
    return MINLZ_OK;
}

static minlz_status append_byte(uint8_t **buf, size_t *len, size_t *cap, uint8_t v) {
    return append_bytes(buf, len, cap, &v, 1);
}

static minlz_status append_varint(uint8_t **buf, size_t *len, size_t *cap, int64_t v) {
    uint8_t tmp[10];
    size_t n = minlz_put_varint(tmp, v);
    return append_bytes(buf, len, cap, tmp, n);
}

static minlz_status read_varint64(const uint8_t *src, size_t src_len,
                                  int64_t *value, size_t *used) {
    uint64_t ux = 0;
    unsigned shift = 0;
    size_t i;

    for (i = 0; i < src_len && i < 10; i++) {
        uint8_t b = src[i];
        if (b < 0x80) {
            if (i == 9 && b > 1) {
                return MINLZ_CORRUPT;
            }
            ux |= (uint64_t)b << shift;
            *value = (int64_t)((ux >> 1) ^ (uint64_t)-(int64_t)(ux & 1u));
            *used = i + 1;
            return MINLZ_OK;
        }
        ux |= (uint64_t)(b & 0x7fu) << shift;
        shift += 7;
    }
    return MINLZ_CORRUPT;
}

static minlz_status index_reserve(minlz_index *idx, size_t n) {
    minlz_index_entry *p;
    if (n > MINLZ_MAX_INDEX_ENTRIES) {
        return MINLZ_CORRUPT;
    }
    if (idx->cap >= n) {
        return MINLZ_OK;
    }
    p = (minlz_index_entry *)realloc(idx->entries, n * sizeof(*idx->entries));
    if (!p && n != 0) {
        return MINLZ_IO;
    }
    idx->entries = p;
    idx->cap = n;
    return MINLZ_OK;
}

static int64_t normalized_block_size(size_t block_size) {
    int64_t v = (int64_t)block_size;
    if (v <= 0) {
        v = MINLZ_DEFAULT_BLOCK_SIZE;
    }
    while (v < (int64_t)MINLZ_MIN_INDEX_DIST) {
        v *= 2;
    }
    return v;
}

void minlz_index_init(minlz_index_builder *idx, size_t block_size) {
    if (!idx) {
        return;
    }
    idx->entries = NULL;
    idx->len = 0;
    idx->cap = 0;
    idx->est_block_uncomp = (int64_t)block_size;
    while (idx->est_block_uncomp < (int64_t)MINLZ_MIN_INDEX_DIST) {
        idx->est_block_uncomp *= 2;
    }
}

void minlz_index_free(minlz_index_builder *idx) {
    if (!idx) {
        return;
    }
    free(idx->entries);
    idx->entries = NULL;
    idx->len = 0;
    idx->cap = 0;
}

minlz_status minlz_index_add(minlz_index_builder *idx, int64_t compressed, int64_t uncompressed) {
    minlz_index_entry *p;
    if (!idx) {
        return MINLZ_OK;
    }
    if (idx->len > 0) {
        minlz_index_entry last = idx->entries[idx->len - 1];
        if (uncompressed - last.uncompressed < idx->est_block_uncomp) {
            return MINLZ_OK;
        }
        if (last.uncompressed > uncompressed || last.compressed > compressed) {
            return MINLZ_CORRUPT;
        }
    }
    if (idx->len >= MINLZ_MAX_INDEX_ENTRIES) {
        return MINLZ_OK;
    }
    if (idx->len == idx->cap) {
        size_t next = idx->cap ? idx->cap * 2 : 16;
        p = (minlz_index_entry *)realloc(idx->entries, next * sizeof(*idx->entries));
        if (!p) {
            return MINLZ_IO;
        }
        idx->entries = p;
        idx->cap = next;
    }
    idx->entries[idx->len].compressed = compressed;
    idx->entries[idx->len].uncompressed = uncompressed;
    idx->len++;
    return MINLZ_OK;
}

minlz_status minlz_index_append(uint8_t **buf, size_t *len, size_t *cap,
                                const minlz_index_builder *idx,
                                int64_t uncomp_total, int64_t comp_total) {
    size_t init_len = *len;
    size_t i;
    int has_uncompressed = 0;
    int64_t c_predict;
    uint8_t header[4] = { MINLZ_CHUNK_INDEX, 0, 0, 0 };
    uint8_t tmp4[4];
    uint32_t chunk_len;
    uint32_t total_size;
    minlz_status st;

    if (!idx) {
        return MINLZ_OK;
    }
    st = append_bytes(buf, len, cap, header, sizeof(header));
    if (st != MINLZ_OK) {
        return st;
    }
    st = append_bytes(buf, len, cap, (const uint8_t *)MINLZ_INDEX_HEADER, MINLZ_INDEX_HEADER_LEN);
    if (st != MINLZ_OK) {
        return st;
    }
    if ((st = append_varint(buf, len, cap, uncomp_total)) != MINLZ_OK ||
        (st = append_varint(buf, len, cap, comp_total)) != MINLZ_OK ||
        (st = append_varint(buf, len, cap, idx->est_block_uncomp)) != MINLZ_OK ||
        (st = append_varint(buf, len, cap, (int64_t)idx->len)) != MINLZ_OK) {
        return st;
    }

    for (i = 0; i < idx->len; i++) {
        if (i == 0) {
            if (idx->entries[i].uncompressed != 0) {
                has_uncompressed = 1;
                break;
            }
        } else if (idx->entries[i].uncompressed !=
                   idx->entries[i - 1].uncompressed + idx->est_block_uncomp) {
            has_uncompressed = 1;
            break;
        }
    }
    if ((st = append_byte(buf, len, cap, (uint8_t)has_uncompressed)) != MINLZ_OK) {
        return st;
    }
    if (has_uncompressed) {
        for (i = 0; i < idx->len; i++) {
            int64_t u = idx->entries[i].uncompressed;
            if (i > 0) {
                u -= idx->entries[i - 1].uncompressed + idx->est_block_uncomp;
            }
            if ((st = append_varint(buf, len, cap, u)) != MINLZ_OK) {
                return st;
            }
        }
    }

    c_predict = idx->est_block_uncomp / 2;
    for (i = 0; i < idx->len; i++) {
        int64_t c = idx->entries[i].compressed;
        if (i > 0) {
            c -= idx->entries[i - 1].compressed + c_predict;
            c_predict += c / 2;
        }
        if ((st = append_varint(buf, len, cap, c)) != MINLZ_OK) {
            return st;
        }
    }

    total_size = (uint32_t)(*len - init_len + 4 + MINLZ_INDEX_TRAILER_LEN);
    tmp4[0] = (uint8_t)total_size;
    tmp4[1] = (uint8_t)(total_size >> 8);
    tmp4[2] = (uint8_t)(total_size >> 16);
    tmp4[3] = (uint8_t)(total_size >> 24);
    if ((st = append_bytes(buf, len, cap, tmp4, 4)) != MINLZ_OK ||
        (st = append_bytes(buf, len, cap, (const uint8_t *)MINLZ_INDEX_TRAILER,
                           MINLZ_INDEX_TRAILER_LEN)) != MINLZ_OK) {
        return st;
    }

    chunk_len = (uint32_t)(*len - init_len - MINLZ_CHUNK_HEADER_SIZE);
    (*buf)[init_len + 1] = (uint8_t)chunk_len;
    (*buf)[init_len + 2] = (uint8_t)(chunk_len >> 8);
    (*buf)[init_len + 3] = (uint8_t)(chunk_len >> 16);
    return MINLZ_OK;
}

minlz_status minlz_index_create(minlz_index **idx) {
    minlz_index *p;
    if (!idx) {
        return MINLZ_CORRUPT;
    }
    *idx = NULL;
    p = (minlz_index *)calloc(1, sizeof(*p));
    if (!p) {
        return MINLZ_IO;
    }
    p->total_uncompressed = -1;
    p->total_compressed = -1;
    p->est_block_uncomp = normalized_block_size(MINLZ_DEFAULT_BLOCK_SIZE);
    *idx = p;
    return MINLZ_OK;
}

void minlz_index_destroy(minlz_index *idx) {
    if (!idx) {
        return;
    }
    free(idx->entries);
    free(idx);
}

void minlz_index_reset(minlz_index *idx, size_t block_size) {
    if (!idx) {
        return;
    }
    idx->len = 0;
    idx->total_uncompressed = -1;
    idx->total_compressed = -1;
    idx->est_block_uncomp = normalized_block_size(block_size);
}

minlz_status minlz_index_add_pair(minlz_index *idx, int64_t compressed_offset,
                                  int64_t uncompressed_offset) {
    if (!idx) {
        return MINLZ_CORRUPT;
    }
    if (compressed_offset < 0 || uncompressed_offset < 0) {
        return MINLZ_CORRUPT;
    }
    if (idx->len > 0) {
        minlz_index_entry last = idx->entries[idx->len - 1];
        if (last.compressed > compressed_offset || last.uncompressed > uncompressed_offset) {
            return MINLZ_CORRUPT;
        }
        if (uncompressed_offset - last.uncompressed < idx->est_block_uncomp) {
            return MINLZ_OK;
        }
    }
    if (idx->len >= MINLZ_MAX_INDEX_ENTRIES) {
        return MINLZ_OK;
    }
    if (idx->len == idx->cap) {
        size_t next = idx->cap ? idx->cap * 2 : 16;
        minlz_status st;
        if (next > MINLZ_MAX_INDEX_ENTRIES) {
            next = MINLZ_MAX_INDEX_ENTRIES;
        }
        st = index_reserve(idx, next);
        if (st != MINLZ_OK) {
            return st;
        }
    }
    idx->entries[idx->len].compressed = compressed_offset;
    idx->entries[idx->len].uncompressed = uncompressed_offset;
    idx->len++;
    return MINLZ_OK;
}

minlz_status minlz_index_serialize(const minlz_index *idx, uint8_t **dst, size_t *dst_len,
                                   int64_t total_uncompressed, int64_t total_compressed) {
    minlz_index_builder b;
    size_t cap = 0;
    minlz_status st;
    if (!idx || !dst || !dst_len) {
        return MINLZ_CORRUPT;
    }
    *dst = NULL;
    *dst_len = 0;
    b.entries = idx->entries;
    b.len = idx->len;
    b.cap = idx->cap;
    b.est_block_uncomp = idx->est_block_uncomp;
    st = minlz_index_append(dst, dst_len, &cap, &b, total_uncompressed, total_compressed);
    if (st != MINLZ_OK) {
        free(*dst);
        *dst = NULL;
        *dst_len = 0;
    }
    return st;
}

minlz_status minlz_index_load(minlz_index *idx, const uint8_t *src, size_t src_len,
                              size_t *used) {
    const uint8_t *p;
    const uint8_t *end;
    size_t chunk_len;
    size_t need;
    size_t n;
    int64_t v;
    int64_t c_predict;
    int has_uncompressed;
    size_t entries;
    size_t i;
    uint32_t stored_size;
    minlz_status st;

    if (used) {
        *used = 0;
    }
    if (!idx || !src || src_len < MINLZ_CHUNK_HEADER_SIZE + MINLZ_INDEX_HEADER_LEN +
                                MINLZ_INDEX_TRAILER_LEN + 4u) {
        return MINLZ_CORRUPT;
    }
    if (src[0] != MINLZ_CHUNK_INDEX && src[0] != MINLZ_LEGACY_INDEX_CHUNK) {
        return MINLZ_CORRUPT;
    }
    chunk_len = (size_t)src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
    need = MINLZ_CHUNK_HEADER_SIZE + chunk_len;
    if (need < MINLZ_CHUNK_HEADER_SIZE || need > src_len ||
        chunk_len < MINLZ_INDEX_HEADER_LEN + MINLZ_INDEX_TRAILER_LEN + 4u) {
        return MINLZ_CORRUPT;
    }
    p = src + MINLZ_CHUNK_HEADER_SIZE;
    end = src + need;
    if (memcmp(p, MINLZ_INDEX_HEADER, MINLZ_INDEX_HEADER_LEN) != 0) {
        return MINLZ_UNSUPPORTED;
    }
    p += MINLZ_INDEX_HEADER_LEN;

    st = read_varint64(p, (size_t)(end - p), &idx->total_uncompressed, &n);
    if (st != MINLZ_OK || idx->total_uncompressed < 0) {
        return MINLZ_CORRUPT;
    }
    p += n;
    st = read_varint64(p, (size_t)(end - p), &idx->total_compressed, &n);
    if (st != MINLZ_OK) {
        return MINLZ_CORRUPT;
    }
    p += n;
    st = read_varint64(p, (size_t)(end - p), &idx->est_block_uncomp, &n);
    if (st != MINLZ_OK || idx->est_block_uncomp < 0) {
        return MINLZ_CORRUPT;
    }
    p += n;
    st = read_varint64(p, (size_t)(end - p), &v, &n);
    if (st != MINLZ_OK || v < 0 || v > (int64_t)MINLZ_MAX_INDEX_ENTRIES) {
        return MINLZ_CORRUPT;
    }
    p += n;
    entries = (size_t)v;
    if (p >= end) {
        return MINLZ_CORRUPT;
    }
    has_uncompressed = *p++;
    if (has_uncompressed != 0 && has_uncompressed != 1) {
        return MINLZ_CORRUPT;
    }
    st = index_reserve(idx, entries);
    if (st != MINLZ_OK) {
        return st;
    }
    idx->len = entries;

    for (i = 0; i < entries; i++) {
        int64_t u = 0;
        if (has_uncompressed) {
            st = read_varint64(p, (size_t)(end - p), &u, &n);
            if (st != MINLZ_OK) {
                return MINLZ_CORRUPT;
            }
            p += n;
        }
        if (i > 0) {
            int64_t prev = idx->entries[i - 1].uncompressed;
            u += prev + idx->est_block_uncomp;
            if (u <= prev) {
                return MINLZ_CORRUPT;
            }
        }
        if (u < 0) {
            return MINLZ_CORRUPT;
        }
        idx->entries[i].uncompressed = u;
    }

    c_predict = idx->est_block_uncomp / 2;
    for (i = 0; i < entries; i++) {
        int64_t c;
        st = read_varint64(p, (size_t)(end - p), &c, &n);
        if (st != MINLZ_OK) {
            return MINLZ_CORRUPT;
        }
        p += n;
        if (i > 0) {
            int64_t prev = idx->entries[i - 1].compressed;
            c += prev + c_predict;
            if (c <= prev) {
                return MINLZ_CORRUPT;
            }
            c_predict += (c - prev - c_predict) / 2;
        }
        if (c < 0) {
            return MINLZ_CORRUPT;
        }
        idx->entries[i].compressed = c;
    }

    if ((size_t)(end - p) < 4u + MINLZ_INDEX_TRAILER_LEN) {
        return MINLZ_CORRUPT;
    }
    stored_size = minlz_load32(p);
    p += 4;
    if (stored_size != need) {
        return MINLZ_CORRUPT;
    }
    if (memcmp(p, MINLZ_INDEX_TRAILER, MINLZ_INDEX_TRAILER_LEN) != 0) {
        return MINLZ_CORRUPT;
    }
    p += MINLZ_INDEX_TRAILER_LEN;
    if (p != end) {
        return MINLZ_CORRUPT;
    }
    if (used) {
        *used = need;
    }
    return MINLZ_OK;
}

minlz_status minlz_index_load_stream_file(minlz_index *idx, FILE *in) {
    uint8_t tail[4 + MINLZ_INDEX_TRAILER_LEN];
    uint32_t sz;
    uint8_t *buf;
    minlz_status st;
    if (!idx || !in) {
        return MINLZ_IO;
    }
    if (fseek(in, -(long)sizeof(tail), SEEK_END) != 0) {
        return MINLZ_UNSUPPORTED;
    }
    if (fread(tail, 1, sizeof(tail), in) != sizeof(tail)) {
        return MINLZ_IO;
    }
    if (memcmp(tail + 4, MINLZ_INDEX_TRAILER, MINLZ_INDEX_TRAILER_LEN) != 0) {
        return MINLZ_UNSUPPORTED;
    }
    sz = minlz_load32(tail);
    if (sz < MINLZ_CHUNK_HEADER_SIZE + MINLZ_INDEX_HEADER_LEN + 4u + MINLZ_INDEX_TRAILER_LEN ||
        sz > MINLZ_MAX_USER_CHUNK_SIZE + MINLZ_CHUNK_HEADER_SIZE) {
        return MINLZ_CORRUPT;
    }
    if (fseek(in, -(long)sz, SEEK_END) != 0) {
        return MINLZ_IO;
    }
    buf = (uint8_t *)malloc(sz);
    if (!buf) {
        return MINLZ_IO;
    }
    if (fread(buf, 1, sz, in) != sz) {
        free(buf);
        return MINLZ_IO;
    }
    st = minlz_index_load(idx, buf, sz, NULL);
    free(buf);
    return st;
}

minlz_status minlz_index_stream(minlz_read_cb read_cb, void *read_ctx,
                                uint8_t **dst, size_t *dst_len) {
    minlz_index *idx = NULL;
    uint8_t hdr[4];
    int have_header = 0;
    int64_t comp = 0;
    int64_t uncomp = 0;
    size_t max_block = MINLZ_MAX_BLOCK_SIZE;
    minlz_status st;

    if (!dst || !dst_len) {
        return MINLZ_CORRUPT;
    }
    *dst = NULL;
    *dst_len = 0;
    st = minlz_index_create(&idx);
    if (st != MINLZ_OK) {
        return st;
    }
    idx->len = 0;
    idx->total_uncompressed = -1;
    idx->total_compressed = -1;
    idx->est_block_uncomp = 0;

    for (;;) {
        size_t got = 0;
        uint8_t type;
        size_t len;
        st = minlz_read_exact(read_cb, read_ctx, hdr, sizeof(hdr), 1, &got);
        if (st != MINLZ_OK) {
            goto done;
        }
        if (got == 0) {
            idx->total_uncompressed = uncomp;
            idx->total_compressed = comp;
            st = minlz_index_serialize(idx, dst, dst_len, uncomp, comp);
            goto done;
        }
        type = hdr[0];
        len = (size_t)hdr[1] | ((size_t)hdr[2] << 8) | ((size_t)hdr[3] << 16);
        if (len > MINLZ_MAX_USER_CHUNK_SIZE) {
            st = MINLZ_TOO_LARGE;
            goto done;
        }
        if (type == MINLZ_CHUNK_STREAM_ID) {
            uint8_t id[6];
            if (len != 6) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            st = minlz_read_exact(read_cb, read_ctx, id, sizeof(id), 0, NULL);
            if (st != MINLZ_OK) {
                goto done;
            }
            comp += 10;
            if (memcmp(id, "MinLz", 5) == 0) {
                unsigned lg = id[5] & 15u;
                if ((id[5] >> 6) != 0 || lg > 13) {
                    st = MINLZ_CORRUPT;
                    goto done;
                }
                max_block = (size_t)1u << (lg + 10u);
                have_header = 1;
                continue;
            }
            st = MINLZ_UNSUPPORTED;
            goto done;
        }
        if (!have_header && type != MINLZ_CHUNK_EOF) {
            st = type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ? MINLZ_CORRUPT : MINLZ_OK;
            if (st != MINLZ_OK) {
                goto done;
            }
        }
        if (type == MINLZ_CHUNK_EOF) {
            uint8_t tmp[10];
            size_t want = 0, used = 0;
            if (len != 0) {
                size_t n = len < sizeof(tmp) ? len : sizeof(tmp);
                st = minlz_read_exact(read_cb, read_ctx, tmp, n, 0, NULL);
                if (st == MINLZ_OK && len > n) {
                    st = minlz_skip_exact(read_cb, read_ctx, len - n);
                }
                if (st != MINLZ_OK) {
                    goto done;
                }
                st = minlz_read_uvarint(tmp, n, &want, &used);
                if (st != MINLZ_OK || used != len || (uint64_t)want != (uint64_t)uncomp) {
                    st = MINLZ_CORRUPT;
                    goto done;
                }
            }
            have_header = 0;
        } else if (type == MINLZ_CHUNK_UNCOMPRESSED) {
            size_t dlen;
            if (len < MINLZ_CHECKSUM_SIZE || len - MINLZ_CHECKSUM_SIZE > max_block) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            dlen = len - MINLZ_CHECKSUM_SIZE;
            if (idx->est_block_uncomp == 0) {
                idx->est_block_uncomp = (int64_t)dlen;
            }
            st = minlz_index_add_pair(idx, comp, uncomp);
            if (st != MINLZ_OK) {
                goto done;
            }
            uncomp += (int64_t)dlen;
            st = minlz_skip_exact(read_cb, read_ctx, len);
            if (st != MINLZ_OK) {
                goto done;
            }
        } else if (type == MINLZ_CHUNK_MINLZ || type == MINLZ_CHUNK_MINLZ_COMP_CRC) {
            uint8_t prefix[MINLZ_CHECKSUM_SIZE + 10u];
            size_t prefix_len;
            size_t dlen = 0, hlen = 0;
            if (len < MINLZ_CHECKSUM_SIZE) {
                st = MINLZ_CORRUPT;
                goto done;
            }
            prefix_len = len < sizeof(prefix) ? len : sizeof(prefix);
            st = minlz_read_exact(read_cb, read_ctx, prefix, prefix_len, 0, NULL);
            if (st == MINLZ_OK && len > prefix_len) {
                st = minlz_skip_exact(read_cb, read_ctx, len - prefix_len);
            }
            if (st != MINLZ_OK) {
                goto done;
            }
            st = minlz_payload_decoded_len(prefix + MINLZ_CHECKSUM_SIZE,
                                           prefix_len - MINLZ_CHECKSUM_SIZE, &dlen, &hlen);
            (void)hlen;
            if (st != MINLZ_OK || dlen > max_block) {
                st = st == MINLZ_OK ? MINLZ_TOO_LARGE : st;
                goto done;
            }
            if (idx->est_block_uncomp == 0) {
                idx->est_block_uncomp = (int64_t)dlen;
            }
            st = minlz_index_add_pair(idx, comp, uncomp);
            if (st != MINLZ_OK) {
                goto done;
            }
            uncomp += (int64_t)dlen;
        } else if (type <= MINLZ_MAX_NON_SKIPPABLE_CHUNK ||
                   (type >= 0xc0u && type <= MINLZ_MAX_USER_NON_SKIPPABLE_CHUNK)) {
            st = MINLZ_UNSUPPORTED;
            goto done;
        } else {
            st = minlz_skip_exact(read_cb, read_ctx, len);
            if (st != MINLZ_OK) {
                goto done;
            }
        }
        comp += (int64_t)(MINLZ_CHUNK_HEADER_SIZE + len);
    }

done:
    minlz_index_destroy(idx);
    return st;
}

minlz_status minlz_index_find(const minlz_index *idx, int64_t offset,
                              minlz_index_pair *pair) {
    size_t lo;
    size_t hi;
    size_t best = (size_t)-1;
    if (!idx || !pair || idx->total_uncompressed < 0) {
        return MINLZ_CORRUPT;
    }
    if (MINLZ_LIKELY(idx->len == 1u && idx->entries[0].uncompressed == 0 &&
                     offset >= 0 && offset <= idx->total_uncompressed)) {
        pair->compressed_offset = idx->entries[0].compressed;
        pair->uncompressed_offset = 0;
        return MINLZ_OK;
    }
    if (MINLZ_LIKELY(offset >= 0 && offset <= idx->total_uncompressed && idx->len <= 2u)) {
        if (idx->len == 0u) {
            pair->compressed_offset = 0;
            pair->uncompressed_offset = 0;
        } else if (idx->len == 1u ||
                   idx->entries[1].uncompressed > offset) {
            if (idx->entries[0].uncompressed <= offset) {
                pair->compressed_offset = idx->entries[0].compressed;
                pair->uncompressed_offset = idx->entries[0].uncompressed;
            } else {
                pair->compressed_offset = 0;
                pair->uncompressed_offset = 0;
            }
        } else {
            pair->compressed_offset = idx->entries[1].compressed;
            pair->uncompressed_offset = idx->entries[1].uncompressed;
        }
        return MINLZ_OK;
    }
    if (offset < 0) {
        offset = idx->total_uncompressed + offset;
        if (offset < 0) {
            return MINLZ_CORRUPT;
        }
    }
    if (offset > idx->total_uncompressed) {
        return MINLZ_CORRUPT;
    }
    if (idx->len == 0) {
        pair->compressed_offset = 0;
        pair->uncompressed_offset = 0;
        return MINLZ_OK;
    }
    if (idx->len == 1) {
        if (idx->entries[0].uncompressed <= offset) {
            pair->compressed_offset = idx->entries[0].compressed;
            pair->uncompressed_offset = idx->entries[0].uncompressed;
        } else {
            pair->compressed_offset = 0;
            pair->uncompressed_offset = 0;
        }
        return MINLZ_OK;
    }
    if (idx->est_block_uncomp > 0 &&
        idx->entries[0].uncompressed == 0) {
        uint64_t step = (uint64_t)idx->est_block_uncomp;
        size_t guess = (size_t)((uint64_t)offset / step);
        unsigned adjust = 0;
        if (guess >= idx->len) {
            guess = idx->len - 1;
        }
        while (guess + 1 < idx->len &&
               idx->entries[guess + 1].uncompressed <= offset &&
               adjust++ < 4u) {
            guess++;
        }
        while (idx->entries[guess].uncompressed > offset &&
               guess != 0 && adjust++ < 8u) {
            guess--;
        }
        if (idx->entries[guess].uncompressed <= offset &&
            (guess + 1 == idx->len || idx->entries[guess + 1].uncompressed > offset)) {
            pair->compressed_offset = idx->entries[guess].compressed;
            pair->uncompressed_offset = idx->entries[guess].uncompressed;
            return MINLZ_OK;
        }
    }
    lo = 0;
    hi = idx->len;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (idx->entries[mid].uncompressed <= offset) {
            best = mid;
            lo = mid + 1;
        } else {
            hi = mid;
        }
    }
    if (best == (size_t)-1) {
        pair->compressed_offset = 0;
        pair->uncompressed_offset = 0;
    } else {
        pair->compressed_offset = idx->entries[best].compressed;
        pair->uncompressed_offset = idx->entries[best].uncompressed;
    }
    return MINLZ_OK;
}

minlz_status minlz_index_info_get(const minlz_index *idx, minlz_index_info *info) {
    if (!idx || !info) {
        return MINLZ_CORRUPT;
    }
    info->total_uncompressed = idx->total_uncompressed;
    info->total_compressed = idx->total_compressed;
    info->est_block_uncompressed = idx->est_block_uncomp;
    info->entries = idx->len;
    return MINLZ_OK;
}

minlz_status minlz_index_entry_get(const minlz_index *idx, size_t n,
                                   minlz_index_pair *pair) {
    if (!idx || !pair || n >= idx->len) {
        return MINLZ_CORRUPT;
    }
    pair->compressed_offset = idx->entries[n].compressed;
    pair->uncompressed_offset = idx->entries[n].uncompressed;
    return MINLZ_OK;
}

minlz_status minlz_index_json(const minlz_index *idx, char **json, size_t *json_len) {
    size_t cap;
    size_t len;
    size_t i;
    char *out;
    if (!idx || !json || !json_len) {
        return MINLZ_CORRUPT;
    }
    cap = 160 + idx->len * 96;
    out = (char *)malloc(cap);
    if (!out) {
        return MINLZ_IO;
    }
    len = (size_t)snprintf(out, cap,
                           "{\n  \"total_uncompressed\": %lld,\n"
                           "  \"total_compressed\": %lld,\n"
                           "  \"offsets\": [",
                           (long long)idx->total_uncompressed,
                           (long long)idx->total_compressed);
    for (i = 0; i < idx->len; i++) {
        int n = snprintf(out + len, cap - len,
                         "%s\n    {\"compressed\": %lld, \"uncompressed\": %lld}",
                         i ? "," : "",
                         (long long)idx->entries[i].compressed,
                         (long long)idx->entries[i].uncompressed);
        if (n < 0 || (size_t)n >= cap - len) {
            free(out);
            return MINLZ_IO;
        }
        len += (size_t)n;
    }
    {
        int n = snprintf(out + len, cap - len,
                         "\n  ],\n  \"est_block_uncompressed\": %lld\n}\n",
                         (long long)idx->est_block_uncomp);
        if (n < 0 || (size_t)n >= cap - len) {
            free(out);
            return MINLZ_IO;
        }
        len += (size_t)n;
    }
    *json = out;
    *json_len = len;
    return MINLZ_OK;
}

minlz_status minlz_index_remove_headers(const uint8_t *src, size_t src_len,
                                        const uint8_t **trimmed, size_t *trimmed_len) {
    size_t chunk_len;
    const uint8_t *p;
    size_t n;
    if (!src || !trimmed || !trimmed_len || src_len < MINLZ_CHUNK_HEADER_SIZE +
        MINLZ_INDEX_HEADER_LEN + 4u + MINLZ_INDEX_TRAILER_LEN || src[0] != MINLZ_CHUNK_INDEX) {
        return MINLZ_CORRUPT;
    }
    chunk_len = (size_t)src[1] | ((size_t)src[2] << 8) | ((size_t)src[3] << 16);
    if (chunk_len + MINLZ_CHUNK_HEADER_SIZE > src_len ||
        chunk_len < MINLZ_INDEX_HEADER_LEN + 4u + MINLZ_INDEX_TRAILER_LEN) {
        return MINLZ_CORRUPT;
    }
    p = src + MINLZ_CHUNK_HEADER_SIZE;
    if (memcmp(p, MINLZ_INDEX_HEADER, MINLZ_INDEX_HEADER_LEN) != 0) {
        return MINLZ_CORRUPT;
    }
    p += MINLZ_INDEX_HEADER_LEN;
    n = chunk_len - MINLZ_INDEX_HEADER_LEN - 4u - MINLZ_INDEX_TRAILER_LEN;
    if (memcmp(p + n + 4u, MINLZ_INDEX_TRAILER, MINLZ_INDEX_TRAILER_LEN) != 0) {
        return MINLZ_CORRUPT;
    }
    *trimmed = p;
    *trimmed_len = n;
    return MINLZ_OK;
}

minlz_status minlz_index_restore_headers(const uint8_t *src, size_t src_len,
                                         uint8_t **dst, size_t *dst_len) {
    size_t cap;
    uint32_t total_size;
    uint32_t chunk_len;
    if (!dst || !dst_len) {
        return MINLZ_CORRUPT;
    }
    *dst = NULL;
    *dst_len = 0;
    if (src_len == 0) {
        return MINLZ_OK;
    }
    if (src_len > MINLZ_MAX_USER_CHUNK_SIZE) {
        return MINLZ_TOO_LARGE;
    }
    cap = MINLZ_CHUNK_HEADER_SIZE + MINLZ_INDEX_HEADER_LEN + src_len + 4u +
          MINLZ_INDEX_TRAILER_LEN;
    *dst = (uint8_t *)malloc(cap);
    if (!*dst) {
        return MINLZ_IO;
    }
    (*dst)[0] = MINLZ_CHUNK_INDEX;
    chunk_len = (uint32_t)(MINLZ_INDEX_HEADER_LEN + src_len + 4u + MINLZ_INDEX_TRAILER_LEN);
    (*dst)[1] = (uint8_t)chunk_len;
    (*dst)[2] = (uint8_t)(chunk_len >> 8);
    (*dst)[3] = (uint8_t)(chunk_len >> 16);
    memcpy(*dst + 4, MINLZ_INDEX_HEADER, MINLZ_INDEX_HEADER_LEN);
    memcpy(*dst + 4 + MINLZ_INDEX_HEADER_LEN, src, src_len);
    total_size = (uint32_t)cap;
    minlz_store32(*dst + 4 + MINLZ_INDEX_HEADER_LEN + src_len, total_size);
    memcpy(*dst + cap - MINLZ_INDEX_TRAILER_LEN, MINLZ_INDEX_TRAILER,
           MINLZ_INDEX_TRAILER_LEN);
    *dst_len = cap;
    return MINLZ_OK;
}
