/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/debug/protocols/ws/simple_sha1.h>

#if XE_PLATFORM_WIN32
#include <winsock2.h>
#else
#include <arpa/inet.h>
#endif  // WIN32


using namespace xe;
using namespace xe::debug::protocols::ws;


namespace {


// http://git.kernel.org/?p=git/git.git;a=blob;f=block-sha1/sha1.c

/*
 * SHA1 routine optimized to do word accesses rather than byte accesses,
 * and to avoid unnecessary copies into the context array.
 *
 * This was initially based on the Mozilla SHA1 implementation, although
 * none of the original Mozilla code remains.
 */

typedef struct {
  size_t  size;
  uint32_t H[5];
  uint32_t W[16];
} SHA_CTX;

#define SHA_ROT(X,l,r)  (((X) << (l)) | ((X) >> (r)))
#define SHA_ROL(X,n)  SHA_ROT(X,n,32-(n))
#define SHA_ROR(X,n)  SHA_ROT(X,32-(n),n)

#define setW(x, val) (*(volatile unsigned int *)&W(x) = (val))

#define get_be32(p) ntohl(*(unsigned int *)(p))
#define put_be32(p, v)  do { *(unsigned int *)(p) = htonl(v); } while (0)

#define W(x) (array[(x)&15])

#define SHA_SRC(t) get_be32((unsigned char *) block + (t)*4)
#define SHA_MIX(t) SHA_ROL(W((t)+13) ^ W((t)+8) ^ W((t)+2) ^ W(t), 1);

#define SHA_ROUND(t, input, fn, constant, A, B, C, D, E) do { \
  unsigned int TEMP = input(t); setW(t, TEMP); \
  E += TEMP + SHA_ROL(A,5) + (fn) + (constant); \
  B = SHA_ROR(B, 2); } while (0)

#define T_0_15(t, A, B, C, D, E)  SHA_ROUND(t, SHA_SRC, (((C^D)&B)^D) , 0x5a827999, A, B, C, D, E )
#define T_16_19(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (((C^D)&B)^D) , 0x5a827999, A, B, C, D, E )
#define T_20_39(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (B^C^D) , 0x6ed9eba1, A, B, C, D, E )
#define T_40_59(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, ((B&C)+(D&(B^C))) , 0x8f1bbcdc, A, B, C, D, E )
#define T_60_79(t, A, B, C, D, E) SHA_ROUND(t, SHA_MIX, (B^C^D) ,  0xca62c1d6, A, B, C, D, E )

static void SHA1_Block(SHA_CTX *ctx, const void *block) {
  uint32_t A = ctx->H[0];
  uint32_t B = ctx->H[1];
  uint32_t C = ctx->H[2];
  uint32_t D = ctx->H[3];
  uint32_t E = ctx->H[4];

  uint32_t array[16];

  /* Round 1 - iterations 0-16 take their input from 'block' */
  T_0_15( 0, A, B, C, D, E);
  T_0_15( 1, E, A, B, C, D);
  T_0_15( 2, D, E, A, B, C);
  T_0_15( 3, C, D, E, A, B);
  T_0_15( 4, B, C, D, E, A);
  T_0_15( 5, A, B, C, D, E);
  T_0_15( 6, E, A, B, C, D);
  T_0_15( 7, D, E, A, B, C);
  T_0_15( 8, C, D, E, A, B);
  T_0_15( 9, B, C, D, E, A);
  T_0_15(10, A, B, C, D, E);
  T_0_15(11, E, A, B, C, D);
  T_0_15(12, D, E, A, B, C);
  T_0_15(13, C, D, E, A, B);
  T_0_15(14, B, C, D, E, A);
  T_0_15(15, A, B, C, D, E);

  /* Round 1 - tail. Input from 512-bit mixing array */
  T_16_19(16, E, A, B, C, D);
  T_16_19(17, D, E, A, B, C);
  T_16_19(18, C, D, E, A, B);
  T_16_19(19, B, C, D, E, A);

  /* Round 2 */
  T_20_39(20, A, B, C, D, E);
  T_20_39(21, E, A, B, C, D);
  T_20_39(22, D, E, A, B, C);
  T_20_39(23, C, D, E, A, B);
  T_20_39(24, B, C, D, E, A);
  T_20_39(25, A, B, C, D, E);
  T_20_39(26, E, A, B, C, D);
  T_20_39(27, D, E, A, B, C);
  T_20_39(28, C, D, E, A, B);
  T_20_39(29, B, C, D, E, A);
  T_20_39(30, A, B, C, D, E);
  T_20_39(31, E, A, B, C, D);
  T_20_39(32, D, E, A, B, C);
  T_20_39(33, C, D, E, A, B);
  T_20_39(34, B, C, D, E, A);
  T_20_39(35, A, B, C, D, E);
  T_20_39(36, E, A, B, C, D);
  T_20_39(37, D, E, A, B, C);
  T_20_39(38, C, D, E, A, B);
  T_20_39(39, B, C, D, E, A);

  /* Round 3 */
  T_40_59(40, A, B, C, D, E);
  T_40_59(41, E, A, B, C, D);
  T_40_59(42, D, E, A, B, C);
  T_40_59(43, C, D, E, A, B);
  T_40_59(44, B, C, D, E, A);
  T_40_59(45, A, B, C, D, E);
  T_40_59(46, E, A, B, C, D);
  T_40_59(47, D, E, A, B, C);
  T_40_59(48, C, D, E, A, B);
  T_40_59(49, B, C, D, E, A);
  T_40_59(50, A, B, C, D, E);
  T_40_59(51, E, A, B, C, D);
  T_40_59(52, D, E, A, B, C);
  T_40_59(53, C, D, E, A, B);
  T_40_59(54, B, C, D, E, A);
  T_40_59(55, A, B, C, D, E);
  T_40_59(56, E, A, B, C, D);
  T_40_59(57, D, E, A, B, C);
  T_40_59(58, C, D, E, A, B);
  T_40_59(59, B, C, D, E, A);

  /* Round 4 */
  T_60_79(60, A, B, C, D, E);
  T_60_79(61, E, A, B, C, D);
  T_60_79(62, D, E, A, B, C);
  T_60_79(63, C, D, E, A, B);
  T_60_79(64, B, C, D, E, A);
  T_60_79(65, A, B, C, D, E);
  T_60_79(66, E, A, B, C, D);
  T_60_79(67, D, E, A, B, C);
  T_60_79(68, C, D, E, A, B);
  T_60_79(69, B, C, D, E, A);
  T_60_79(70, A, B, C, D, E);
  T_60_79(71, E, A, B, C, D);
  T_60_79(72, D, E, A, B, C);
  T_60_79(73, C, D, E, A, B);
  T_60_79(74, B, C, D, E, A);
  T_60_79(75, A, B, C, D, E);
  T_60_79(76, E, A, B, C, D);
  T_60_79(77, D, E, A, B, C);
  T_60_79(78, C, D, E, A, B);
  T_60_79(79, B, C, D, E, A);

  ctx->H[0] += A;
  ctx->H[1] += B;
  ctx->H[2] += C;
  ctx->H[3] += D;
  ctx->H[4] += E;
}

void SHA1_Update(SHA_CTX *ctx, const void *data, unsigned long len)
{
  uint32_t lenW = ctx->size & 63;
  ctx->size += len;

  if (lenW) {
    uint32_t left = 64 - lenW;
    if (len < left) {
      left = len;
    }
    memcpy(lenW + (char *)ctx->W, data, left);
    lenW = (lenW + left) & 63;
    len -= left;
    data = ((const char *)data + left);
    if (lenW) {
      return;
    }
    SHA1_Block(ctx, ctx->W);
  }
  while (len >= 64) {
    SHA1_Block(ctx, data);
    data = ((const char *)data + 64);
    len -= 64;
  }
  if (len) {
    memcpy(ctx->W, data, len);
  }
}

}


void xe::debug::protocols::ws::SHA1(
    const uint8_t* data, size_t length, uint8_t out_hash[20]) {
  static const uint8_t pad[64] = { 0x80 };

  SHA_CTX ctx = {
    0,
    {
      0x67452301,
      0xefcdab89,
      0x98badcfe,
      0x10325476,
      0xc3d2e1f0,
    },
    { 0 }
  };

  SHA1_Update(&ctx, data, (unsigned long)length);

  uint32_t padlen[2] = {
    htonl((uint32_t)(ctx.size >> 29)),
    htonl((uint32_t)(ctx.size << 3)),
  };

  size_t i = ctx.size & 63;
  SHA1_Update(&ctx, pad, 1 + (63 & (55 - i)));
  SHA1_Update(&ctx, padlen, 8);

  for (size_t n = 0; n < 5; n++) {
    put_be32(out_hash + n * 4, ctx.H[n]);
  }
}
