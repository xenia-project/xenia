/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BYTE_ORDER_H_
#define XENIA_BYTE_ORDER_H_

#include <xenia/platform.h>
#include <xenia/types.h>


#if XE_COMPILER_MSVC
#define XESWAP16                        _byteswap_ushort
#define XESWAP32                        _byteswap_ulong
#define XESWAP64                        _byteswap_uint64
#elif XE_LIKE_OSX
#include <libkern/OSByteOrder.h>
#define XESWAP16                        OSSwapInt16
#define XESWAP32                        OSSwapInt32
#define XESWAP64                        OSSwapInt64
#else
#define XESWAP16                        __bswap_16
#define XESWAP32                        __bswap_32
#define XESWAP64                        __bswap_64
#endif


#if XE_CPU_BIGENDIAN
#define XESWAP16BE(p)                   (p)
#define XESWAP32BE(p)                   (p)
#define XESWAP64BE(p)                   (p)
#define XESWAP16LE(p)                   XESWAP16(p)
#define XESWAP32LE(p)                   XESWAP32(p)
#define XESWAP64LE(p)                   XESWAP64(p)
#else
#define XESWAP16BE(p)                   XESWAP16(p)
#define XESWAP32BE(p)                   XESWAP32(p)
#define XESWAP64BE(p)                   XESWAP64(p)
#define XESWAP16LE(p)                   (p)
#define XESWAP32LE(p)                   (p)
#define XESWAP64LE(p)                   (p)
#endif


#if XE_CPU_BIGENDIAN
#define XESWAPF32BE(p)                  (p)
#define XESWAPF64BE(p)                  (p)
XEFORCEINLINE float XESWAPF32LE(float value) {
  uint32_t dummy = XESWAP32LE(*reinterpret_cast<uint32_t *>(&value));
  return *reinterpret_cast<float *>(&dummy);
}
XEFORCEINLINE double XESWAPF64LE(double value) {
  uint64_t dummy = XESWAP64LE(*reinterpret_cast<uint64_t *>(&value));
  return *reinterpret_cast<double *>(&dummy);
}
#else
XEFORCEINLINE float XESWAPF32BE(float value) {
  uint32_t dummy = XESWAP32BE(*reinterpret_cast<uint32_t *>(&value));
  return *reinterpret_cast<float *>(&dummy);
}
XEFORCEINLINE double XESWAPF64BE(double value) {
  uint64_t dummy = XESWAP64BE(*reinterpret_cast<uint64_t *>(&value));
  return *reinterpret_cast<double *>(&dummy);
}
#define XESWAPF32LE(p)                  (p)
#define XESWAPF64LE(p)                  (p)
#endif


#define XEGETINT8BE(p)                  (  (int8_t)(*(p)))
#define XEGETUINT8BE(p)                 ( (uint8_t)(*(p)))
#define XEGETINT16BE(p)                 ( (int16_t)XESWAP16BE(*(uint16_t*)(p)))
#define XEGETUINT16BE(p)                ((uint16_t)XESWAP16BE(*(uint16_t*)(p)))
#define XEGETUINT24BE(p) \
    (((uint32_t)XEGETUINT8BE((p) + 0) << 16) | \
     ((uint32_t)XEGETUINT8BE((p) + 1) << 8) | \
     (uint32_t)XEGETUINT8BE((p) + 2))
#define XEGETINT32BE(p)                 ( (int32_t)XESWAP32BE(*(uint32_t*)(p)))
#define XEGETUINT32BE(p)                ((uint32_t)XESWAP32BE(*(uint32_t*)(p)))
#define XEGETINT64BE(p)                 ( (int64_t)XESWAP64BE(*(uint64_t*)(p)))
#define XEGETUINT64BE(p)                ((uint64_t)XESWAP64BE(*(uint64_t*)(p)))
#define XEGETINT8LE(p)                  (  (int8_t)(*(p)))
#define XEGETUINT8LE(p)                 ( (uint8_t)(*(p)))
#define XEGETINT16LE(p)                 ( (int16_t)XESWAP16LE(*(uint16_t*)(p)))
#define XEGETUINT16LE(p)                ((uint16_t)XESWAP16LE(*(uint16_t*)(p)))
#define XEGETUINT24LE(p) \
    (((uint32_t)XEGETUINT8LE((p) + 2) << 16) | \
     ((uint32_t)XEGETUINT8LE((p) + 1) << 8) | \
     (uint32_t)XEGETUINT8LE((p) + 0))
#define XEGETINT32LE(p)                 ( (int32_t)XESWAP32LE(*(uint32_t*)(p)))
#define XEGETUINT32LE(p)                ((uint32_t)XESWAP32LE(*(uint32_t*)(p)))
#define XEGETINT64LE(p)                 ( (int64_t)XESWAP64LE(*(uint64_t*)(p)))
#define XEGETUINT64LE(p)                ((uint64_t)XESWAP64LE(*(uint64_t*)(p)))

#define XESETINT8BE(p, v)               (*(  (int8_t*)(p)) = (int8_t)v)
#define XESETUINT8BE(p, v)              (*( (uint8_t*)(p)) = (uint8_t)v)
#define XESETINT16BE(p, v)              (*( (int16_t*)(p)) = XESWAP16BE( (int16_t)v))
#define XESETUINT16BE(p, v)             (*((uint16_t*)(p)) = XESWAP16BE((uint16_t)v))
#define XESETINT32BE(p, v)              (*( (int32_t*)(p)) = XESWAP32BE( (int32_t)v))
#define XESETUINT32BE(p, v)             (*((uint32_t*)(p)) = XESWAP32BE((uint32_t)v))
#define XESETINT64BE(p, v)              (*( (int64_t*)(p)) = XESWAP64BE( (int64_t)v))
#define XESETUINT64BE(p, v)             (*((uint64_t*)(p)) = XESWAP64BE((uint64_t)v))
#define XESETINT8LE(p, v)               (*(  (int8_t*)(p)) =   (int8_t)v)
#define XESETUINT8LE(p, v)              (*( (uint8_t*)(p)) =  (uint8_t)v)
#define XESETINT16LE(p, v)              (*( (int16_t*)(p)) =  (int16_t)v)
#define XESETUINT16LE(p, v)             (*((uint16_t*)(p)) = (uint16_t)v)
#define XESETINT32LE(p, v)              (*( (int32_t*)(p)) =  (int32_t)v)
#define XESETUINT32LE(p, v)             (*((uint32_t*)(p)) = (uint32_t)v)
#define XESETINT64LE(p, v)              (*( (int64_t*)(p)) =  (int64_t)v)
#define XESETUINT64LE(p, v)             (*((uint64_t*)(p)) = (uint64_t)v)


#endif  // XENIA_BYTE_ORDER_H_
