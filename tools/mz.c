/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz.h"
#include "mz_stats.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <fcntl.h>
#include <io.h>
#else
#include <glob.h>
#include <sys/types.h>
#endif

typedef struct cli_options {
    int decompress;
    int cat;
    int block;
    int stdout_mode;
    int quiet;
    int safe;
    int remove_src;
    int verify;
    const char *out_name;
    const char *sidecar_name;
    int auto_search_sidecar;
    int search_uncompressed;
    int search_compression_skip_set;
    int search_enabled;
    int search_extras;
    int search_max;
    int search_lim;
    int search_lim_set;
    const char *search_prefix_arg;
    const char *search_prefixes_arg;
    minlz_search_config search_config;
    minlz_stream_options stream;
} cli_options;

static void binary_stdio(void) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
}

static void usage(const char *argv0) {
    fprintf(stderr,
            "Usage:\n"
            "  %s c [options] <input...>\n"
            "  %s d [options] <input...>\n"
            "  %s cat [options] <input...>\n"
            "  %s search [options] <pattern> <input...>\n"
            "  %s sidecar build|extract [options] <input>\n"
            "  %s stats [options] <input...>\n\n"
            "Options: -0 -xfast -1 -2 -3 -bs SIZE -pad SIZE -block -c -o FILE -q -verify -safe -rm -index -no-index\n"
            "Search: -l (default) -n -offsets -c -q -v -window-stats -bail -ignore-crc -sidecar FILE -no-sidecar\n"
            "Search tables: -search -search.len N -search.prefixes BYTES -search.prefix TEXT -search.extras N -search.max PCT -search.lim PCT -search.uncompressed -search.compress.skip PCT -search.sidecar\n"
            "Sidecar build additionally accepts -search.lens 4,6,8 for multiple tables per block.\n",
            argv0, argv0, argv0, argv0, argv0, argv0);
}

static void search_usage(FILE *out, const char *name) {
    fprintf(out,
            "Search for a literal byte pattern in compressed MinLZ streams.\n\n"
            "Usage: %s search [options] <pattern> <input...>\n"
            "Options:\n"
            "  -bail          Return an error when search tables cannot be used\n"
            "  -c             Print only matching block/line counts\n"
            "  -help          Display this help\n"
            "  -l             Print matching lines (default)\n"
            "  -l=false       Print matching blocks instead of lines\n"
            "  -n             Print matching line numbers\n"
            "  -no-color      Disable colored output (C output is uncolored)\n"
            "  -no-sidecar    Disable sidecar auto-detection\n"
            "  -q             Quiet; exit 0 when found, 1 when absent\n"
            "  -sidecar FILE  Search through the supplied .mzs sidecar\n"
            "  -v             Print search statistics\n"
            "  -window-stats  Print per-window search-table statistics\n",
            name);
}

static void sidecar_usage(FILE *out, const char *name,
                          const char *subcommand) {
    if (!subcommand) {
        fprintf(out,
                "Sidecar search-index streams.\n\n"
                "Subcommands:\n"
                "  build    Build search indexes for an existing MinLZ stream\n"
                "  extract  Extract existing search-index chunks\n\n"
                "Usage: %s sidecar <build|extract> [options] <input>\n",
                name);
        return;
    }
    if (strcmp(subcommand, "build") == 0) {
        fprintf(out,
                "Build a sidecar search-index stream from a MinLZ stream.\n\n"
                "Usage: %s sidecar build [options] <input>\n"
                "Options:\n"
                "  -help                    Display this help\n"
                "  -o FILE                  Sidecar output (default <input>.mzs)\n"
                "  -q                       Quiet\n"
                "  -safe                    Do not overwrite an existing sidecar\n"
                "  -search.lens LIST        Comma-separated match lengths (default 6)\n"
                "  -search.prefixes BYTES   Prefix-byte filter\n"
                "  -search.prefix TEXT      Long prefix filter\n"
                "  -search.extras N         Extra long-prefix windows (0..15)\n"
                "  -search.max PCT          Maximum table population (default 75)\n"
                "  -search.lim PCT          Reduction limit (default 25)\n"
                "  -search.uncompressed     Disable search-table compression\n",
                name);
        return;
    }
    fprintf(out,
            "Extract search-index chunks into a sidecar stream.\n\n"
            "Usage: %s sidecar extract [options] <input>\n"
            "Options:\n"
            "  -help           Display this help\n"
            "  -newstream FILE Also write a stripped, independently seekable stream\n"
            "  -o FILE         Sidecar output (default <input>.mzs)\n"
            "  -q              Quiet\n"
            "  -safe           Do not overwrite existing files\n",
            name);
}

static int has_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    return n >= m && strcmp(s + n - m, suffix) == 0;
}

static char *xstrdup(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)malloc(n);
    if (p) {
        memcpy(p, s, n);
    }
    return p;
}

static char *append_suffix(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    char *p = (char *)malloc(n + m + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n);
    memcpy(p + n, suffix, m + 1);
    return p;
}

static int cli_add_file(char ***files, size_t *count,
                        size_t *capacity, const char *name) {
    char **next;
    char *copy;
    if (*count == *capacity) {
        size_t next_capacity = *capacity ? *capacity * 2u : 16u;
        if (next_capacity < *count ||
            next_capacity > SIZE_MAX / sizeof(**files)) {
            return 0;
        }
        next = (char **)realloc(
            *files, next_capacity * sizeof(**files));
        if (!next) {
            return 0;
        }
        *files = next;
        *capacity = next_capacity;
    }
    copy = xstrdup(name);
    if (!copy) {
        return 0;
    }
    (*files)[(*count)++] = copy;
    return 1;
}

static int cli_expand_file(char ***files, size_t *count,
                           size_t *capacity, const char *pattern) {
    if (strcmp(pattern, "-") == 0 ||
        strpbrk(pattern, "*?[") == NULL) {
        return cli_add_file(files, count, capacity, pattern);
    }
#ifdef _WIN32
    {
        struct _finddata_t found;
        intptr_t handle;
        const char *slash1 = strrchr(pattern, '\\');
        const char *slash2 = strrchr(pattern, '/');
        const char *slash =
            !slash1 ? slash2
                    : (!slash2 || slash1 > slash2 ? slash1 : slash2);
        size_t prefix_len =
            slash ? (size_t)(slash - pattern + 1) : 0u;
        handle = _findfirst(pattern, &found);
        if (handle == -1) {
            return 1;
        }
        do {
            char *path;
            size_t name_len;
            int ok;
            if ((found.attrib & _A_SUBDIR) != 0) {
                continue;
            }
            name_len = strlen(found.name);
            path = (char *)malloc(prefix_len + name_len + 1u);
            if (!path) {
                _findclose(handle);
                return 0;
            }
            memcpy(path, pattern, prefix_len);
            memcpy(path + prefix_len, found.name, name_len + 1u);
            ok = cli_add_file(files, count, capacity, path);
            free(path);
            if (!ok) {
                _findclose(handle);
                return 0;
            }
        } while (_findnext(handle, &found) == 0);
        _findclose(handle);
        return 1;
    }
#else
    {
        glob_t matches;
        size_t i;
        int rc;
        memset(&matches, 0, sizeof(matches));
        rc = glob(pattern, 0, NULL, &matches);
        if (rc == GLOB_NOMATCH) {
            globfree(&matches);
            return 1;
        }
        if (rc != 0) {
            globfree(&matches);
            return 0;
        }
        for (i = 0u; i < matches.gl_pathc; i++) {
            if (!cli_add_file(
                    files, count, capacity, matches.gl_pathv[i])) {
                globfree(&matches);
                return 0;
            }
        }
        globfree(&matches);
        return 1;
    }
#endif
}

static void cli_free_files(char **files, size_t count) {
    size_t i;
    for (i = 0u; i < count; i++) {
        free(files[i]);
    }
    free(files);
}

static char *trim_suffix_alloc(const char *s, const char *suffix) {
    size_t n = strlen(s);
    size_t m = strlen(suffix);
    char *p;
    if (n < m || strcmp(s + n - m, suffix) != 0) {
        return append_suffix(s, ".out");
    }
    p = (char *)malloc(n - m + 1);
    if (!p) {
        return NULL;
    }
    memcpy(p, s, n - m);
    p[n - m] = 0;
    return p;
}

static int file_exists(const char *name) {
    FILE *f = fopen(name, "rb");
    if (!f) {
        return 0;
    }
    fclose(f);
    return 1;
}

static int cli_same_path(const char *a, const char *b) {
    char *absolute_a;
    char *absolute_b;
    int same;
    if (!a || !b) {
        return 0;
    }
#ifdef _WIN32
    absolute_a = _fullpath(NULL, a, 0);
    absolute_b = _fullpath(NULL, b, 0);
    if (!absolute_a || !absolute_b) {
        free(absolute_a);
        free(absolute_b);
        return _stricmp(a, b) == 0;
    }
    same = _stricmp(absolute_a, absolute_b) == 0;
#else
    absolute_a = realpath(a, NULL);
    absolute_b = realpath(b, NULL);
    if (!absolute_a || !absolute_b) {
        free(absolute_a);
        free(absolute_b);
        return strcmp(a, b) == 0;
    }
    same = strcmp(absolute_a, absolute_b) == 0;
#endif
    free(absolute_a);
    free(absolute_b);
    return same;
}

static size_t parse_size(const char *s, int *ok) {
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    *ok = 1;
    if (end && *end) {
        if ((end[0] == 'K' || end[0] == 'k') && end[1] == 0) {
            v <<= 10;
        } else if ((end[0] == 'M' || end[0] == 'm') && end[1] == 0) {
            v <<= 20;
        } else if ((end[0] == 'G' || end[0] == 'g') && end[1] == 0) {
            v <<= 30;
        } else {
            *ok = 0;
        }
    }
    return (size_t)v;
}

static int read_all(FILE *f, uint8_t **out, size_t *out_len, size_t max_len) {
    size_t cap = 65536;
    size_t len = 0;
    uint8_t *buf = (uint8_t *)malloc(cap);
    if (!buf) {
        return 0;
    }
    for (;;) {
        size_t n;
        if (len == cap) {
            size_t next = cap * 2;
            uint8_t *p;
            if (next > max_len && cap < max_len) {
                next = max_len;
            }
            if (next == cap) {
                free(buf);
                return 0;
            }
            p = (uint8_t *)realloc(buf, next);
            if (!p) {
                free(buf);
                return 0;
            }
            buf = p;
            cap = next;
        }
        n = fread(buf + len, 1, cap - len, f);
        len += n;
        if (n == 0) {
            if (ferror(f)) {
                free(buf);
                return 0;
            }
            break;
        }
    }
    *out = buf;
    *out_len = len;
    return 1;
}

static minlz_status file_read_cb(void *ctx, uint8_t *dst, size_t cap, size_t *nread) {
    FILE *f = (FILE *)ctx;
    *nread = fread(dst, 1, cap, f);
    return (*nread < cap && ferror(f)) ? MINLZ_IO : MINLZ_OK;
}

static minlz_status file_write_cb(void *ctx, const uint8_t *src, size_t len) {
    FILE *f = (FILE *)ctx;
    return fwrite(src, 1, len, f) == len ? MINLZ_OK : MINLZ_IO;
}

static minlz_status file_read_at_cb(void *ctx, int64_t offset,
                                    uint8_t *dst, size_t len,
                                    size_t *nread) {
    FILE *f = (FILE *)ctx;
#ifdef _WIN32
    if (_fseeki64(f, offset, SEEK_SET) != 0) {
        return MINLZ_IO;
    }
#else
    if (fseeko(f, (off_t)offset, SEEK_SET) != 0) {
        return MINLZ_IO;
    }
#endif
    *nread = fread(dst, 1, len, f);
    return (*nread < len && ferror(f)) ? MINLZ_IO : MINLZ_OK;
}

static minlz_status discard_write_cb(void *ctx, const uint8_t *src, size_t len) {
    (void)ctx;
    (void)src;
    (void)len;
    return MINLZ_OK;
}

static int verify_stream(FILE *compressed, const minlz_stream_options *opts) {
    minlz_stream_options dec = *opts;
    dec.ignore_crc = 0;
    dec.allow_fallback = 0;
    dec.block_size = MINLZ_MAX_BLOCK_SIZE;
    rewind(compressed);
    return minlz_stream_decode(file_read_cb, compressed, discard_write_cb, NULL, &dec) == MINLZ_OK;
}

static int compress_block(FILE *in, FILE *out, const cli_options *opt) {
    uint8_t *src = NULL;
    uint8_t *enc = NULL;
    uint8_t *dec = NULL;
    size_t src_len = 0;
    size_t enc_len = 0;
    size_t dec_len = 0;
    minlz_status st;
    int ok = 0;
    if (!read_all(in, &src, &src_len, MINLZ_MAX_BLOCK_SIZE)) {
        fprintf(stderr, "ERROR: unable to read input or input exceeds 8MiB\n");
        goto done;
    }
    enc = (uint8_t *)malloc(minlz_max_encoded_len(src_len));
    if (!enc) {
        goto done;
    }
    st = minlz_encode(enc, minlz_max_encoded_len(src_len), &enc_len,
                      src, src_len, opt->stream.level);
    if (st != MINLZ_OK) {
        fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
        goto done;
    }
    if (opt->verify) {
        st = minlz_decoded_len(enc, enc_len, &dec_len);
        if (st != MINLZ_OK) {
            fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
            goto done;
        }
        dec = (uint8_t *)malloc(dec_len ? dec_len : 1);
        if (!dec) {
            goto done;
        }
        st = minlz_decode(dec, dec_len, &dec_len, enc, enc_len);
        if (st != MINLZ_OK || dec_len != src_len || memcmp(dec, src, src_len) != 0) {
            fprintf(stderr, "ERROR: verification failed\n");
            goto done;
        }
    } else if (fwrite(enc, 1, enc_len, out) != enc_len) {
        fprintf(stderr, "ERROR: write failed\n");
        goto done;
    }
    ok = 1;
done:
    free(src);
    free(enc);
    free(dec);
    return ok;
}

static int decompress_block(FILE *in, FILE *out, const cli_options *opt) {
    uint8_t *src = NULL;
    uint8_t *dec = NULL;
    size_t src_len = 0;
    size_t dec_len = 0;
    minlz_status st;
    int ok = 0;
    if (!read_all(in, &src, &src_len, minlz_max_encoded_len(MINLZ_MAX_BLOCK_SIZE))) {
        fprintf(stderr, "ERROR: unable to read block\n");
        goto done;
    }
    st = minlz_decoded_len(src, src_len, &dec_len);
    if (st != MINLZ_OK) {
        fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
        goto done;
    }
    dec = (uint8_t *)malloc(dec_len ? dec_len : 1);
    if (!dec) {
        goto done;
    }
    st = minlz_decode(dec, dec_len, &dec_len, src, src_len);
    if (st != MINLZ_OK) {
        fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
        goto done;
    }
    if (!opt->verify && fwrite(dec, 1, dec_len, out) != dec_len) {
        fprintf(stderr, "ERROR: write failed\n");
        goto done;
    }
    ok = 1;
done:
    free(src);
    free(dec);
    return ok;
}

static int compress_stream(FILE *in, FILE *out, const cli_options *opt) {
    minlz_status st;
    if (opt->verify) {
        FILE *tmp = tmpfile();
        if (!tmp) {
            fprintf(stderr, "ERROR: unable to create temporary file\n");
            return 0;
        }
        st = minlz_stream_encode_file(in, tmp, &opt->stream);
        if (st == MINLZ_OK && !verify_stream(tmp, &opt->stream)) {
            st = MINLZ_CORRUPT;
        }
        fclose(tmp);
    } else {
        st = minlz_stream_encode_file(in, out, &opt->stream);
    }
    if (st != MINLZ_OK) {
        fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
        return 0;
    }
    return 1;
}

static int decompress_stream(FILE *in, FILE *out, const cli_options *opt) {
    minlz_stream_options dec = opt->stream;
    minlz_status st;
    dec.block_size = MINLZ_MAX_BLOCK_SIZE;
    dec.allow_fallback = 1;
    if (opt->verify) {
        st = minlz_stream_decode(file_read_cb, in, discard_write_cb, NULL, &dec);
    } else {
        st = minlz_stream_decode_file(in, out, &dec);
    }
    if (st != MINLZ_OK) {
        fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
        return 0;
    }
    return 1;
}

static int process_file(const char *name, const cli_options *opt, int multiple) {
    FILE *in = NULL;
    FILE *out = NULL;
    FILE *sidecar = NULL;
    char *out_name = NULL;
    char *sidecar_out_name = NULL;
    int ok = 0;
    cli_options run_opt = *opt;
    int use_stdio_in = strcmp(name, "-") == 0;
    int stdout_mode =
        opt->stdout_mode || opt->cat ||
        (use_stdio_in && !opt->out_name);

    if (use_stdio_in) {
        in = stdin;
    } else {
        in = fopen(name, "rb");
        if (!in) {
            fprintf(stderr, "ERROR: unable to open %s\n", name);
            return 0;
        }
    }

    if (opt->verify) {
        out = stdout;
    } else if (stdout_mode) {
        out = stdout;
    } else {
        if (opt->out_name) {
            if (multiple) {
                fprintf(stderr, "ERROR: -o can only be used with a single input\n");
                goto done;
            }
            out_name = xstrdup(opt->out_name);
        } else if (opt->decompress) {
            if (opt->block || has_suffix(name, ".mzb")) {
                out_name = trim_suffix_alloc(name, ".mzb");
            } else {
                out_name = trim_suffix_alloc(name, ".mz");
            }
        } else {
            out_name = append_suffix(name, opt->block ? ".mzb" : ".mz");
        }
        if (!out_name) {
            goto done;
        }
        if (opt->safe && file_exists(out_name)) {
            fprintf(stderr, "ERROR: destination exists: %s\n", out_name);
            goto done;
        }
        out = fopen(out_name, "wb");
        if (!out) {
            fprintf(stderr, "ERROR: unable to create %s\n", out_name);
            goto done;
        }
    }

    if (!opt->quiet && !opt->verify && !stdout_mode) {
        fprintf(stderr, "%s %s -> %s\n", opt->decompress ? "Decompressing" : "Compressing",
                name, out_name ? out_name : "(stdout)");
    }

    if (!opt->decompress && !opt->block &&
        (opt->sidecar_name || opt->auto_search_sidecar)) {
        const char *sidecar_name = opt->sidecar_name;
        if (opt->sidecar_name && multiple) {
            fprintf(stderr,
                    "ERROR: -sidecar can only be used with a single input\n");
            goto done;
        }
        if (opt->verify) {
            fprintf(stderr,
                    "ERROR: -sidecar cannot be combined with -verify\n");
            goto done;
        }
        if (opt->auto_search_sidecar) {
            if (!out_name) {
                fprintf(stderr,
                        "ERROR: -search.sidecar needs a file output\n");
                goto done;
            }
            sidecar_out_name = append_suffix(out_name, ".mzs");
            if (!sidecar_out_name) {
                goto done;
            }
            sidecar_name = sidecar_out_name;
        }
        if (opt->safe && file_exists(sidecar_name)) {
            fprintf(stderr,
                    "ERROR: sidecar destination exists: %s\n",
                    sidecar_name);
            goto done;
        }
        sidecar = fopen(sidecar_name, "wb");
        if (!sidecar) {
            fprintf(stderr, "ERROR: unable to create %s\n",
                    sidecar_name);
            goto done;
        }
        run_opt.stream.sidecar_write_cb = file_write_cb;
        run_opt.stream.sidecar_write_ctx = sidecar;
    }

    if (run_opt.decompress) {
        ok = run_opt.block ? decompress_block(in, out, &run_opt)
                           : decompress_stream(in, out, &run_opt);
    } else {
        ok = run_opt.block ? compress_block(in, out, &run_opt)
                           : compress_stream(in, out, &run_opt);
    }

    if (ok && opt->remove_src && !opt->verify && !use_stdio_in) {
        remove(name);
    }

done:
    if (sidecar) {
        fclose(sidecar);
    }
    if (out && out != stdout) {
        fclose(out);
    }
    if (in && in != stdin) {
        fclose(in);
    }
    free(out_name);
    free(sidecar_out_name);
    return ok;
}

typedef struct cli_search_output {
    const char *name;
    int multiple;
    int quiet;
    int stop_after_first;
    int lines;
    int go_style_offsets;
    int line_numbers;
    size_t pattern_len;
    int have_last_line;
    int64_t last_line_start;
    int64_t contig_end;
    uint64_t line_number;
    uint64_t matches;
} cli_search_output;

static uint8_t cli_search_result_byte(
    const minlz_search_result *result, size_t pos) {
    if (pos < result->previous_block_len) {
        return result->previous_block[pos];
    }
    return result->block[pos - result->previous_block_len];
}

static minlz_status cli_search_match_cb(
    void *ctx, const minlz_search_result *result) {
    cli_search_output *out = (cli_search_output *)ctx;
    minlz_search_result loaded_result;
    if (out->lines && !result->previous_block &&
        result->previous_block_len != 0u) {
        const uint8_t *previous = NULL;
        size_t previous_len = 0u;
        minlz_status st = minlz_search_result_load_previous(
            result, &previous, &previous_len);
        if (st != MINLZ_OK) {
            return st;
        }
        if (!previous ||
            previous_len != result->previous_block_len) {
            return MINLZ_CORRUPT;
        }
        loaded_result = *result;
        loaded_result.previous_block = previous;
        result = &loaded_result;
    }
    if (out->lines) {
        size_t total = result->previous_block_len + result->block_len;
        size_t start;
        size_t end;
        int64_t line_start;
        int line_start_found;
        if (result->offset < 0 ||
            (uint64_t)result->offset > (uint64_t)total) {
            return MINLZ_CORRUPT;
        }
        start = (size_t)result->offset;
        end = start + out->pattern_len;
        if (end > total) {
            end = total;
        }
        while (start != 0u &&
               cli_search_result_byte(result, start - 1u) != '\n') {
            start--;
        }
        line_start_found = start != 0u;
        while (end < total &&
               cli_search_result_byte(result, end) != '\n') {
            end++;
        }
        line_start = result->block_start + (int64_t)start;
        if (!line_start_found && out->have_last_line &&
            result->block_start <= out->contig_end) {
            line_start = out->last_line_start;
        }
        out->contig_end =
            result->block_start + (int64_t)total;
        if (out->have_last_line &&
            line_start == out->last_line_start) {
            return MINLZ_OK;
        }
        out->have_last_line = 1;
        out->last_line_start = line_start;
        out->matches++;
        out->line_number++;
        if (!out->quiet) {
            size_t pos;
            if (out->multiple) {
                printf("%s:", out->name);
            }
            if (out->line_numbers) {
                printf("%llu:", (unsigned long long)out->line_number);
            }
            printf("%lld:", (long long)result->stream_offset);
            for (pos = start; pos < end; pos++) {
                putchar((int)cli_search_result_byte(result, pos));
            }
            putchar('\n');
        }
    } else {
        out->matches++;
        if (!out->quiet) {
            if (out->multiple) {
                printf("%s:", out->name);
            }
            printf(out->go_style_offsets ? "%lld:\n" : "%lld\n",
                   (long long)result->stream_offset);
        }
    }
    return out->stop_after_first ? MINLZ_SEARCH_STOP : MINLZ_OK;
}

typedef struct cli_search_info_output {
    const char *name;
} cli_search_info_output;

static void cli_search_info_cb(
    void *ctx, const minlz_search_config *config) {
    cli_search_info_output *out =
        (cli_search_info_output *)ctx;
    char description[1400];
    if (!out || !config) {
        return;
    }
    minlz_search_config_snprint(
        description, sizeof(description), config);
    fprintf(stderr, "%s: search info: %s\n",
            out->name, description);
}

static int run_search_command(int argc, char **argv) {
    const char *pattern = NULL;
    const char *sidecar_name = NULL;
    const char **patterns;
    int pattern_count = 0;
    char **files = NULL;
    size_t file_count = 0u;
    size_t file_capacity = 0u;
    int quiet = 0;
    int count_only = 0;
    int bail = 0;
    int ignore_crc = 0;
    int verbose = 0;
    int window_stats = 0;
    int lines = 1;
    int go_style_offsets = 0;
    int line_numbers = 0;
    int no_sidecar = 0;
    int found = 0;
    int multiple_inputs = 0;
    int i;

    patterns =
        (const char **)calloc((size_t)argc, sizeof(*patterns));
    if (!patterns) {
        return 2;
    }
    for (i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-help") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0) {
            search_usage(stdout, argv[0]);
            free(patterns);
            return 0;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-c") == 0) {
            count_only = 1;
        } else if (strcmp(argv[i], "-bail") == 0) {
            bail = 1;
        } else if (strcmp(argv[i], "-ignore-crc") == 0) {
            ignore_crc = 1;
        } else if (strcmp(argv[i], "-v") == 0) {
            verbose = 1;
        } else if (strcmp(argv[i], "-window-stats") == 0) {
            window_stats = 1;
            verbose = 1;
        } else if (strcmp(argv[i], "-no-color") == 0) {
            /* C output is intentionally uncolored. */
        } else if (strcmp(argv[i], "-l") == 0 ||
                   strcmp(argv[i], "-lines") == 0) {
            lines = 1;
            go_style_offsets = 0;
        } else if (strcmp(argv[i], "-l=false") == 0 ||
                   strcmp(argv[i], "--l=false") == 0) {
            lines = 0;
            go_style_offsets = 1;
        } else if (strcmp(argv[i], "-l=true") == 0 ||
                   strcmp(argv[i], "--l=true") == 0) {
            lines = 1;
            go_style_offsets = 0;
        } else if (strcmp(argv[i], "-offsets") == 0) {
            lines = 0;
            go_style_offsets = 0;
        } else if (strcmp(argv[i], "-n") == 0) {
            line_numbers = 1;
            lines = 1;
        } else if (strcmp(argv[i], "-no-sidecar") == 0) {
            no_sidecar = 1;
        } else if (strcmp(argv[i], "-sidecar") == 0 &&
                   i + 1 < argc) {
            sidecar_name = argv[++i];
        } else if (!pattern) {
            pattern = argv[i];
        } else {
            patterns[pattern_count++] = argv[i];
        }
    }
    if (!pattern || pattern_count == 0) {
        usage(argv[0]);
        free(patterns);
        return 1;
    }
    multiple_inputs = pattern_count > 1;
    for (i = 0; i < pattern_count; i++) {
        if (!cli_expand_file(
                &files, &file_count, &file_capacity,
                patterns[i])) {
            fprintf(stderr, "ERROR: unable to expand %s\n",
                    patterns[i]);
            free(patterns);
            cli_free_files(files, file_count);
            return 2;
        }
    }
    free(patterns);

    for (i = 0; (size_t)i < file_count; i++) {
        const char *name = files[i];
        const char *selected_sidecar = sidecar_name;
        FILE *main_file = NULL;
        FILE *sidecar_file = NULL;
        char *auto_sidecar = NULL;
        cli_search_output output;
        minlz_search_stats stats;
        minlz_status st;
        cli_search_info_output info_output;
        clock_t started = clock();

        memset(&output, 0, sizeof(output));
        output.name = name;
        output.multiple = multiple_inputs;
        output.quiet = quiet || count_only;
        output.stop_after_first = quiet;
        output.lines = lines;
        output.go_style_offsets = go_style_offsets;
        output.line_numbers = line_numbers;
        output.pattern_len = strlen(pattern);
        output.contig_end = -1;
        main_file = strcmp(name, "-") == 0
                        ? stdin
                        : fopen(name, "rb");
        if (!main_file) {
            fprintf(stderr, "ERROR: unable to open %s\n", name);
            continue;
        }
        if (!selected_sidecar && !no_sidecar &&
            strcmp(name, "-") != 0) {
            auto_sidecar = append_suffix(name, ".mzs");
            if (auto_sidecar && file_exists(auto_sidecar)) {
                selected_sidecar = auto_sidecar;
                if (verbose) {
                    fprintf(stderr, "%s: using sidecar %s\n",
                            name, selected_sidecar);
                }
            }
        }

        info_output.name = name;
        if (selected_sidecar) {
            minlz_sidecar_searcher *searcher = NULL;
            if (main_file == stdin) {
                fprintf(stderr,
                        "ERROR: sidecar search needs a seekable main file\n");
                st = MINLZ_UNSUPPORTED;
            } else {
                sidecar_file = fopen(selected_sidecar, "rb");
                if (!sidecar_file) {
                    fprintf(stderr, "ERROR: unable to open %s\n",
                            selected_sidecar);
                    st = MINLZ_IO;
                } else {
                    st = minlz_sidecar_searcher_create(
                        &searcher, file_read_at_cb, main_file,
                        file_read_cb, sidecar_file);
                    if (st == MINLZ_OK) {
                        minlz_sidecar_searcher_set_ignore_crc(
                            searcher, ignore_crc);
                        minlz_sidecar_searcher_set_bail_on_missing(
                            searcher, bail);
                        minlz_sidecar_searcher_set_collect_stats(
                            searcher, verbose);
                        if (verbose) {
                            minlz_sidecar_searcher_set_info_callback(
                                searcher, cli_search_info_cb,
                                &info_output);
                        }
                        st = minlz_sidecar_searcher_search(
                            searcher, (const uint8_t *)pattern,
                            strlen(pattern), cli_search_match_cb, &output);
                        if (verbose &&
                            minlz_sidecar_searcher_stats(
                                searcher, &stats) == MINLZ_OK) {
                            fprintf(stderr, "%s:\n", name);
                            if (window_stats) {
                                size_t count =
                                    minlz_sidecar_searcher_window_count(
                                        searcher);
                                minlz_search_window_stat *windows =
                                    count
                                        ? (minlz_search_window_stat *)calloc(
                                              count, sizeof(*windows))
                                        : NULL;
                                size_t wi;
                                if (count == 0u || windows) {
                                    for (wi = 0u; wi < count; wi++) {
                                        if (minlz_sidecar_searcher_window_get(
                                                searcher, wi,
                                                &windows[wi]) != MINLZ_OK) {
                                            count = wi;
                                            break;
                                        }
                                    }
                                    minlz_search_stats_fprint_extended(
                                        stderr, &stats, windows, count);
                                } else {
                                    minlz_search_stats_fprint(
                                        stderr, &stats);
                                }
                                free(windows);
                            } else {
                                minlz_search_stats_fprint(
                                    stderr, &stats);
                            }
                        }
                    }
                    minlz_sidecar_searcher_destroy(searcher);
                }
            }
        } else {
            minlz_block_searcher *searcher = NULL;
            st = minlz_block_searcher_create(
                &searcher, file_read_cb, main_file);
            if (st == MINLZ_OK) {
                minlz_block_searcher_set_ignore_crc(
                    searcher, ignore_crc);
                minlz_block_searcher_set_bail_on_missing(
                    searcher, bail);
                minlz_block_searcher_set_collect_stats(
                    searcher, verbose);
                if (verbose) {
                    minlz_block_searcher_set_info_callback(
                        searcher, cli_search_info_cb,
                        &info_output);
                }
                st = minlz_block_searcher_search(
                    searcher, (const uint8_t *)pattern,
                    strlen(pattern), cli_search_match_cb, &output);
                if (verbose &&
                    minlz_block_searcher_stats(
                        searcher, &stats) == MINLZ_OK) {
                    fprintf(stderr, "%s:\n", name);
                    if (window_stats) {
                        size_t count =
                            minlz_block_searcher_window_count(
                                searcher);
                        minlz_search_window_stat *windows =
                            count
                                ? (minlz_search_window_stat *)calloc(
                                      count, sizeof(*windows))
                                : NULL;
                        size_t wi;
                        if (count == 0u || windows) {
                            for (wi = 0u; wi < count; wi++) {
                                if (minlz_block_searcher_window_get(
                                        searcher, wi,
                                        &windows[wi]) != MINLZ_OK) {
                                    count = wi;
                                    break;
                                }
                            }
                            minlz_search_stats_fprint_extended(
                                stderr, &stats, windows, count);
                        } else {
                            minlz_search_stats_fprint(
                                stderr, &stats);
                        }
                        free(windows);
                    } else {
                        minlz_search_stats_fprint(
                            stderr, &stats);
                    }
                }
            }
            minlz_block_searcher_destroy(searcher);
        }
        if (st != MINLZ_OK) {
            fprintf(stderr, "ERROR: %s: %s\n", name,
                    minlz_status_string(st));
            if (sidecar_file) {
                fclose(sidecar_file);
            }
            if (main_file != stdin) {
                fclose(main_file);
            }
            free(auto_sidecar);
            continue;
        }
        if (count_only && !quiet) {
            if (file_count > 1u) {
                printf("%s: ", name);
            }
            printf("%llu\n", (unsigned long long)output.matches);
        }
        found |= output.matches != 0u;
        if (sidecar_file) {
            fclose(sidecar_file);
        }
        if (main_file != stdin) {
            fclose(main_file);
        }
        if (verbose) {
            double elapsed =
                (double)(clock() - started) / (double)CLOCKS_PER_SEC;
            double mbps =
                elapsed > 0.0
                    ? (double)stats.uncompressed_size /
                          (elapsed * 1000000.0)
                    : 0.0;
            fprintf(stderr, "%s took %.3fs %.1f MB/s\n",
                    name, elapsed, mbps);
        }
        free(auto_sidecar);
    }
    cli_free_files(files, file_count);
    return quiet ? (found ? 0 : 1) : 0;
}

static int run_sidecar_command(int argc, char **argv) {
    const char *subcommand;
    const char *input = NULL;
    const char *output_name = NULL;
    const char *newstream_name = NULL;
    const char *search_lens = "6";
    const char *prefixes = NULL;
    const char *long_prefix = NULL;
    minlz_search_config configs[8];
    size_t config_count = 0;
    FILE *in = NULL;
    FILE *out = NULL;
    FILE *newstream = NULL;
    char *default_output = NULL;
    minlz_status st;
    int i;
    int safe = 0;
    int quiet = 0;
    int extras = 0;
    int max_population = 75;
    int reduced_population = 25;
    int reduced_population_set = 0;
    int uncompressed = 0;

    if (argc < 3 ||
        (argc == 3 &&
         (strcmp(argv[2], "-help") == 0 ||
          strcmp(argv[2], "--help") == 0 ||
          strcmp(argv[2], "-h") == 0))) {
        sidecar_usage(argc < 3 ? stderr : stdout, argv[0], NULL);
        return argc < 3 ? 1 : 0;
    }
    subcommand = argv[2];
    if (strcmp(subcommand, "build") != 0 &&
        strcmp(subcommand, "extract") != 0) {
        fprintf(stderr, "ERROR: unknown sidecar subcommand %s\n",
                subcommand);
        sidecar_usage(stderr, argv[0], NULL);
        return 1;
    }
    if (argc < 4) {
        sidecar_usage(stderr, argv[0], subcommand);
        return 1;
    }
    for (i = 3; i < argc; i++) {
        if (strcmp(argv[i], "-help") == 0 ||
            strcmp(argv[i], "--help") == 0 ||
            strcmp(argv[i], "-h") == 0) {
            sidecar_usage(stdout, argv[0], subcommand);
            return 0;
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_name = argv[++i];
        } else if (strcmp(argv[i], "-newstream") == 0 &&
                   i + 1 < argc) {
            newstream_name = argv[++i];
        } else if (strcmp(argv[i], "-safe") == 0) {
            safe = 1;
        } else if (strcmp(argv[i], "-q") == 0) {
            quiet = 1;
        } else if (strcmp(argv[i], "-search.lens") == 0 &&
                   i + 1 < argc) {
            search_lens = argv[++i];
        } else if (strcmp(argv[i], "-search.len") == 0 &&
                   i + 1 < argc) {
            search_lens = argv[++i];
        } else if (strcmp(argv[i], "-search.prefixes") == 0 &&
                   i + 1 < argc) {
            prefixes = argv[++i];
        } else if (strcmp(argv[i], "-search.prefix") == 0 &&
                   i + 1 < argc) {
            long_prefix = argv[++i];
        } else if (strcmp(argv[i], "-search.extras") == 0 &&
                   i + 1 < argc) {
            extras = atoi(argv[++i]);
            if (extras < 0 || extras > 15) {
                fprintf(stderr, "ERROR: invalid search extras\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-search.max") == 0 &&
                   i + 1 < argc) {
            max_population = atoi(argv[++i]);
            if (max_population < 0 || max_population > 100) {
                fprintf(stderr, "ERROR: invalid search population\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-search.lim") == 0 &&
                   i + 1 < argc) {
            reduced_population = atoi(argv[++i]);
            reduced_population_set = 1;
            if (reduced_population < 0 ||
                reduced_population > 100) {
                fprintf(stderr,
                        "ERROR: invalid reduced search population\n");
                return 1;
            }
        } else if (strcmp(argv[i], "-search.uncompressed") == 0) {
            uncompressed = 1;
        } else {
            input = argv[i];
        }
    }
    if (!input) {
        sidecar_usage(stderr, argv[0], subcommand);
        return 1;
    }
    if (prefixes && prefixes[0] != '\0' &&
        long_prefix && long_prefix[0] != '\0') {
        fprintf(stderr,
                "ERROR: cannot combine -search.prefixes and -search.prefix\n");
        return 1;
    }
    if (strcmp(subcommand, "build") == 0) {
        const char *p = search_lens;
        while (*p != '\0') {
            char *end = NULL;
            long match_len;
            minlz_search_config *config;
            while (*p == ' ' || *p == '\t' || *p == ',') {
                p++;
            }
            if (*p == '\0') {
                break;
            }
            match_len = strtol(p, &end, 10);
            if (end == p || match_len < 1 || match_len > 8 ||
                config_count == sizeof(configs) / sizeof(configs[0])) {
                fprintf(stderr, "ERROR: invalid -search.lens value\n");
                return 1;
            }
            p = end;
            while (*p == ' ' || *p == '\t') {
                p++;
            }
            if (*p != '\0' && *p != ',') {
                fprintf(stderr, "ERROR: invalid -search.lens list\n");
                return 1;
            }
            config = configs + config_count++;
            minlz_search_config_init(config);
            st = minlz_search_config_set_match_len(
                config, (unsigned)match_len);
            if (st == MINLZ_OK) {
                st = minlz_search_config_set_max_population(
                    config, (unsigned)max_population);
            }
            if (st == MINLZ_OK && long_prefix) {
                size_t prefix_len = strlen(long_prefix);
                if (prefix_len == 0u) {
                    if (extras != 0) {
                        fprintf(stderr,
                                "ERROR: -search.extras requires "
                                "-search.prefix\n");
                        return 1;
                    }
                } else if (extras != 0 || prefix_len > 1u) {
                    st = minlz_search_config_set_long_prefix(
                        config, (const uint8_t *)long_prefix,
                        prefix_len, (unsigned)extras);
                } else {
                    st = minlz_search_config_set_byte_prefix(
                        config, (const uint8_t *)long_prefix,
                        prefix_len);
                }
            } else if (st == MINLZ_OK && prefixes &&
                       prefixes[0] != '\0') {
                if (extras != 0) {
                    fprintf(stderr,
                            "ERROR: -search.extras requires -search.prefix\n");
                    return 1;
                }
                st = minlz_search_config_set_byte_prefix(
                    config, (const uint8_t *)prefixes,
                    strlen(prefixes));
            } else if (st == MINLZ_OK && extras != 0) {
                fprintf(stderr,
                        "ERROR: -search.extras requires -search.prefix\n");
                return 1;
            }
            if (st == MINLZ_OK && reduced_population_set) {
                st =
                    minlz_search_config_set_max_reduced_population(
                        config, (unsigned)reduced_population);
            }
            if (st != MINLZ_OK) {
                fprintf(stderr, "ERROR: invalid search configuration\n");
                return 1;
            }
            if (uncompressed) {
                minlz_search_config_set_compression(config, 0);
            }
        }
        if (config_count == 0u) {
            fprintf(stderr, "ERROR: no search lengths supplied\n");
            return 1;
        }
    }
    if (!output_name) {
        default_output = append_suffix(input, ".mzs");
        if (!default_output) {
            fprintf(stderr, "ERROR: unable to allocate sidecar filename\n");
            return 2;
        }
        output_name = default_output;
    }
    if (cli_same_path(input, output_name)) {
        fprintf(stderr,
                "ERROR: sidecar destination would overwrite input\n");
        free(default_output);
        return 1;
    }
    if (newstream_name &&
        (cli_same_path(input, newstream_name) ||
         cli_same_path(output_name, newstream_name))) {
        fprintf(stderr,
                "ERROR: -newstream collides with input or sidecar\n");
        free(default_output);
        return 1;
    }
    if (strcmp(subcommand, "build") == 0 && newstream_name) {
        fprintf(stderr,
                "ERROR: -newstream is only valid for extract\n");
        free(default_output);
        return 1;
    }
    if ((safe && file_exists(output_name)) ||
        (newstream_name && safe && file_exists(newstream_name))) {
        fprintf(stderr, "ERROR: destination exists\n");
        free(default_output);
        return 1;
    }
    in = fopen(input, "rb");
    if (in) {
        out = fopen(output_name, "wb");
    }
    if (in && out && newstream_name) {
        newstream = fopen(newstream_name, "wb");
    }
    if (!in || !out || (newstream_name && !newstream)) {
        fprintf(stderr, "ERROR: unable to open sidecar files\n");
        st = MINLZ_IO;
    } else if (strcmp(subcommand, "build") == 0) {
        st = minlz_build_sidecar(
            file_write_cb, out, file_read_cb, in,
            configs, config_count, 0);
    } else {
        st = minlz_extract_sidecar(
            file_write_cb, out,
            newstream ? file_write_cb : NULL, newstream,
            file_read_cb, in);
    }
    if (newstream) {
        fclose(newstream);
    }
    if (out) {
        fclose(out);
    }
    if (in) {
        fclose(in);
    }
    if (st != MINLZ_OK) {
        fprintf(stderr, "ERROR: %s\n", minlz_status_string(st));
        free(default_output);
        return 2;
    }
    if (!quiet) {
        fprintf(stderr, "%s: OK\n", output_name);
    }
    free(default_output);
    return 0;
}

int main(int argc, char **argv) {
    cli_options opt;
    const char **files;
    int file_count = 0;
    int i;
    int first = 1;
    int explicit_mode = 0;
    binary_stdio();
    memset(&opt, 0, sizeof(opt));
    minlz_stream_options_init(&opt.stream);
    minlz_search_config_init(&opt.search_config);
    opt.search_max = 75;
    opt.search_lim = 25;
    opt.stream.block_size = MINLZ_MAX_BLOCK_SIZE;
    opt.stream.add_index = 1;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }
    if (strcmp(argv[1], "version") == 0 ||
        strcmp(argv[1], "--version") == 0 ||
        strcmp(argv[1], "-version") == 0) {
        printf("minlz-c %s (search spec %s)\n",
               MINLZ_VERSION_STRING,
               MINLZ_SEARCH_SPEC_VERSION);
        return 0;
    }
    if (strcmp(argv[1], "search") == 0 ||
        strcmp(argv[1], "find") == 0 ||
        strcmp(argv[1], "s") == 0) {
        return run_search_command(argc, argv);
    }
    if (strcmp(argv[1], "sidecar") == 0) {
        return run_sidecar_command(argc, argv);
    }
    if (strcmp(argv[1], "stats") == 0) {
        return mz_run_stats(argc, argv);
    }
    if (strcmp(argv[1], "c") == 0 || strcmp(argv[1], "compress") == 0) {
        opt.decompress = 0;
        first = 2;
        explicit_mode = 1;
    } else if (strcmp(argv[1], "d") == 0 || strcmp(argv[1], "decompress") == 0) {
        opt.decompress = 1;
        first = 2;
        explicit_mode = 1;
    } else if (strcmp(argv[1], "cat") == 0) {
        opt.decompress = 1;
        opt.cat = 1;
        opt.stdout_mode = 1;
        first = 2;
        explicit_mode = 1;
    }

    files = (const char **)calloc((size_t)argc, sizeof(*files));
    if (!files) {
        return 2;
    }

    for (i = first; i < argc; i++) {
        const char *a = argv[i];
        if (strcmp(a, "-0") == 0) {
            opt.stream.level = MINLZ_LEVEL_UNCOMPRESSED;
        } else if (strcmp(a, "-xfast") == 0) {
            opt.stream.level = MINLZ_LEVEL_SUPERFAST;
        } else if (strcmp(a, "-1") == 0) {
            opt.stream.level = MINLZ_LEVEL_FASTEST;
        } else if (strcmp(a, "-2") == 0) {
            opt.stream.level = MINLZ_LEVEL_BALANCED;
        } else if (strcmp(a, "-3") == 0) {
            opt.stream.level = MINLZ_LEVEL_SMALLEST;
        } else if (strcmp(a, "-bs") == 0 && i + 1 < argc) {
            int ok;
            opt.stream.block_size = parse_size(argv[++i], &ok);
            if (!ok) {
                fprintf(stderr, "ERROR: invalid block size\n");
                free(files);
                return 1;
            }
        } else if (strcmp(a, "-pad") == 0 && i + 1 < argc) {
            int ok;
            opt.stream.padding = parse_size(argv[++i], &ok);
            if (!ok) {
                fprintf(stderr, "ERROR: invalid padding size\n");
                free(files);
                return 1;
            }
        } else if (strcmp(a, "-block") == 0) {
            opt.block = 1;
        } else if (strcmp(a, "-c") == 0) {
            opt.stdout_mode = 1;
        } else if (strcmp(a, "-o") == 0 && i + 1 < argc) {
            opt.out_name = argv[++i];
        } else if (strcmp(a, "-q") == 0) {
            opt.quiet = 1;
        } else if (strcmp(a, "-verify") == 0) {
            opt.verify = 1;
        } else if (strcmp(a, "-safe") == 0) {
            opt.safe = 1;
        } else if (strcmp(a, "-rm") == 0) {
            opt.remove_src = 1;
        } else if (strcmp(a, "-index") == 0) {
            opt.stream.add_index = 1;
        } else if (strcmp(a, "-no-index") == 0) {
            opt.stream.add_index = 0;
        } else if (strcmp(a, "-search") == 0) {
            opt.search_enabled = 1;
        } else if (strcmp(a, "-search.len") == 0 &&
                   i + 1 < argc) {
            unsigned n = (unsigned)atoi(argv[++i]);
            if (minlz_search_config_set_match_len(
                    &opt.search_config, n) != MINLZ_OK) {
                fprintf(stderr, "ERROR: invalid search length\n");
                free(files);
                return 1;
            }
        } else if (strcmp(a, "-search.prefixes") == 0 &&
                   i + 1 < argc) {
            opt.search_prefixes_arg = argv[++i];
        } else if (strcmp(a, "-search.prefix") == 0 &&
                   i + 1 < argc) {
            opt.search_prefix_arg = argv[++i];
        } else if (strcmp(a, "-search.extras") == 0 &&
                   i + 1 < argc) {
            int extras = atoi(argv[++i]);
            if (extras < 0 || extras > 15) {
                fprintf(stderr, "ERROR: invalid search extras\n");
                free(files);
                return 1;
            }
            opt.search_extras = extras;
        } else if (strcmp(a, "-search.max") == 0 &&
                   i + 1 < argc) {
            char *end = NULL;
            long max_pop = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0') {
                fprintf(stderr, "ERROR: invalid search population\n");
                free(files);
                return 1;
            }
            opt.search_max = (int)max_pop;
        } else if (strcmp(a, "-search.lim") == 0 &&
                   i + 1 < argc) {
            char *end = NULL;
            long reduced = strtol(argv[++i], &end, 10);
            if (!end || *end != '\0') {
                fprintf(stderr,
                        "ERROR: invalid reduced search population\n");
                free(files);
                return 1;
            }
            opt.search_lim = (int)reduced;
            opt.search_lim_set = 1;
        } else if (strcmp(a, "-search.uncompressed") == 0) {
            minlz_search_config_set_compression(
                &opt.search_config, 0);
            opt.search_uncompressed = 1;
        } else if (strcmp(a, "-search.compress.skip") == 0 &&
                   i + 1 < argc) {
            char *end = NULL;
            double pct = strtod(argv[++i], &end);
            if (!end || *end != '\0' ||
                minlz_search_config_set_compression_skip(
                    &opt.search_config, pct) != MINLZ_OK) {
                fprintf(stderr,
                        "ERROR: invalid search compression skip\n");
                free(files);
                return 1;
            }
            opt.search_compression_skip_set = 1;
        } else if (strcmp(a, "-search.sidecar") == 0) {
            opt.auto_search_sidecar = 1;
        } else if (strcmp(a, "-sidecar") == 0 &&
                   i + 1 < argc) {
            opt.sidecar_name = argv[++i];
            opt.search_enabled = 1;
        } else if (strcmp(a, "-cpu") == 0 && i + 1 < argc) {
            opt.stream.threads = atoi(argv[++i]);
        } else if (strcmp(a, "-help") == 0 || strcmp(a, "--help") == 0) {
            usage(argv[0]);
            free(files);
            return 0;
        } else {
            files[file_count++] = a;
        }
    }

    if (file_count == 0) {
        usage(argv[0]);
        free(files);
        return 1;
    }
    if (opt.search_enabled && opt.search_uncompressed &&
        opt.search_compression_skip_set) {
        fprintf(stderr,
                "ERROR: -search.uncompressed conflicts with "
                "-search.compress.skip\n");
        free(files);
        return 1;
    }
    if (opt.auto_search_sidecar && !opt.search_enabled) {
        fprintf(stderr,
                "ERROR: -search.sidecar requires -search\n");
        free(files);
        return 1;
    }
    if (opt.auto_search_sidecar && opt.sidecar_name) {
        fprintf(stderr,
                "ERROR: use either -search.sidecar or -sidecar FILE\n");
        free(files);
        return 1;
    }
    if (opt.auto_search_sidecar &&
        (opt.block || opt.stdout_mode || opt.verify)) {
        fprintf(stderr,
                "ERROR: -search.sidecar requires stream file output\n");
        free(files);
        return 1;
    }
    if (opt.search_enabled) {
        size_t long_prefix_len =
            opt.search_prefix_arg
                ? strlen(opt.search_prefix_arg)
                : 0u;
        size_t prefixes_len =
            opt.search_prefixes_arg
                ? strlen(opt.search_prefixes_arg)
                : 0u;
        if (opt.block) {
            fprintf(stderr,
                    "ERROR: search tables are only valid for streams\n");
            free(files);
            return 1;
        }
        if (opt.search_max < 1 || opt.search_max > 100 ||
            minlz_search_config_set_max_population(
                &opt.search_config,
                (unsigned)opt.search_max) != MINLZ_OK) {
            fprintf(stderr, "ERROR: invalid search population\n");
            free(files);
            return 1;
        }
        if (opt.search_lim_set &&
            (opt.search_lim < 1 || opt.search_lim > 100 ||
             minlz_search_config_set_max_reduced_population(
                 &opt.search_config,
                 (unsigned)opt.search_lim) != MINLZ_OK)) {
            fprintf(stderr,
                    "ERROR: invalid reduced search population\n");
            free(files);
            return 1;
        }
        if (long_prefix_len != 0u && prefixes_len != 0u) {
            fprintf(stderr,
                    "ERROR: cannot use both -search.prefix and "
                    "-search.prefixes\n");
            free(files);
            return 1;
        }
        if (long_prefix_len != 0u) {
            minlz_status st;
            if (opt.search_extras != 0 ||
                long_prefix_len > 1u) {
                st = minlz_search_config_set_long_prefix(
                    &opt.search_config,
                    (const uint8_t *)opt.search_prefix_arg,
                    long_prefix_len,
                    (unsigned)opt.search_extras);
            } else {
                st = minlz_search_config_set_byte_prefix(
                    &opt.search_config,
                    (const uint8_t *)opt.search_prefix_arg,
                    long_prefix_len);
            }
            if (st != MINLZ_OK) {
                fprintf(stderr, "ERROR: invalid search prefix\n");
                free(files);
                return 1;
            }
        } else if (prefixes_len != 0u) {
            if (opt.search_extras != 0) {
                fprintf(stderr,
                        "ERROR: -search.extras requires "
                        "-search.prefix\n");
                free(files);
                return 1;
            }
            if (minlz_search_config_set_byte_prefix(
                    &opt.search_config,
                    (const uint8_t *)opt.search_prefixes_arg,
                    prefixes_len) != MINLZ_OK) {
                fprintf(stderr, "ERROR: invalid search prefixes\n");
                free(files);
                return 1;
            }
        } else if (opt.search_extras != 0) {
            fprintf(stderr,
                    "ERROR: -search.extras requires "
                    "-search.prefix\n");
            free(files);
            return 1;
        }
        if (opt.search_config.table_type ==
                MINLZ_SEARCH_TABLE_LONG_PREFIX &&
            (unsigned)opt.search_config.match_len +
                    opt.search_config.extras >
                16u) {
            fprintf(stderr,
                    "ERROR: search match length + extras exceeds 16\n");
            free(files);
            return 1;
        }
        opt.stream.search = &opt.search_config;
    }
    if (!explicit_mode && file_count > 0) {
        opt.decompress = has_suffix(files[0], ".mz") || has_suffix(files[0], ".mzb") ||
                         has_suffix(files[0], ".sz") || has_suffix(files[0], ".snappy");
        opt.block = has_suffix(files[0], ".mzb");
    }

    for (i = 0; i < file_count; i++) {
        if (!process_file(files[i], &opt, file_count > 1)) {
            free(files);
            return 2;
        }
    }
    free(files);
    return 0;
}
