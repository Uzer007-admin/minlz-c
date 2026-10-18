/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

#if !defined(_WIN32)
#include <sys/types.h>
#endif

static minlz_status minlz_file_read_cb(void *ctx, uint8_t *dst, size_t cap, size_t *nread) {
    FILE *f = (FILE *)ctx;
    *nread = fread(dst, 1, cap, f);
    if (*nread < cap && ferror(f)) {
        return MINLZ_IO;
    }
    return MINLZ_OK;
}

static minlz_status minlz_file_write_cb(void *ctx, const uint8_t *src, size_t len) {
    FILE *f = (FILE *)ctx;
    if (len == 0) {
        return MINLZ_OK;
    }
    return fwrite(src, 1, len, f) == len ? MINLZ_OK : MINLZ_IO;
}

static minlz_status minlz_file_seek_cb(void *ctx, int64_t offset, int whence, int64_t *new_pos) {
    FILE *f = (FILE *)ctx;
    int origin;
    int rc;
    int64_t pos;
    switch (whence) {
    case MINLZ_SEEK_SET:
        origin = SEEK_SET;
        break;
    case MINLZ_SEEK_CUR:
        origin = SEEK_CUR;
        break;
    case MINLZ_SEEK_END:
        origin = SEEK_END;
        break;
    default:
        return MINLZ_UNSUPPORTED;
    }
#if defined(_WIN32)
    rc = _fseeki64(f, offset, origin);
    pos = rc == 0 ? (int64_t)_ftelli64(f) : -1;
#elif defined(_LARGEFILE_SOURCE) || defined(_FILE_OFFSET_BITS)
    rc = fseeko(f, (off_t)offset, origin);
    pos = rc == 0 ? (int64_t)ftello(f) : -1;
#else
    if (offset > LONG_MAX || offset < LONG_MIN) {
        return MINLZ_TOO_LARGE;
    }
    rc = fseek(f, (long)offset, origin);
    pos = rc == 0 ? (int64_t)ftell(f) : -1;
#endif
    if (rc != 0 || pos < 0) {
        return MINLZ_IO;
    }
    if (new_pos) {
        *new_pos = pos;
    }
    return MINLZ_OK;
}

minlz_status minlz_stream_encode_file(FILE *in, FILE *out, const minlz_stream_options *opts) {
    if (!in || !out) {
        return MINLZ_IO;
    }
    return minlz_stream_encode(minlz_file_read_cb, in, minlz_file_write_cb, out, opts);
}

minlz_status minlz_stream_decode_file(FILE *in, FILE *out, const minlz_stream_options *opts) {
    if (!in || !out) {
        return MINLZ_IO;
    }
    return minlz_stream_decode(minlz_file_read_cb, in, minlz_file_write_cb, out, opts);
}

minlz_status minlz_reader_create_file(minlz_reader **reader, FILE *in,
                                      const minlz_stream_options *opts) {
    if (!in) {
        return MINLZ_IO;
    }
    return minlz_reader_create(reader, minlz_file_read_cb, in, opts);
}

minlz_status minlz_reader_create_seekable_file(minlz_reader **reader, FILE *in,
                                               const minlz_stream_options *opts) {
    if (!in) {
        return MINLZ_IO;
    }
    return minlz_reader_create_seekable(reader, minlz_file_read_cb, minlz_file_seek_cb,
                                        in, opts);
}

minlz_status minlz_writer_create_file(minlz_writer **writer, FILE *out,
                                      const minlz_stream_options *opts) {
    if (!out) {
        return MINLZ_IO;
    }
    return minlz_writer_create(writer, minlz_file_write_cb, out, opts);
}
