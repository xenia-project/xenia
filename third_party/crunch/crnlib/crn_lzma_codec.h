// File: crn_lzma_codec.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_packed_uint.h"

namespace crnlib
{
   class lzma_codec
   {
   public:
      lzma_codec();
      ~lzma_codec();

      // Always available, because we're statically linking in lzmalib now vs. dynamically loading the DLL.
      bool is_initialized() const { return true; }

      bool pack(const void* p, uint n, crnlib::vector<uint8>& buf);

      bool unpack(const void* p, uint n, crnlib::vector<uint8>& buf);

   private:
      typedef int (CRNLIB_STDCALL *LzmaCompressFuncPtr)(unsigned char *dest, size_t *destLen, const unsigned char *src, size_t srcLen,
         unsigned char *outProps, size_t *outPropsSize, /* *outPropsSize must be = 5 */
         int level,      /* 0 <= level <= 9, default = 5 */
         unsigned dictSize,  /* default = (1 << 24) */
         int lc,        /* 0 <= lc <= 8, default = 3  */
         int lp,        /* 0 <= lp <= 4, default = 0  */
         int pb,        /* 0 <= pb <= 4, default = 2  */
         int fb,        /* 5 <= fb <= 273, default = 32 */
         int numThreads /* 1 or 2, default = 2 */
         );

      typedef int (CRNLIB_STDCALL  *LzmaUncompressFuncPtr)(unsigned char *dest, size_t *destLen, const unsigned char *src, size_t *srcLen,
         const unsigned char *props, size_t propsSize);

      LzmaCompressFuncPtr     m_pCompress;
      LzmaUncompressFuncPtr   m_pUncompress;

      enum { cLZMAPropsSize = 5 };

#pragma pack(push)
#pragma pack(1)
      struct header
      {
         enum { cSig = 'L' | ('0' << 8), cChecksumSkipBytes = 3 };
         packed_uint<2> m_sig;
         uint8          m_checksum;

         uint8          m_lzma_props[cLZMAPropsSize];

         packed_uint<4> m_comp_size;
         packed_uint<4> m_uncomp_size;

         packed_uint<4> m_adler32;
      };
#pragma pack(pop)

   };

} // namespace crnlib
