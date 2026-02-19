/* This file is part of libmspack.
 * (C) 2003-2014 Stuart Caie.
 *
 * libmspack is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License (LGPL) version 2.1
 *
 * For further details, see the file COPYING.LIB distributed with libmspack
 */

#ifndef MSPACK_READHUFF_H
#define MSPACK_READHUFF_H 1

/* This implements a fast Huffman tree decoding system. */

#if !(defined(BITS_ORDER_MSB) || defined(BITS_ORDER_LSB))
# error "readhuff.h is used in conjunction with readbits.h, include that first"
#endif
#if !(defined(TABLEBITS) && defined(MAXSYMBOLS))
# error "define TABLEBITS(tbl) and MAXSYMBOLS(tbl) before using readhuff.h"
#endif
#if !(defined(HUFF_TABLE) && defined(HUFF_LEN))
# error "define HUFF_TABLE(tbl) and HUFF_LEN(tbl) before using readhuff.h"
#endif
#ifndef HUFF_ERROR
# error "define HUFF_ERROR before using readhuff.h"
#endif
#ifndef HUFF_MAXBITS
# define HUFF_MAXBITS 16
#endif

/* Decodes the next huffman symbol from the input bitstream into var.
 * Do not use this macro on a table unless build_decode_table() succeeded.
 */
#define READ_HUFFSYM(tbl, var) do {                     \
    ENSURE_BITS(HUFF_MAXBITS);                          \
    sym = HUFF_TABLE(tbl, PEEK_BITS(TABLEBITS(tbl)));   \
    if (sym >= MAXSYMBOLS(tbl)) HUFF_TRAVERSE(tbl);     \
    (var) = sym;                                        \
    i = HUFF_LEN(tbl, sym);                             \
    REMOVE_BITS(i);                                     \
} while (0)

#ifdef BITS_ORDER_LSB
# define HUFF_TRAVERSE(tbl) do {                        \
    i = TABLEBITS(tbl) - 1;                             \
    do {                                                \
        if (i++ > HUFF_MAXBITS) HUFF_ERROR;             \
        sym = HUFF_TABLE(tbl,                           \
            (sym << 1) | ((bit_buffer >> i) & 1));      \
    } while (sym >= MAXSYMBOLS(tbl));                   \
} while (0)
#else
#define HUFF_TRAVERSE(tbl) do {                         \
    i = 1 << (BITBUF_WIDTH - TABLEBITS(tbl));           \
    do {                                                \
        if ((i >>= 1) == 0) HUFF_ERROR;                 \
        sym = HUFF_TABLE(tbl,                           \
            (sym << 1) | ((bit_buffer & i) ? 1 : 0));   \
    } while (sym >= MAXSYMBOLS(tbl));                   \
} while (0)
#endif

/* make_decode_table(nsyms, nbits, length[], table[])
 *
 * This function was originally coded by David Tritscher.
 * It builds a fast huffman decoding table from
 * a canonical huffman code lengths table.
 *
 * nsyms  = total number of symbols in this huffman tree.
 * nbits  = any symbols with a code length of nbits or less can be decoded
 *          in one lookup of the table.
 * length = A table to get code lengths from [0 to nsyms-1]
 * table  = The table to fill up with decoded symbols and pointers.
 *          Should be ((1<<nbits) + (nsyms*2)) in length.
 *
 * Returns 0 for OK or 1 for error
 */
static int make_decode_table(unsigned int nsyms, unsigned int nbits,
                             unsigned char *length, unsigned short *table)
{
    register unsigned short sym, next_symbol;
    register unsigned int leaf, fill;
#ifdef BITS_ORDER_LSB
    register unsigned int reverse;
#endif
    register unsigned char bit_num;
    unsigned int pos         = 0; /* the current position in the decode table */
    unsigned int table_mask  = 1 << nbits;
    unsigned int bit_mask    = table_mask >> 1; /* don't do 0 length codes */

    /* fill entries for codes short enough for a direct mapping */
    for (bit_num = 1; bit_num <= nbits; bit_num++) {
        for (sym = 0; sym < nsyms; sym++) {
            if (length[sym] != bit_num) continue;
#ifdef BITS_ORDER_MSB
            leaf = pos;
#else
            /* reverse the significant bits */
            fill = length[sym]; reverse = pos >> (nbits - fill); leaf = 0;
            do {leaf <<= 1; leaf |= reverse & 1; reverse >>= 1;} while (--fill);
#endif

            if((pos += bit_mask) > table_mask) return 1; /* table overrun */

            /* fill all possible lookups of this symbol with the symbol itself */
#ifdef BITS_ORDER_MSB
            for (fill = bit_mask; fill-- > 0;) table[leaf++] = sym;
#else
            fill = bit_mask; next_symbol = 1 << bit_num;
            do { table[leaf] = sym; leaf += next_symbol; } while (--fill);
#endif
        }
        bit_mask >>= 1;
    }

    /* exit with success if table is now complete */
    if (pos == table_mask) return 0;

    /* mark all remaining table entries as unused */
    for (sym = pos; sym < table_mask; sym++) {
#ifdef BITS_ORDER_MSB
        table[sym] = 0xFFFF;
#else
        reverse = sym; leaf = 0; fill = nbits;
        do { leaf <<= 1; leaf |= reverse & 1; reverse >>= 1; } while (--fill);
        table[leaf] = 0xFFFF;
#endif
    }

    /* next_symbol = base of allocation for long codes */
    next_symbol = ((table_mask >> 1) < nsyms) ? nsyms : (table_mask >> 1);

    /* give ourselves room for codes to grow by up to 16 more bits.
     * codes now start at bit nbits+16 and end at (nbits+16-codelength) */
    pos <<= 16;
    table_mask <<= 16;
    bit_mask = 1 << 15;

    for (bit_num = nbits+1; bit_num <= HUFF_MAXBITS; bit_num++) {
        for (sym = 0; sym < nsyms; sym++) {
            if (length[sym] != bit_num) continue;
            if (pos >= table_mask) return 1; /* table overflow */

#ifdef BITS_ORDER_MSB
            leaf = pos >> 16;
#else
            /* leaf = the first nbits of the code, reversed */
            reverse = pos >> 16; leaf = 0; fill = nbits;
            do {leaf <<= 1; leaf |= reverse & 1; reverse >>= 1;} while (--fill);
#endif
            for (fill = 0; fill < (bit_num - nbits); fill++) {
                /* if this path hasn't been taken yet, 'allocate' two entries */
                if (table[leaf] == 0xFFFF) {
                    table[(next_symbol << 1)     ] = 0xFFFF;
                    table[(next_symbol << 1) + 1 ] = 0xFFFF;
                    table[leaf] = next_symbol++;
                }

                /* follow the path and select either left or right for next bit */
                leaf = table[leaf] << 1;
                if ((pos >> (15-fill)) & 1) leaf++;
            }
            table[leaf] = sym;
            pos += bit_mask;
        }
        bit_mask >>= 1;
    }

    /* full table? */
    return (pos == table_mask) ? 0 : 1;
}
#endif
