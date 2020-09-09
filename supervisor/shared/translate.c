/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2018 Scott Shawcroft for Adafruit Industries
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "supervisor/shared/translate.h"

#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#ifndef NO_QSTR
#include "genhdr/compression.generated.h"
#endif

#include "py/misc.h"
#include "supervisor/serial.h"

void serial_write_compressed(const compressed_string_t* compressed) {
    char decompressed[decompress_length(compressed)];
    decompress(compressed, decompressed);
    serial_write(decompressed);
}

STATIC int put_utf8(char *buf, int u) {
    if(u <= 0x7f) {
        *buf = u;
        return 1;
    } else if(bigram_start <= u && u <= bigram_end) {
        int n = (u - 0x80) * 2;
        // (note that at present, entries in the bigrams table are
        // guaranteed not to represent bigrams themselves, so this adds
        // at most 1 level of recursive call
        int ret = put_utf8(buf, bigrams[n]);
        return ret + put_utf8(buf + ret, bigrams[n+1]);
    } else if(u <= 0x07ff) {
        *buf++ = 0b11000000 | (u >> 6);
        *buf   = 0b10000000 | (u & 0b00111111);
        return 2;
    } else { // u <= 0xffff
        *buf++ = 0b11100000 | (u >> 12);
        *buf++ = 0b10000000 | ((u >> 6) & 0b00111111);
        *buf   = 0b10000000 | (u & 0b00111111);
        return 3;
    }
}

uint16_t decompress_length(const compressed_string_t* compressed) {
    if (compress_max_length_bits <= 8) {
        return 1 + (compressed->data >> (8 - compress_max_length_bits));
    } else {
        return 1 + ((compressed->data * 256 + compressed->tail[0]) >> (16 - compress_max_length_bits));
    }
}

char* decompress(const compressed_string_t* compressed, char* decompressed) {
    uint8_t this_byte = compress_max_length_bits / 8;
    uint8_t this_bit = 7 - compress_max_length_bits % 8;
    uint8_t b = (&compressed->data)[this_byte];
    uint16_t length = decompress_length(compressed);

    // Stop one early because the last byte is always NULL.
    for (uint16_t i = 0; i < length - 1;) {
        uint32_t bits = 0;
        uint8_t bit_length = 0;
        uint32_t max_code = lengths[0];
        uint32_t searched_length = lengths[0];
        while (true) {
            bits <<= 1;
            if ((0x80 & b) != 0) {
                bits |= 1;
            }
            b <<= 1;
            bit_length += 1;
            if (this_bit == 0) {
                this_bit = 7;
                this_byte += 1;
                b = (&compressed->data)[this_byte]; // This may read past the end but its never used.
            } else {
                this_bit -= 1;
            }
            if (max_code > 0 && bits < max_code) {
                break;
            }
            max_code = (max_code << 1) + lengths[bit_length];
            searched_length += lengths[bit_length];
        }
        i += put_utf8(decompressed + i, values[searched_length + bits - max_code]);
    }

    decompressed[length-1] = '\0';
    return decompressed;
}

inline __attribute__((always_inline)) const compressed_string_t* translate(const char* original) {
    #ifndef NO_QSTR
    #define QDEF(id, str)
    #define TRANSLATION(id, firstbyte, ...) if (strcmp(original, id) == 0) { static const compressed_string_t v = { .data = firstbyte, .tail = { __VA_ARGS__ } }; return &v; } else
    #include "genhdr/qstrdefs.generated.h"
    #undef TRANSLATION
    #undef QDEF
    #endif
    return NULL;
}
