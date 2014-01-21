// File: crn_math.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#if defined(_M_IX86) && defined(_MSC_VER)
   #include <intrin.h>
   #pragma intrinsic(__emulu)
   unsigned __int64 __emulu(unsigned int a,unsigned int b );
#endif

namespace crnlib
{
   namespace math
   {
      const float cNearlyInfinite = 1.0e+37f;
						
      const float cDegToRad = 0.01745329252f;
      const float cRadToDeg = 57.29577951f;

      extern uint g_bitmasks[32];

      template<typename T> inline bool within_closed_range(T a, T b, T c) { return (a >= b) && (a <= c); }

      template<typename T> inline bool within_open_range(T a, T b, T c) { return (a >= b) && (a < c); }

      // Yes I know these should probably be pass by ref, not val:
      // http://www.stepanovpapers.com/notes.pdf
      // Just don't use them on non-simple (non built-in) types!
      template<typename T> inline T minimum(T a, T b) { return (a < b) ? a : b; }

      template<typename T> inline T minimum(T a, T b, T c) { return minimum(minimum(a, b), c); }

      template<typename T> inline T maximum(T a, T b) { return (a > b) ? a : b; }

      template<typename T> inline T maximum(T a, T b, T c) { return maximum(maximum(a, b), c); }

      template<typename T, typename U> inline T lerp(T a, T b, U c) { return a + (b - a) * c; }

      template<typename T> inline T clamp(T value, T low, T high) { return (value < low) ? low : ((value > high) ? high : value); }

      template<typename T> inline T saturate(T value) { return (value < 0.0f) ? 0.0f : ((value > 1.0f) ? 1.0f : value); }

      inline int float_to_int(float f) { return static_cast<int>(f); }

      inline uint float_to_uint(float f) { return static_cast<uint>(f); }

      inline int float_to_int(double f) { return static_cast<int>(f); }

      inline uint float_to_uint(double f) { return static_cast<uint>(f); }

      inline int float_to_int_round(float f) { return static_cast<int>((f < 0.0f) ? -floor(-f + .5f) : floor(f + .5f)); }

      inline uint float_to_uint_round(float f) { return static_cast<uint>((f < 0.0f) ? 0.0f : floor(f + .5f)); }

      template<typename T> inline int sign(T value) { return (value < 0) ? -1 : ((value > 0) ? 1 : 0); }

      template<typename T> inline T square(T value) { return value * value; }

      inline bool is_power_of_2(uint32 x) { return x && ((x & (x - 1U)) == 0U); }
      inline bool is_power_of_2(uint64 x) { return x && ((x & (x - 1U)) == 0U); }

      template<typename T> inline T align_up_value(T x, uint alignment)
      {
         CRNLIB_ASSERT(is_power_of_2(alignment));
         uint q = static_cast<uint>(x);
         q = (q + alignment - 1) & (~(alignment - 1));
         return static_cast<T>(q);
      }

      template<typename T> inline T align_down_value(T x, uint alignment)
      {
         CRNLIB_ASSERT(is_power_of_2(alignment));
         uint q = static_cast<uint>(x);
         q = q & (~(alignment - 1));
         return static_cast<T>(q);
      }

      template<typename T> inline T get_align_up_value_delta(T x, uint alignment)
      {
         return align_up_value(x, alignment) - x;
      }
		      
		// From "Hackers Delight"
      inline uint32 next_pow2(uint32 val)
      {
         val--;
         val |= val >> 16;
         val |= val >> 8;
         val |= val >> 4;
         val |= val >> 2;
         val |= val >> 1;
         return val + 1;
      }

      inline uint64 next_pow2(uint64 val)
      {
         val--;
         val |= val >> 32;
         val |= val >> 16;
         val |= val >> 8;
         val |= val >> 4;
         val |= val >> 2;
         val |= val >> 1;
         return val + 1;
      }
            
      inline uint floor_log2i(uint v)
      {
         uint l = 0;
         while (v > 1U)
         {
            v >>= 1;
            l++;
         }
         return l;
      }

      inline uint ceil_log2i(uint v)
      {
         uint l = floor_log2i(v);
         if ((l != cIntBits) && (v > (1U << l)))
            l++;
         return l;
      }

      // Returns the total number of bits needed to encode v.
      inline uint total_bits(uint v)
      {
         uint l = 0;
         while (v > 0U)
         {
            v >>= 1;
            l++;
         }
         return l;
      }

      // Actually counts the number of set bits, but hey
      inline uint bitmask_size(uint mask)
      {
         uint size = 0;
         while (mask)
         {
            mask &= (mask - 1U);
            size++;
         }
         return size;
      }

      inline uint bitmask_ofs(uint mask)
      {
         if (!mask)
            return 0;
         uint ofs = 0;
         while ((mask & 1U) == 0)
         {
            mask >>= 1U;
            ofs++;
         }
         return ofs;
      }

      // See Bit Twiddling Hacks (public domain)
      // http://www-graphics.stanford.edu/~seander/bithacks.html
      inline uint count_trailing_zero_bits(uint v)
      {
         uint c = 32; // c will be the number of zero bits on the right

         static const unsigned int B[] = { 0x55555555, 0x33333333, 0x0F0F0F0F, 0x00FF00FF, 0x0000FFFF };
         static const unsigned int S[] = { 1, 2, 4, 8, 16 }; // Our Magic Binary Numbers

         for (int i = 4; i >= 0; --i) // unroll for more speed
         {
            if (v & B[i])
            {
               v <<= S[i];
               c -= S[i];
            }
         }

         if (v)
         {
            c--;
         }

         return c;
      }

      inline uint count_leading_zero_bits(uint v)
      {
         uint temp;
         uint result = 32U;

         temp = (v >> 16U); if (temp) { result -= 16U; v = temp; }
         temp = (v >>  8U); if (temp) { result -=  8U; v = temp; }
         temp = (v >>  4U); if (temp) { result -=  4U; v = temp; }
         temp = (v >>  2U); if (temp) { result -=  2U; v = temp; }
         temp = (v >>  1U); if (temp) { result -=  1U; v = temp; }

         if (v & 1U)
            result--;

         return result;
      }

      inline uint64 emulu(uint32 a, uint32 b)
      {
#if defined(_M_IX86) && defined(_MSC_VER)
         return __emulu(a, b);
#else
         return static_cast<uint64>(a) * static_cast<uint64>(b);
#endif
      }

      double compute_entropy(const uint8* p, uint n);

      void compute_lower_pow2_dim(int& width, int& height);
      void compute_upper_pow2_dim(int& width, int& height);
      
      inline bool equal_tol(float a, float b, float t)
      {
         return fabs(a - b) < ((maximum(fabs(a), fabs(b)) + 1.0f) * t);
      }

      inline bool equal_tol(double a, double b, double t)
      {
         return fabs(a - b) < ((maximum(fabs(a), fabs(b)) + 1.0f) * t);
      }
   }

} // namespace crnlib






