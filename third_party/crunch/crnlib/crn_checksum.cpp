// File: crn_checksum.cpp
#include "crn_core.h"

namespace crnlib
{
   // From the public domain stb.h header.
   uint adler32(const void* pBuf, size_t buflen, uint adler32)
   {
      const uint8* buffer = static_cast<const uint8*>(pBuf);
      
      const unsigned long ADLER_MOD = 65521;
      unsigned long s1 = adler32 & 0xffff, s2 = adler32 >> 16;
      size_t blocklen;
      unsigned long i;

      blocklen = buflen % 5552;
      while (buflen) {
         for (i=0; i + 7 < blocklen; i += 8) {
            s1 += buffer[0], s2 += s1;
            s1 += buffer[1], s2 += s1;
            s1 += buffer[2], s2 += s1;
            s1 += buffer[3], s2 += s1;
            s1 += buffer[4], s2 += s1;
            s1 += buffer[5], s2 += s1;
            s1 += buffer[6], s2 += s1;
            s1 += buffer[7], s2 += s1;

            buffer += 8;
         }

         for (; i < blocklen; ++i)
            s1 += *buffer++, s2 += s1;

         s1 %= ADLER_MOD, s2 %= ADLER_MOD;
         buflen -= blocklen;
         blocklen = 5552;
      }
      return (s2 << 16) + s1;
   }
   
   uint16 crc16(const void* pBuf, size_t len, uint16 crc)
   {
      crc = ~crc;

      const uint8* p = reinterpret_cast<const uint8*>(pBuf);
      while (len)
      {
         const uint16 q = *p++ ^ (crc >> 8);
         crc <<= 8U;
         uint16 r = (q >> 4) ^ q;
         crc ^= r;
         r <<= 5U;
         crc ^= r;
         r <<= 7U;
         crc ^= r; 
         len--;
      }

      return static_cast<uint16>(~crc);
   }

} // namespace crnlib

