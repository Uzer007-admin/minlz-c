/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#ifndef MINLZ_BLOCK_DECODE_REPEAT_H
#define MINLZ_BLOCK_DECODE_REPEAT_H

/*
 * The decoder has already validated the offset and output bounds. Grow a
 * contiguous source prefix of whole periods, then reuse it for the rest of
 * the match. Each copy starts after the source range, so memcpy is safe.
 */
static MINLZ_NOINLINE void minlz_copy_match_repeated(uint8_t *dst,
                                                        size_t offset, size_t length) {
    const uint8_t *pattern = dst - offset;
    size_t available = offset;
    while (available < 16384u && available < length) {
        memcpy(dst, pattern, available);
        dst += available;
        length -= available;
        available *= 2u;
    }
    while (length > available) {
        memcpy(dst, pattern, available);
        dst += available;
        length -= available;
    }
    memcpy(dst, pattern, length);
}

#endif
