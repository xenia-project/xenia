// File: crn_utils.cpp
#include "crn_core.h"
#include "crn_utils.h"

namespace crnlib
{
   namespace utils
   {
      void endian_switch_words(uint16* p, uint num)
      {
         uint16* p_end = p + num;
         while (p != p_end)
         {
            uint16 k = *p;
            *p++ = swap16(k);
         }
      }

      void endian_switch_dwords(uint32* p, uint num)
      {
         uint32* p_end = p + num;
         while (p != p_end)
         {
            uint32 k = *p;
            *p++ = swap32(k);
         }
      }

      void copy_words(uint16* pDst, const uint16* pSrc, uint num, bool endian_switch)
      {
         if (!endian_switch)
            memcpy(pDst, pSrc, num << 1U);
         else
         {
            uint16* pDst_end = pDst + num;
            while (pDst != pDst_end)
               *pDst++ = swap16(*pSrc++);
         }
      }

      void copy_dwords(uint32* pDst, const uint32* pSrc, uint num, bool endian_switch)
      {
         if (!endian_switch)
            memcpy(pDst, pSrc, num << 2U);
         else
         {
            uint32* pDst_end = pDst + num;
            while (pDst != pDst_end)
               *pDst++ = swap32(*pSrc++);
         }
      }

      uint compute_max_mips(uint width, uint height)
      {
         if ((width | height) == 0)
            return 0;

         uint num_mips = 1;

         while ((width > 1U) || (height > 1U))
         {
            width >>= 1U;
            height >>= 1U;
            num_mips++;
         }

         return num_mips;
      }

   } // namespace utils

} // namespace crnlib
