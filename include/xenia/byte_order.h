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


#if XE_COMPILER(MSVC)
#define XESWAP16                        _byteswap_ushort
#define XESWAP32                        _byteswap_ulong
#define XESWAP64                        _byteswap_uint64
#elif XE_LIKE(OSX)
#include <libkern/OSByteOrder.h>
#define XESWAP16                        OSSwapInt16
#define XESWAP32                        OSSwapInt32
#define XESWAP64                        OSSwapInt64
#else
#define XESWAP16                        bswap_16
#define XESWAP32                        bswap_32
#define XESWAP64                        bswap_64
#endif


#if XE_CPU(BIGENDIAN)
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


#define XEGETINT8BE(p)                  (  (int8_t)(*(p)))
#define XEGETUINT8BE(p)                 ( (uint8_t)(*(p)))
#define XEGETINT16BE(p)                 ( (int16_t)XESWAP16BE(*(uint16_t*)(p)))
#define XEGETUINT16BE(p)                ((uint16_t)XESWAP16BE(*(uint16_t*)(p)))
#define XEGETINT32BE(p)                 ( (int32_t)XESWAP32BE(*(uint32_t*)(p)))
#define XEGETUINT32BE(p)                ((uint32_t)XESWAP32BE(*(uint32_t*)(p)))
#define XEGETINT64BE(p)                 ( (int64_t)XESWAP64BE(*(uint64_t*)(p)))
#define XEGETUINT64BE(p)                ((uint64_t)XESWAP64BE(*(uint64_t*)(p)))
#define XEGETINT8LE(p)                  (  (int8_t)(*(p)))
#define XEGETUINT8LE(p)                 ( (uint8_t)(*(p)))
#define XEGETINT16LE(p)                 ( (int16_t)XESWAP16LE(*(uint16_t*)(p)))
#define XEGETUINT16LE(p)                ((uint16_t)XESWAP16LE(*(uint16_t*)(p)))
#define XEGETINT32LE(p)                 ( (int32_t)XESWAP32LE(*(uint32_t*)(p)))
#define XEGETUINT32LE(p)                ((uint32_t)XESWAP32LE(*(uint32_t*)(p)))
#define XEGETINT64LE(p)                 ( (int64_t)XESWAP64LE(*(uint64_t*)(p)))
#define XEGETUINT64LE(p)                ((uint64_t)XESWAP64LE(*(uint64_t*)(p)))


#endif  // XENIA_BYTE_ORDER_H_
