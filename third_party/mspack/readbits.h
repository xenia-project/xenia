/* This file is part of libmspack.
 * (C) 2003-2010 Stuart Caie.
 *
 * libmspack is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (LGPL) version 2.1
 *
 * For further details, see the file COPYING.LIB distributed with libmspack
 */

#ifndef MSPACK_READBITS_H
#define MSPACK_READBITS_H 1

/* this header defines macros that read data streams by
 * the individual bits
 *
 * INIT_BITS         initialises bitstream state in state structure
 * STORE_BITS        stores bitstream state in state structure
 * RESTORE_BITS      restores bitstream state from state structure
 * ENSURE_BITS(n)    ensure there are at least N bits in the bit buffer
 * READ_BITS(var,n)  takes N bits from the buffer and puts them in var
 * PEEK_BITS(n)      extracts without removing N bits from the bit buffer
 * REMOVE_BITS(n)    removes N bits from the bit buffer
 *
 * READ_BITS simply calls ENSURE_BITS, PEEK_BITS and REMOVE_BITS,
 * which means it's limited to reading the number of bits you can
 * ensure at any one time. It also fails if asked to read zero bits.
 * If you need to read zero bits, or more bits than can be ensured in
 * one go, use READ_MANY_BITS instead.
 *
 * These macros have variable names baked into them, so to use them
 * you have to define some macros:
 * - BITS_TYPE: the type name of your state structure
 * - BITS_VAR: the variable that points to your state structure
 * - define BITS_ORDER_MSB if bits are read from the MSB, or
 *   define BITS_ORDER_LSB if bits are read from the LSB
 * - READ_BYTES: some code that reads more data into the bit buffer,
 *   it should use READ_IF_NEEDED (calls read_input if the byte buffer
 *   is empty), then INJECT_BITS(data,n) to put data from the byte
 *   buffer into the bit buffer.
 *
 * You also need to define some variables and structure members:
 * - unsigned char *i_ptr;    // current position in the byte buffer
 * - unsigned char *i_end;    // end of the byte buffer
 * - unsigned int bit_buffer; // the bit buffer itself
 * - unsigned int bits_left;  // number of bits remaining
 *
 * If you use read_input() and READ_IF_NEEDED, they also expect these
 * structure members:
 * - struct mspack_system *sys;  // to access sys->read()
 * - unsigned int error;         // to record/return read errors
 * - unsigned char input_end;    // to mark reaching the EOF
 * - unsigned char *inbuf;       // the input byte buffer
 * - unsigned int inbuf_size;    // the size of the input byte buffer
 *
 * Your READ_BYTES implementation should read data from *i_ptr and
 * put them in the bit buffer. READ_IF_NEEDED will call read_input()
 * if i_ptr reaches i_end, and will fill up inbuf and set i_ptr to
 * the start of inbuf and i_end to the end of inbuf.
 *
 * If you're reading in MSB order, the routines work by using the area
 * beyond the MSB and the LSB of the bit buffer as a free source of
 * zeroes when shifting. This avoids having to mask any bits. So we
 * have to know the bit width of the bit buffer variable. We use
 * <limits.h> and CHAR_BIT to find the size of the bit buffer in bits.
 *
 * If you are reading in LSB order, bits need to be masked. Normally
 * this is done by computing the mask: N bits are masked by the value
 * (1<<N)-1). However, you can define BITS_LSB_TABLE to use a lookup
 * table instead of computing this. This adds two new macros,
 * PEEK_BITS_T and READ_BITS_T which work the same way as PEEK_BITS
 * and READ_BITS, except they use this lookup table. This is useful if
 * you need to look up a number of bits that are only known at
 * runtime, so the bit mask can't be turned into a constant by the
 * compiler.

 * The bit buffer datatype should be at least 32 bits wide: it must be
 * possible to ENSURE_BITS(17), so it must be possible to add 16 new bits
 * to the bit buffer when the bit buffer already has 1 to 15 bits left.
 */

#ifndef BITS_VAR
# error "define BITS_VAR as the state structure poiner variable name"
#endif
#ifndef BITS_TYPE
# error "define BITS_TYPE as the state structure type"
#endif
#if defined(BITS_ORDER_MSB) && defined(BITS_ORDER_LSB)
# error "you must define either BITS_ORDER_MSB or BITS_ORDER_LSB"
#else
# if !(defined(BITS_ORDER_MSB) || defined(BITS_ORDER_LSB))
#  error "you must define BITS_ORDER_MSB or BITS_ORDER_LSB"
# endif
#endif

#if HAVE_LIMITS_H
# include <limits.h>
#endif
#ifndef CHAR_BIT
# define CHAR_BIT (8)
#endif
#define BITBUF_WIDTH (sizeof(bit_buffer) * CHAR_BIT)

#define INIT_BITS do {                          \
    BITS_VAR->i_ptr      = &BITS_VAR->inbuf[0]; \
    BITS_VAR->i_end      = &BITS_VAR->inbuf[0]; \
    BITS_VAR->bit_buffer = 0;                   \
    BITS_VAR->bits_left  = 0;                   \
    BITS_VAR->input_end  = 0;                   \
} while (0)

#define STORE_BITS do {                 \
    BITS_VAR->i_ptr      = i_ptr;       \
    BITS_VAR->i_end      = i_end;       \
    BITS_VAR->bit_buffer = bit_buffer;  \
    BITS_VAR->bits_left  = bits_left;   \
} while (0)

#define RESTORE_BITS do {               \
    i_ptr      = BITS_VAR->i_ptr;       \
    i_end      = BITS_VAR->i_end;       \
    bit_buffer = BITS_VAR->bit_buffer;  \
    bits_left  = BITS_VAR->bits_left;   \
} while (0)

#define ENSURE_BITS(nbits) do {                 \
    while (bits_left < (nbits)) READ_BYTES;     \
} while (0)

#define READ_BITS(val, nbits) do {              \
    ENSURE_BITS(nbits);                         \
    (val) = PEEK_BITS(nbits);                   \
    REMOVE_BITS(nbits);                         \
} while (0)

#define READ_MANY_BITS(val, bits) do {                          \
    unsigned char needed = (bits), bitrun;                      \
    (val) = 0;                                                  \
    while (needed > 0) {                                        \
        if (bits_left <= (BITBUF_WIDTH - 16)) READ_BYTES;       \
        bitrun = (bits_left < needed) ? bits_left : needed;     \
        (val) = ((val) << bitrun) | PEEK_BITS(bitrun);          \
        REMOVE_BITS(bitrun);                                    \
        needed -= bitrun;                                       \
    }                                                           \
} while (0)

#ifdef BITS_ORDER_MSB
# define PEEK_BITS(nbits)   (bit_buffer >> (BITBUF_WIDTH - (nbits)))
# define REMOVE_BITS(nbits) ((bit_buffer <<= (nbits)), (bits_left -= (nbits)))
# define INJECT_BITS(bitdata,nbits) ((bit_buffer |= \
    (bitdata) << (BITBUF_WIDTH - (nbits) - bits_left)), (bits_left += (nbits)))
#else /* BITS_ORDER_LSB */
# define PEEK_BITS(nbits)   (bit_buffer & ((1 << (nbits))-1))
# define REMOVE_BITS(nbits) ((bit_buffer >>= (nbits)), (bits_left -= (nbits)))
# define INJECT_BITS(bitdata,nbits) ((bit_buffer |= \
    (bitdata) << bits_left), (bits_left += (nbits)))
#endif

#ifdef BITS_LSB_TABLE
/* lsb_bit_mask[n] = (1 << n) - 1 */
static const unsigned short lsb_bit_mask[17] = {
    0x0000, 0x0001, 0x0003, 0x0007, 0x000f, 0x001f, 0x003f, 0x007f, 0x00ff,
    0x01ff, 0x03ff, 0x07ff, 0x0fff, 0x1fff, 0x3fff, 0x7fff, 0xffff
};
# define PEEK_BITS_T(nbits) (bit_buffer & lsb_bit_mask[(nbits)])
# define READ_BITS_T(val, nbits) do {   \
    ENSURE_BITS(nbits);                 \
    (val) = PEEK_BITS_T(nbits);         \
    REMOVE_BITS(nbits);                 \
} while (0)
#endif

#ifndef BITS_NO_READ_INPUT
# define READ_IF_NEEDED do {            \
    if (i_ptr >= i_end) {               \
        if (read_input(BITS_VAR))       \
            return BITS_VAR->error;     \
        i_ptr = BITS_VAR->i_ptr;        \
        i_end = BITS_VAR->i_end;        \
    }                                   \
} while (0)

static int read_input(BITS_TYPE *p) {
    int read = p->sys->read(p->input, &p->inbuf[0], (int)p->inbuf_size);
    if (read < 0) return p->error = MSPACK_ERR_READ;

    /* we might overrun the input stream by asking for bits we don't use,
     * so fake 2 more bytes at the end of input */
    if (read == 0) {
        if (p->input_end) {
            D(("out of input bytes"))
            return p->error = MSPACK_ERR_READ;
        }
        else {
            read = 2;
            p->inbuf[0] = p->inbuf[1] = 0;
            p->input_end = 1;
        }
    }

    /* update i_ptr and i_end */
    p->i_ptr = &p->inbuf[0];
    p->i_end = &p->inbuf[read];
    return MSPACK_ERR_OK;
}
#endif
#endif
