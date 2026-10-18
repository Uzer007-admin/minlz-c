/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#include <errno.h>
#include <stdlib.h>

const char *minlz_status_string(minlz_status status) {
    switch (status) {
    case MINLZ_OK:
        return "ok";
    case MINLZ_CORRUPT:
        return "minlz: corrupt input";
    case MINLZ_CRC:
        return "minlz: crc mismatch";
    case MINLZ_TOO_LARGE:
        return "minlz: block is too large";
    case MINLZ_UNSUPPORTED:
        return "minlz: unsupported input";
    case MINLZ_INVALID_LEVEL:
        return "minlz: invalid compression level";
    case MINLZ_DST_TOO_SMALL:
        return "minlz: destination buffer too small";
    case MINLZ_IO:
        return "minlz: io error";
    case MINLZ_SEARCH_TABLES_UNUSABLE:
        return "minlz: search tables cannot be used for this pattern";
    case MINLZ_SEARCH_STOP:
        return "minlz: search stopped";
    case MINLZ_SEARCH_FORWARD:
        return "minlz: search forward";
    default:
        return "minlz: unknown error";
    }
}

void minlz_free(void *p) {
    free(p);
}

size_t minlz_put_uvarint(uint8_t *dst, uint64_t v) {
    size_t n = 0;
    while (v >= 0x80) {
        dst[n++] = (uint8_t)(v | 0x80u);
        v >>= 7;
    }
    dst[n++] = (uint8_t)v;
    return n;
}

minlz_status minlz_read_uvarint(const uint8_t *src, size_t src_len, size_t *value, size_t *used) {
    uint64_t x = 0;
    unsigned shift = 0;
    size_t i;

    for (i = 0; i < src_len && i < 10; i++) {
        uint8_t b = src[i];
        if (b < 0x80) {
            if (i == 9 && b > 1) {
                return MINLZ_CORRUPT;
            }
            x |= (uint64_t)b << shift;
            if (x > (uint64_t)SIZE_MAX) {
                return MINLZ_TOO_LARGE;
            }
            *value = (size_t)x;
            *used = i + 1;
            return MINLZ_OK;
        }
        x |= (uint64_t)(b & 0x7fu) << shift;
        shift += 7;
    }
    return MINLZ_CORRUPT;
}

size_t minlz_put_varint(uint8_t *dst, int64_t v) {
    uint64_t ux = ((uint64_t)v) << 1;
    if (v < 0) {
        ux = ~ux;
    }
    return minlz_put_uvarint(dst, ux);
}

minlz_status minlz_write_all(minlz_write_cb cb, void *ctx, const uint8_t *src, size_t len) {
    if (!cb && len != 0) {
        return MINLZ_IO;
    }
    return cb(ctx, src, len);
}

minlz_status minlz_read_exact(minlz_read_cb cb, void *ctx, uint8_t *dst,
                              size_t len, int allow_eof, size_t *got) {
    size_t pos = 0;
    if (got) {
        *got = 0;
    }
    while (pos < len) {
        size_t n = 0;
        minlz_status st;
        if (!cb) {
            return MINLZ_IO;
        }
        st = cb(ctx, dst + pos, len - pos, &n);
        if (st != MINLZ_OK) {
            return st;
        }
        if (n == 0) {
            if (got) {
                *got = pos;
            }
            return (allow_eof && pos == 0) ? MINLZ_OK : MINLZ_CORRUPT;
        }
        pos += n;
    }
    if (got) {
        *got = pos;
    }
    return MINLZ_OK;
}

minlz_status minlz_skip_exact(minlz_read_cb cb, void *ctx, size_t len) {
    uint8_t tmp[4096];
    while (len > 0) {
        size_t want = len < sizeof(tmp) ? len : sizeof(tmp);
        minlz_status st = minlz_read_exact(cb, ctx, tmp, want, 0, NULL);
        if (st != MINLZ_OK) {
            return st;
        }
        len -= want;
    }
    return MINLZ_OK;
}
