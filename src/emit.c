/*
 * Copyright 2026 MinLZ C contributors.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 */
#include "minlz_internal.h"

static size_t minlz_emit_copy2(uint8_t *dst, size_t dst_cap, size_t offset, size_t length) {
    if (offset < MINLZ_MIN_COPY2_OFFSET || offset > MINLZ_MAX_COPY2_OFFSET || length < 4) {
        return 0;
    }
    offset -= MINLZ_MIN_COPY2_OFFSET;
    length -= 4;
    if (length <= 60) {
        if (dst_cap < 3) {
            return 0;
        }
        dst[0] = (uint8_t)((length << 2) | MINLZ_TAG_COPY2);
        minlz_store16(dst + 1, (uint16_t)offset);
        return 3;
    }
    length -= 60;
    if (length < 256) {
        if (dst_cap < 4) {
            return 0;
        }
        dst[0] = (uint8_t)((61u << 2) | MINLZ_TAG_COPY2);
        minlz_store16(dst + 1, (uint16_t)offset);
        dst[3] = (uint8_t)length;
        return 4;
    }
    if (length < 65536) {
        if (dst_cap < 5) {
            return 0;
        }
        dst[0] = (uint8_t)((62u << 2) | MINLZ_TAG_COPY2);
        minlz_store16(dst + 1, (uint16_t)offset);
        minlz_store16(dst + 3, (uint16_t)length);
        return 5;
    }
    if (length >= (1u << 24)) {
        return 0;
    }
    if (dst_cap < 6) {
        return 0;
    }
    dst[0] = (uint8_t)((63u << 2) | MINLZ_TAG_COPY2);
    minlz_store16(dst + 1, (uint16_t)offset);
    dst[3] = (uint8_t)length;
    dst[4] = (uint8_t)(length >> 8);
    dst[5] = (uint8_t)(length >> 16);
    return 6;
}

static size_t minlz_emit_copy3(uint8_t *dst, size_t dst_cap, size_t offset,
                               size_t length, size_t lit_len) {
    uint32_t encoded;
    if (offset < MINLZ_MIN_COPY3_OFFSET || offset > MINLZ_MAX_COPY3_OFFSET ||
        length < 4 || lit_len > 3) {
        return 0;
    }
    length -= 4;
    offset -= MINLZ_MIN_COPY3_OFFSET;
    encoded = ((uint32_t)offset << 11) | MINLZ_TAG_COPY3 | ((uint32_t)lit_len << 3);
    if (length <= 60) {
        if (dst_cap < 4) {
            return 0;
        }
        encoded |= (uint32_t)length << 5;
        minlz_store32(dst, encoded);
        return 4;
    }
    length -= 60;
    if (length < 256) {
        if (dst_cap < 5) {
            return 0;
        }
        encoded |= 61u << 5;
        minlz_store32(dst, encoded);
        dst[4] = (uint8_t)length;
        return 5;
    }
    if (length < 65536) {
        if (dst_cap < 6) {
            return 0;
        }
        encoded |= 62u << 5;
        minlz_store32(dst, encoded);
        minlz_store16(dst + 4, (uint16_t)length);
        return 6;
    }
    if (length >= (1u << 24)) {
        return 0;
    }
    if (dst_cap < 7) {
        return 0;
    }
    encoded |= 63u << 5;
    minlz_store32(dst, encoded);
    dst[4] = (uint8_t)length;
    dst[5] = (uint8_t)(length >> 8);
    dst[6] = (uint8_t)(length >> 16);
    return 7;
}

size_t minlz_emit_literal(uint8_t *dst, size_t dst_cap, const uint8_t *lit, size_t lit_len) {
    size_t i;
    size_t n;
    if (lit_len == 0) {
        return 0;
    }
    n = lit_len - 1;
    if (n < 29) {
        if (dst_cap < 1 + lit_len) {
            return 0;
        }
        dst[0] = (uint8_t)(n << 3);
        i = 1;
    } else if (n < 29 + (1u << 8)) {
        if (dst_cap < 2 + lit_len) {
            return 0;
        }
        dst[0] = (uint8_t)(29u << 3);
        dst[1] = (uint8_t)(n - 29);
        i = 2;
    } else if (n < 29 + (1u << 16)) {
        n -= 29;
        if (dst_cap < 3 + lit_len) {
            return 0;
        }
        dst[0] = (uint8_t)(30u << 3);
        minlz_store16(dst + 1, (uint16_t)n);
        i = 3;
    } else if (n < 29 + (1u << 24)) {
        n -= 29;
        if (dst_cap < 4 + lit_len) {
            return 0;
        }
        dst[0] = (uint8_t)(31u << 3);
        dst[1] = (uint8_t)n;
        dst[2] = (uint8_t)(n >> 8);
        dst[3] = (uint8_t)(n >> 16);
        i = 4;
    } else {
        return 0;
    }
    memcpy(dst + i, lit, lit_len);
    return i + lit_len;
}

size_t minlz_emit_repeat(uint8_t *dst, size_t dst_cap, size_t length) {
    if (length == 0) {
        return 0;
    }
    if (length < 30) {
        if (dst_cap < 1) {
            return 0;
        }
        dst[0] = (uint8_t)(((length - 1) << 3) | MINLZ_TAG_REPEAT);
        return 1;
    }
    length -= 30;
    if (length < 256) {
        if (dst_cap < 2) {
            return 0;
        }
        dst[0] = (uint8_t)((29u << 3) | MINLZ_TAG_REPEAT);
        dst[1] = (uint8_t)length;
        return 2;
    }
    if (length < 65536) {
        if (dst_cap < 3) {
            return 0;
        }
        dst[0] = (uint8_t)((30u << 3) | MINLZ_TAG_REPEAT);
        minlz_store16(dst + 1, (uint16_t)length);
        return 3;
    }
    if (length >= (1u << 24)) {
        return 0;
    }
    if (dst_cap < 4) {
        return 0;
    }
    dst[0] = (uint8_t)((31u << 3) | MINLZ_TAG_REPEAT);
    dst[1] = (uint8_t)length;
    dst[2] = (uint8_t)(length >> 8);
    dst[3] = (uint8_t)(length >> 16);
    return 4;
}

size_t minlz_emit_copy(uint8_t *dst, size_t dst_cap, size_t offset, size_t length) {
    if (offset == 0 || offset > MINLZ_MAX_COPY3_OFFSET || length < 4) {
        return 0;
    }
    if (offset > MINLZ_MAX_COPY2_OFFSET) {
        return minlz_emit_copy3(dst, dst_cap, offset, length, 0);
    }
    if (offset <= MINLZ_MAX_COPY1_OFFSET) {
        uint16_t enc;
        offset--;
        if (length < 19) {
            if (dst_cap < 2) {
                return 0;
            }
            enc = (uint16_t)((offset << 6) | ((length - 4) << 2) | MINLZ_TAG_COPY1);
            minlz_store16(dst, enc);
            return 2;
        }
        if (length < 274) {
            if (dst_cap < 3) {
                return 0;
            }
            enc = (uint16_t)((offset << 6) | (15u << 2) | MINLZ_TAG_COPY1);
            minlz_store16(dst, enc);
            dst[2] = (uint8_t)(length - 18);
            return 3;
        }
        if (dst_cap < 2) {
            return 0;
        }
        enc = (uint16_t)((offset << 6) | (14u << 2) | MINLZ_TAG_COPY1);
        minlz_store16(dst, enc);
        {
            size_t n = minlz_emit_repeat(dst + 2, dst_cap - 2, length - 18);
            return n ? 2 + n : 0;
        }
    }
    return minlz_emit_copy2(dst, dst_cap, offset, length);
}

size_t minlz_emit_copy_lits2(uint8_t *dst, size_t dst_cap, const uint8_t *lits,
                             size_t lit_len, size_t offset, size_t length) {
    size_t raw_len;
    if (lit_len == 0 || lit_len > 4 || offset < MINLZ_MIN_COPY2_OFFSET ||
        offset > MINLZ_MAX_COPY2_OFFSET || length < 4) {
        return 0;
    }
    raw_len = length - 4;
    offset -= MINLZ_MIN_COPY2_OFFSET;
    if (raw_len > 7) {
        size_t n;
        if (dst_cap < 3 + lit_len) {
            return 0;
        }
        dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (7u << 5) | ((lit_len - 1) << 3));
        minlz_store16(dst + 1, (uint16_t)offset);
        memcpy(dst + 3, lits, lit_len);
        n = minlz_emit_repeat(dst + 3 + lit_len, dst_cap - 3 - lit_len, raw_len - 7);
        return n ? 3 + lit_len + n : 0;
    }
    if (dst_cap < 3 + lit_len) {
        return 0;
    }
    dst[0] = (uint8_t)(MINLZ_TAG_COPY2_FUSED | (raw_len << 5) | ((lit_len - 1) << 3));
    minlz_store16(dst + 1, (uint16_t)offset);
    memcpy(dst + 3, lits, lit_len);
    return 3 + lit_len;
}

size_t minlz_emit_copy_lits3(uint8_t *dst, size_t dst_cap, const uint8_t *lits,
                             size_t lit_len, size_t offset, size_t length) {
    size_t n;
    if (lit_len > 3) {
        return 0;
    }
    n = minlz_emit_copy3(dst, dst_cap, offset, length, lit_len);
    if (n == 0 || dst_cap - n < lit_len) {
        return 0;
    }
    memcpy(dst + n, lits, lit_len);
    return n + lit_len;
}
