/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#if defined(__linux__) && defined(MINLZ_ENABLE_HUGEPAGE_WORKSPACE) && MINLZ_ENABLE_HUGEPAGE_WORKSPACE
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#endif
#include "minlz_internal.h"

#include <stdint.h>
#if defined(__linux__) && defined(MINLZ_ENABLE_HUGEPAGE_WORKSPACE) && MINLZ_ENABLE_HUGEPAGE_WORKSPACE
#include <sys/mman.h>
#endif

#define MINLZ_WORKSPACE_ALIGN 64u

static void *minlz_aligned_alloc_zero(size_t size) {
    uintptr_t raw_addr;
    uintptr_t aligned_addr;
    void *raw;
    void *aligned;
    if (size == 0) {
        size = 1;
    }
    if (size > SIZE_MAX - (MINLZ_WORKSPACE_ALIGN - 1u) - sizeof(void *)) {
        return NULL;
    }
    raw = malloc(size + MINLZ_WORKSPACE_ALIGN - 1u + sizeof(void *));
    if (!raw) {
        return NULL;
    }
    raw_addr = (uintptr_t)raw + sizeof(void *);
    aligned_addr = (raw_addr + MINLZ_WORKSPACE_ALIGN - 1u) & ~(uintptr_t)(MINLZ_WORKSPACE_ALIGN - 1u);
    aligned = (void *)aligned_addr;
    ((void **)aligned)[-1] = raw;
    memset(aligned, 0, size);
    return aligned;
}

#if defined(__linux__) && defined(MINLZ_ENABLE_HUGEPAGE_WORKSPACE) && MINLZ_ENABLE_HUGEPAGE_WORKSPACE && defined(MADV_HUGEPAGE)
static void *minlz_largepage_alloc_zero(size_t size) {
    const size_t alignment = 2u << 20;
    size_t span;
    void *raw, *aligned;
    uintptr_t address;
    if (size > SIZE_MAX - (alignment - 1u)) {
        return NULL;
    }
    span = (size + alignment - 1u) & ~(alignment - 1u);
    if (span > SIZE_MAX - (alignment - 1u) - sizeof(void *)) {
        return NULL;
    }
    raw = malloc(span + alignment - 1u + sizeof(void *));
    if (!raw) {
        return NULL;
    }
    address = ((uintptr_t)raw + sizeof(void *) + alignment - 1u) &
              ~(uintptr_t)(alignment - 1u);
    aligned = (void *)address;
    if (madvise(aligned, span, MADV_HUGEPAGE) != 0) {
        free(raw);
        return NULL;
    }
    ((void **)aligned)[-1] = raw;
    memset(aligned, 0, size);
    return aligned;
}
#endif

static void minlz_aligned_free(void *p) {
    if (p) {
        free(((void **)p)[-1]);
    }
}

void minlz_workspace_release_buffer(uint8_t *buf) {
    minlz_aligned_free(buf);
}

minlz_status minlz_workspace_create(minlz_workspace **ws) {
    minlz_workspace *p;
    if (!ws) {
        return MINLZ_CORRUPT;
    }
    *ws = NULL;
    p = (minlz_workspace *)calloc(1, sizeof(*p));
    if (!p) {
        return MINLZ_IO;
    }
    *ws = p;
    return MINLZ_OK;
}

void minlz_workspace_cleanup(minlz_workspace *ws) {
    size_t i;
    if (!ws) {
        return;
    }
    for (i = 0; i < sizeof(ws->tables) / sizeof(ws->tables[0]); i++) {
        minlz_aligned_free(ws->tables[i].pos);
        minlz_aligned_free(ws->tables[i].stamp);
        ws->tables[i].pos = NULL;
        ws->tables[i].stamp = NULL;
        ws->tables[i].cap = 0;
        ws->tables[i].generation = 0;
    }
    for (i = 0; i < sizeof(ws->buffers) / sizeof(ws->buffers[0]); i++) {
        minlz_aligned_free(ws->buffers[i]);
        ws->buffers[i] = NULL;
        ws->buffer_caps[i] = 0;
    }
    ws->encode_scratch_uses = 0;
    ws->encode_scratch_hint = 0;
}

void minlz_workspace_destroy(minlz_workspace *ws) {
    if (!ws) {
        return;
    }
    minlz_workspace_cleanup(ws);
    free(ws);
}

void minlz_workspace_reset(minlz_workspace *ws) {
    size_t i;
    if (!ws) {
        return;
    }
    for (i = 0; i < sizeof(ws->tables) / sizeof(ws->tables[0]); i++) {
        if (ws->tables[i].stamp && ws->tables[i].cap) {
            memset(ws->tables[i].stamp, 0, ws->tables[i].cap * sizeof(ws->tables[i].stamp[0]));
        }
        ws->tables[i].generation = 0;
    }
}

minlz_status minlz_workspace_prepare_table(minlz_workspace *ws, unsigned slot,
                                           size_t entries, uint32_t **pos,
                                           uint32_t **stamp, uint32_t *generation) {
    minlz_workspace_table *t;
    if (!ws || slot >= sizeof(ws->tables) / sizeof(ws->tables[0]) ||
        !pos || !stamp || !generation || entries == 0) {
        return MINLZ_CORRUPT;
    }
    t = &ws->tables[slot];
    if (t->cap < entries) {
        uint32_t *new_pos = (uint32_t *)minlz_aligned_alloc_zero(entries * sizeof(uint32_t));
        uint32_t *new_stamp = (uint32_t *)minlz_aligned_alloc_zero(entries * sizeof(uint32_t));
        if (!new_pos || !new_stamp) {
            minlz_aligned_free(new_pos);
            minlz_aligned_free(new_stamp);
            return MINLZ_IO;
        }
        minlz_aligned_free(t->pos);
        minlz_aligned_free(t->stamp);
        t->pos = new_pos;
        t->stamp = new_stamp;
        t->cap = entries;
        t->generation = 0;
    }
    t->generation++;
    if (t->generation == 0) {
        memset(t->stamp, 0, t->cap * sizeof(t->stamp[0]));
        t->generation = 1;
    }
    *pos = t->pos;
    *stamp = t->stamp;
    *generation = t->generation;
    return MINLZ_OK;
}

minlz_status minlz_workspace_prepare_buffer(minlz_workspace *ws, unsigned slot,
                                            size_t cap, uint8_t **buf) {
    if (!ws || slot >= sizeof(ws->buffers) / sizeof(ws->buffers[0]) || !buf) {
        return MINLZ_CORRUPT;
    }
    if (cap == 0) {
        cap = 1;
    }
    if (ws->buffer_caps[slot] < cap) {
        uint8_t *next = (uint8_t *)minlz_aligned_alloc_zero(cap);
        if (!next) {
            return MINLZ_IO;
        }
        minlz_aligned_free(ws->buffers[slot]);
        ws->buffers[slot] = next;
        ws->buffer_caps[slot] = cap;
        if (slot == 2u) {
            ws->encode_scratch_uses = 0;
            ws->encode_scratch_hint = 0;
        }
    }
    *buf = ws->buffers[slot];
    return MINLZ_OK;
}

minlz_status minlz_workspace_prepare_encode_scratch(minlz_workspace *ws, unsigned slot,
                                                    size_t cap, uint8_t **buf) {
    minlz_status st = minlz_workspace_prepare_buffer(ws, slot, cap, buf);
    if (st != MINLZ_OK) {
        return st;
    }
#if defined(__linux__) && defined(MINLZ_ENABLE_HUGEPAGE_WORKSPACE) && MINLZ_ENABLE_HUGEPAGE_WORKSPACE && defined(MADV_HUGEPAGE)
    if (slot == 2u && cap >= (512u << 10) && ws->encode_scratch_hint == 0u &&
        minlz_backend_has_inline_asm_amd64()) {
        /* Keep cold one-shot calls unchanged; only promote reused hash scratch. */
        if (++ws->encode_scratch_uses >= 4u) {
            uint8_t *next = (uint8_t *)minlz_largepage_alloc_zero(ws->buffer_caps[slot]);
            ws->encode_scratch_hint = 1u;
            if (next) {
                minlz_aligned_free(ws->buffers[slot]);
                ws->buffers[slot] = next;
                ws->encode_scratch_hint = 2u;
                *buf = next;
            }
        }
    }
#endif
    return MINLZ_OK;
}
