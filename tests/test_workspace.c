/* Copyright 2026 MinLZ C contributors. Licensed under Apache-2.0. */
#ifndef _DEFAULT_SOURCE
#define _DEFAULT_SOURCE 1
#endif
#include "minlz_internal.h"

#include <stdio.h>
#if defined(MINLZ_TEST_WRAP_MADVISE)
#include <errno.h>
#include <sys/mman.h>
#endif

#define CHECK(x) do { \
    if (!(x)) { \
        fprintf(stderr, "CHECK failed at %s:%d: %s\n", __FILE__, __LINE__, #x); \
        return 1; \
    } \
} while (0)

static int eligible;

/* Exercise allocator eligibility without linking or executing asm backends. */
int minlz_backend_has_inline_asm_amd64(void) {
    return eligible;
}

#if defined(MINLZ_TEST_WRAP_MADVISE)
static int reject_hint;
static unsigned hint_calls;
int __real_madvise(void *address, size_t length, int advice);
int __wrap_madvise(void *address, size_t length, int advice) {
    hint_calls++;
    if (reject_hint) {
        errno = EINVAL;
        return -1;
    }
    return __real_madvise(address, length, advice);
}
#endif

static int test_ineligible(void) {
    minlz_workspace *ws = NULL;
    uint8_t *buf;
    unsigned i;
    eligible = 0;
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    for (i = 0; i < 12u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
        CHECK(ws->encode_scratch_hint == 0u);
        CHECK(ws->encode_scratch_uses == 0u);
    }
    minlz_workspace_destroy(ws);
    return 0;
}

static int test_reuse(void) {
    minlz_workspace *ws = NULL;
    uint8_t *buf = NULL;
    uintptr_t first;
    unsigned i;
    eligible = 1;
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
    first = (uintptr_t)buf;
    for (i = 0; i < 2u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
        CHECK((uintptr_t)buf == first);
    }
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
    if (ws->encode_scratch_hint == 2u) {
        CHECK(((uintptr_t)buf & ((2u << 20) - 1u)) == 0u);
    }
    first = (uintptr_t)buf;
    buf[0] = 17u;
    buf[589823u] = 29u;
    for (i = 0; i < 12u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
        CHECK((uintptr_t)buf == first);
        CHECK(buf[0] == 17u && buf[589823u] == 29u);
    }
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, SIZE_MAX, &buf) == MINLZ_IO);
    CHECK((uintptr_t)ws->buffers[2] == first && ws->buffer_caps[2] == 589824u);
    CHECK(ws->buffers[2][0] == 17u);
    minlz_workspace_destroy(ws);
    return 0;
}

static int test_buffer_exchange(void) {
    minlz_workspace *ws = NULL;
    minlz_workspace *other = NULL;
    uint8_t *buf = NULL;
    uint8_t *detached = NULL;
    uint8_t *first;
    size_t cap = 0;
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_workspace_create(&other) == MINLZ_OK);
    CHECK(minlz_workspace_prepare_buffer(ws, 2u, 129u, &buf) == MINLZ_OK);
    first = buf;
    buf[0] = 17u;
    buf[128] = 19u;
    CHECK(minlz_workspace_prepare_buffer(other, 1u, 257u, &buf) == MINLZ_OK);
    buf[0] = 23u;
    buf[256] = 29u;
    CHECK(minlz_workspace_exchange_buffer(other, 1u, &detached, &cap) == MINLZ_OK);
    CHECK(detached == buf && cap == 257u);
    CHECK(other->buffers[1] == NULL && other->buffer_caps[1] == 0u);
    ws->encode_scratch_uses = 3u;
    ws->encode_scratch_hint = 2u;
    CHECK(minlz_workspace_exchange_buffer(ws, 2u, &detached, &cap) == MINLZ_OK);
    CHECK(ws->buffers[2] == buf && ws->buffer_caps[2] == 257u);
    CHECK(ws->buffers[2][0] == 23u && ws->buffers[2][256] == 29u);
    CHECK(detached == first && cap == 129u && detached[128] == 19u);
    CHECK(((uintptr_t)detached & 63u) == 0u && ((uintptr_t)buf & 63u) == 0u);
    CHECK(ws->encode_scratch_uses == 0u && ws->encode_scratch_hint == 0u);
    CHECK(minlz_workspace_exchange_buffer(NULL, 2u, &detached, &cap) == MINLZ_CORRUPT);
    CHECK(minlz_workspace_exchange_buffer(ws, 3u, &detached, &cap) == MINLZ_CORRUPT);
    CHECK(minlz_workspace_exchange_buffer(ws, 2u, NULL, &cap) == MINLZ_CORRUPT);
    CHECK(minlz_workspace_exchange_buffer(ws, 2u, &detached, NULL) == MINLZ_CORRUPT);
    CHECK(detached == first && cap == 129u && ws->buffers[2] == buf);
    CHECK(minlz_workspace_prepare_buffer(other, 1u, 64u, &buf) == MINLZ_OK);
    CHECK(buf != detached && buf != ws->buffers[2]);
    minlz_workspace_release_buffer(detached);
    minlz_workspace_release_buffer(NULL);
    minlz_workspace_destroy(other);
    minlz_workspace_destroy(ws);
    return 0;
}

#if defined(MINLZ_TEST_WRAP_MADVISE) && MINLZ_ENABLE_HUGEPAGE_WORKSPACE && defined(MADV_HUGEPAGE)
static int test_rejected_hint(void) {
    minlz_workspace *ws = NULL;
    uint8_t *buf = NULL;
    uintptr_t first;
    unsigned i;
    eligible = 1;
    reject_hint = 1;
    hint_calls = 0u;
    CHECK(minlz_workspace_create(&ws) == MINLZ_OK);
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
    first = (uintptr_t)buf;
    buf[0] = 41u;
    buf[589823u] = 43u;
    for (i = 0; i < 12u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
        CHECK((uintptr_t)buf == first);
        CHECK(buf[0] == 41u && buf[589823u] == 43u);
    }
    CHECK(hint_calls == 1u && ws->encode_scratch_hint == 1u);
    minlz_workspace_reset(ws);
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589824u, &buf) == MINLZ_OK);
    CHECK(hint_calls == 1u && (uintptr_t)buf == first);
    CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589825u, &buf) == MINLZ_OK);
    CHECK(ws->encode_scratch_hint == 0u && ws->encode_scratch_uses == 1u);
    CHECK(buf[0] == 0u && buf[589824u] == 0u);
    for (i = 0; i < 3u; i++) {
        CHECK(minlz_workspace_prepare_encode_scratch(ws, 2u, 589825u, &buf) == MINLZ_OK);
    }
    CHECK(hint_calls == 2u && ws->encode_scratch_hint == 1u);
    minlz_workspace_destroy(ws);
    reject_hint = 0;
    return 0;
}
#endif

int main(void) {
    CHECK(test_ineligible() == 0);
    CHECK(test_reuse() == 0);
    CHECK(test_buffer_exchange() == 0);
#if defined(MINLZ_TEST_WRAP_MADVISE) && MINLZ_ENABLE_HUGEPAGE_WORKSPACE && defined(MADV_HUGEPAGE)
    CHECK(test_rejected_hint() == 0);
#endif
    puts("workspace tests passed");
    return 0;
}
