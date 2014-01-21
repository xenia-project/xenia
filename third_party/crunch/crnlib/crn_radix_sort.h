// File: crn_radix_sort.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   // Returns pointer to sorted array.
   template<typename T>
   T* radix_sort(uint num_vals, T* pBuf0, T* pBuf1, uint key_ofs, uint key_size)
   {  
      CRNLIB_ASSERT_OPEN_RANGE(key_ofs, 0, sizeof(T));
      CRNLIB_ASSERT_CLOSED_RANGE(key_size, 1, 4);

      uint hist[256 * 4];

      memset(hist, 0, sizeof(hist[0]) * 256 * key_size);

#define CRNLIB_GET_KEY(p) (*(uint*)((uint8*)(p) + key_ofs))

      if (key_size == 4)
      {
         T* p = pBuf0;
         T* q = pBuf0 + num_vals;
         for ( ; p != q; p++)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[        key        & 0xFF]++;
            hist[256 + ((key >>  8) & 0xFF)]++;
            hist[512 + ((key >> 16) & 0xFF)]++;
            hist[768 + ((key >> 24) & 0xFF)]++;
         }
      }
      else if (key_size == 3)
      {
         T* p = pBuf0;
         T* q = pBuf0 + num_vals;
         for ( ; p != q; p++)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[        key        & 0xFF]++;
            hist[256 + ((key >>  8) & 0xFF)]++;
            hist[512 + ((key >> 16) & 0xFF)]++;
         }
      }   
      else if (key_size == 2)
      {
         T* p = pBuf0;
         T* q = pBuf0 + (num_vals >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            const uint key0 = CRNLIB_GET_KEY(p);
            const uint key1 = CRNLIB_GET_KEY(p+1);

            hist[        key0         & 0xFF]++;
            hist[256 + ((key0 >>  8) & 0xFF)]++;

            hist[        key1        & 0xFF]++;
            hist[256 + ((key1 >>  8) & 0xFF)]++;
         }

         if (num_vals & 1)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[        key        & 0xFF]++;
            hist[256 + ((key >>  8) & 0xFF)]++;
         }
      }      
      else
      {
         CRNLIB_ASSERT(key_size == 1);
         if (key_size != 1)
            return NULL;

         T* p = pBuf0;
         T* q = pBuf0 + (num_vals >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            const uint key0 = CRNLIB_GET_KEY(p);
            const uint key1 = CRNLIB_GET_KEY(p+1);

            hist[key0 & 0xFF]++;
            hist[key1 & 0xFF]++;
         }

         if (num_vals & 1)
         {
            const uint key = CRNLIB_GET_KEY(p);
            hist[key & 0xFF]++;
         }
      }

      T* pCur = pBuf0;
      T* pNew = pBuf1;

      for (uint pass = 0; pass < key_size; pass++)
      {
         const uint* pHist = &hist[pass << 8];

         uint offsets[256];

         uint cur_ofs = 0;
         for (uint i = 0; i < 256; i += 2)
         {
            offsets[i] = cur_ofs;
            cur_ofs += pHist[i];

            offsets[i+1] = cur_ofs;
            cur_ofs += pHist[i+1];
         }

         const uint pass_shift = pass << 3;

         T* p = pCur;
         T* q = pCur + (num_vals >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            uint c0 = (CRNLIB_GET_KEY(p) >> pass_shift) & 0xFF;
            uint c1 = (CRNLIB_GET_KEY(p+1) >> pass_shift) & 0xFF;

            if (c0 == c1)
            {
               uint dst_offset0 = offsets[c0];

               offsets[c0] = dst_offset0 + 2;

               pNew[dst_offset0] = p[0];
               pNew[dst_offset0 + 1] = p[1];
            }
            else
            {
               uint dst_offset0 = offsets[c0]++;
               uint dst_offset1 = offsets[c1]++;

               pNew[dst_offset0] = p[0];
               pNew[dst_offset1] = p[1];
            }
         }

         if (num_vals & 1)
         {
            uint c = (CRNLIB_GET_KEY(p) >> pass_shift) & 0xFF;

            uint dst_offset = offsets[c];
            offsets[c] = dst_offset + 1;

            pNew[dst_offset] = *p;
         }

         T* t = pCur;
         pCur = pNew;
         pNew = t;
      }            

      return pCur;
   }

#undef CRNLIB_GET_KEY

   // Returns pointer to sorted array.
   template<typename T, typename Q>
   T* indirect_radix_sort(uint num_indices, T* pIndices0, T* pIndices1, const Q* pKeys, uint key_ofs, uint key_size, bool init_indices)
   {  
      CRNLIB_ASSERT_OPEN_RANGE(key_ofs, 0, sizeof(T));
      CRNLIB_ASSERT_CLOSED_RANGE(key_size, 1, 4);

      if (init_indices)
      {
         T* p = pIndices0;
         T* q = pIndices0 + (num_indices >> 1) * 2;
         uint i;
         for (i = 0; p != q; p += 2, i += 2)
         {
            p[0] = static_cast<T>(i);
            p[1] = static_cast<T>(i + 1); 
         }

         if (num_indices & 1)
            *p = static_cast<T>(i);
      }

      uint hist[256 * 4];

      memset(hist, 0, sizeof(hist[0]) * 256 * key_size);

#define CRNLIB_GET_KEY(p) (*(const uint*)((const uint8*)(pKeys + *(p)) + key_ofs))
#define CRNLIB_GET_KEY_FROM_INDEX(i) (*(const uint*)((const uint8*)(pKeys + (i)) + key_ofs))

      if (key_size == 4)
      {
         T* p = pIndices0;
         T* q = pIndices0 + num_indices;
         for ( ; p != q; p++)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[        key        & 0xFF]++;
            hist[256 + ((key >>  8) & 0xFF)]++;
            hist[512 + ((key >> 16) & 0xFF)]++;
            hist[768 + ((key >> 24) & 0xFF)]++;
         }
      }
      else if (key_size == 3)
      {
         T* p = pIndices0;
         T* q = pIndices0 + num_indices;
         for ( ; p != q; p++)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[        key        & 0xFF]++;
            hist[256 + ((key >>  8) & 0xFF)]++;
            hist[512 + ((key >> 16) & 0xFF)]++;
         }
      }   
      else if (key_size == 2)
      {
         T* p = pIndices0;
         T* q = pIndices0 + (num_indices >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            const uint key0 = CRNLIB_GET_KEY(p);
            const uint key1 = CRNLIB_GET_KEY(p+1);

            hist[        key0         & 0xFF]++;
            hist[256 + ((key0 >>  8) & 0xFF)]++;

            hist[        key1        & 0xFF]++;
            hist[256 + ((key1 >>  8) & 0xFF)]++;
         }

         if (num_indices & 1)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[        key        & 0xFF]++;
            hist[256 + ((key >>  8) & 0xFF)]++;
         }
      }      
      else
      {
         CRNLIB_ASSERT(key_size == 1);
         if (key_size != 1)
            return NULL;

         T* p = pIndices0;
         T* q = pIndices0 + (num_indices >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            const uint key0 = CRNLIB_GET_KEY(p);
            const uint key1 = CRNLIB_GET_KEY(p+1);

            hist[key0 & 0xFF]++;
            hist[key1 & 0xFF]++;
         }

         if (num_indices & 1)
         {
            const uint key = CRNLIB_GET_KEY(p);

            hist[key & 0xFF]++;
         }
      }      

      T* pCur = pIndices0;
      T* pNew = pIndices1;

      for (uint pass = 0; pass < key_size; pass++)
      {
         const uint* pHist = &hist[pass << 8];

         uint offsets[256];

         uint cur_ofs = 0;
         for (uint i = 0; i < 256; i += 2)
         {
            offsets[i] = cur_ofs;
            cur_ofs += pHist[i];

            offsets[i+1] = cur_ofs;
            cur_ofs += pHist[i+1];
         }

         const uint pass_shift = pass << 3;

         T* p = pCur;
         T* q = pCur + (num_indices >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            uint index0 = p[0];
            uint index1 = p[1];

            uint c0 = (CRNLIB_GET_KEY_FROM_INDEX(index0) >> pass_shift) & 0xFF;
            uint c1 = (CRNLIB_GET_KEY_FROM_INDEX(index1) >> pass_shift) & 0xFF;

            if (c0 == c1)
            {
               uint dst_offset0 = offsets[c0];

               offsets[c0] = dst_offset0 + 2;

               pNew[dst_offset0] = static_cast<T>(index0);
               pNew[dst_offset0 + 1] = static_cast<T>(index1);
            }
            else
            {
               uint dst_offset0 = offsets[c0]++;
               uint dst_offset1 = offsets[c1]++;

               pNew[dst_offset0] = static_cast<T>(index0);
               pNew[dst_offset1] = static_cast<T>(index1);
            }
         }

         if (num_indices & 1)
         {
            uint index = *p;
            uint c = (CRNLIB_GET_KEY_FROM_INDEX(index) >> pass_shift) & 0xFF;

            uint dst_offset = offsets[c];
            offsets[c] = dst_offset + 1;

            pNew[dst_offset] = static_cast<T>(index);
         }

         T* t = pCur;
         pCur = pNew;
         pNew = t;
      }            

      return pCur;
   }

#undef CRNLIB_GET_KEY
#undef CRNLIB_GET_KEY_FROM_INDEX

} // namespace crnlib
