// File: crn_math.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"

namespace crnlib
{
   namespace math
   {
      uint g_bitmasks[32] =
      {
         1U <<  0U,         1U <<  1U,          1U <<  2U,        1U <<  3U,
         1U <<  4U,         1U <<  5U,          1U <<  6U,        1U <<  7U,
         1U <<  8U,         1U <<  9U,          1U << 10U,        1U << 11U,
         1U << 12U,         1U << 13U,          1U << 14U,        1U << 15U,
         1U << 16U,         1U << 17U,          1U << 18U,        1U << 19U,
         1U << 20U,         1U << 21U,          1U << 22U,        1U << 23U,
         1U << 24U,         1U << 25U,          1U << 26U,        1U << 27U,
         1U << 28U,         1U << 29U,          1U << 30U,        1U << 31U
      };
      
      double compute_entropy(const uint8* p, uint n)
      {
         uint hist[256];
         utils::zero_object(hist);
         
         for (uint i = 0; i < n; i++)
            hist[*p++]++;
         
         double entropy = 0.0f;
         
         const double invln2 = 1.0f/log(2.0f);
         for (uint i = 0; i < 256; i++)
         {
            if (!hist[i])
               continue;
               
            double prob = static_cast<double>(hist[i]) / n;
            entropy += (-log(prob) * invln2) * hist[i];
         }
         
         return entropy;
      }

      void compute_lower_pow2_dim(int& width, int& height)
      {
         const int tex_width = width;
         const int tex_height = height;

         width = 1;
         for ( ; ; )
         {
            if ((width * 2) > tex_width)
               break;
            width *= 2;
         }

         height = 1;
         for ( ; ; )
         {
            if ((height * 2) > tex_height)
               break;
            height *= 2;
         }
      }         

      void compute_upper_pow2_dim(int& width, int& height)
      {
         if (!math::is_power_of_2((uint32)width))
            width = math::next_pow2((uint32)width);

         if (!math::is_power_of_2((uint32)height))
            height = math::next_pow2((uint32)height);
      }         
      
   } // namespace math
} // namespace crnlib
