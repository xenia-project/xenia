// File: crn_rand.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
// See:
// http://www.ciphersbyritter.com/NEWS4/RANDC.HTM
// http://burtleburtle.net/bob/rand/smallprng.html
// http://www.cs.ucl.ac.uk/staff/d.jones/GoodPracticeRNG.pdf
// See GPG7, page 120, or http://www.lomont.org/Math/Papers/2008/Lomont_PRNG_2008.pdf
#include "crn_core.h"
#include "crn_rand.h"
#include "crn_hash.h"

#define znew   (z=36969*(z&65535)+(z>>16))
#define wnew   (w=18000*(w&65535)+(w>>16))
#define MWC    ((znew<<16)+wnew )
#define SHR3  (jsr^=(jsr<<17), jsr^=(jsr>>13), jsr^=(jsr<<5))
#define CONG  (jcong=69069*jcong+1234567)
#define FIB   ((b=a+b),(a=b-a))
#define KISS  ((MWC^CONG)+SHR3)
#define LFIB4 (c++,t[c]=t[c]+t[UC(c+58)]+t[UC(c+119)]+t[UC(c+178)])
#define SWB   (c++,bro=(x<y),t[c]=(x=t[UC(c+34)])-(y=t[UC(c+19)]+bro))
#define UNI   (KISS*2.328306e-10)
#define VNI   ((long) KISS)*4.656613e-10
#define UC    (unsigned char)  /*a cast operation*/

//#define rot(x,k) (((x)<<(k))|((x)>>(32-(k))))
#define rot(x,k) CRNLIB_ROTATE_LEFT(x,k)

namespace crnlib
{
   static const double cNorm = 1.0 / (double)0x100000000ULL;

   kiss99::kiss99()
   {
      x = 123456789;
      y = 362436000;
      z = 521288629;
      c = 7654321;
   }

   void kiss99::seed(uint32 i, uint32 j, uint32 k)
   {
      x = i;
      y = j;
      z = k;
      c = 7654321;
   }

   inline uint32 kiss99::next()
   {
      x = 69069*x+12345;

      y ^= (y<<13);
      y ^= (y>>17);
      y ^= (y<<5);

      uint64 t = c;
      t += (698769069ULL*z);
      c = static_cast<uint32>(t >> 32);
      z = static_cast<uint32>(t);

      return (x+y+z);
   }

   inline uint32 ranctx::next()
   {
      uint32 e = a - rot(b, 27);
      a = b ^ rot(c, 17);
      b = c + d;
      c = d + e;
      d = e + a;
      return d;
   }

   void ranctx::seed(uint32 seed)
   {
      a = 0xf1ea5eed, b = c = d = seed;
      for (uint32 i=0; i<20; ++i)
         next();
   }

   well512::well512()
   {
      seed(0xDEADBE3F);
   }

   void well512::seed(uint32 seed[well512::cStateSize])
   {
      memcpy(m_state, seed, sizeof(m_state));
      m_index = 0;
   }

   void well512::seed(uint32 seed)
   {
      uint32 jsr = utils::swap32(seed) ^ 0xAAC29377;

      for (uint i = 0; i < cStateSize; i++)
      {
         SHR3;
         seed = bitmix32c(seed);

         m_state[i] = seed ^ jsr;
      }
      m_index = 0;
   }

   void well512::seed(uint32 seed1, uint32 seed2, uint32 seed3)
   {
      uint32 jsr = seed2;
      uint32 jcong = seed3;

      for (uint i = 0; i < cStateSize; i++)
      {
         SHR3;
         seed1 = bitmix32c(seed1);
         CONG;

         m_state[i] = seed1 ^ jsr ^ jcong;
      }
      m_index = 0;
   }

   inline uint32 well512::next()
   {
      uint32 a, b, c, d;
      a = m_state[m_index];
      c = m_state[(m_index+13)&15];
      b = a^c^(a<<16)^(c<<15);
      c = m_state[(m_index+9)&15];
      c ^= (c>>11);
      a = m_state[m_index] = b^c;
      d = a^((a<<5)&0xDA442D20UL);
      m_index = (m_index + 15)&15;
      a = m_state[m_index];
      m_state[m_index] = a^b^d^(a<<2)^(b<<18)^(c<<28);
      return m_state[m_index];
   }

   random::random()
   {
      seed(12345,65435,34221);
   }

   random::random(uint32 i)
   {
      seed(i);
   }

   void random::seed(uint32 i1, uint32 i2, uint32 i3)
   {
      m_ranctx.seed(i1^i2^i3);

      m_kiss99.seed(i1, i2, i3);

      m_well512.seed(i1, i2, i3);

      for (uint i = 0; i < 100; i++)
         urand32();
   }

   void random::seed(uint32 i)
   {
      uint32 jsr = i;
      SHR3; SHR3;
      uint32 jcong = utils::swap32(~jsr);
      CONG; CONG;
      uint32 i1 = SHR3 ^ CONG;
      uint32 i2 = SHR3 ^ CONG;
      uint32 i3 = SHR3 + CONG;
      seed(i1, i2, i3);
   }

   uint32 random::urand32()
   {
      return m_kiss99.next() ^ (m_ranctx.next() + m_well512.next());
   }

   uint64 random::urand64()
   {
      uint64 result = urand32();
      result <<= 32ULL;
      result |= urand32();
      return result;
   }
   uint32 random::fast_urand32()
   {
      return m_well512.next();
   }

   uint32 random::bit()
   {
      uint32 k = urand32();
      return (k ^ (k >> 6) ^ (k >> 10) ^ (k >> 30)) & 1;
   }

   double random::drand(double l, double h)
   {
      CRNLIB_ASSERT(l <= h);
      if (l >= h)
         return l;

      return math::clamp(l + (h - l) * (urand32() * cNorm), l, h);
   }

   float random::frand(float l, float h)
   {
      CRNLIB_ASSERT(l <= h);
      if (l >= h)
         return l;

      float r = static_cast<float>(l + (h - l) * (urand32() * cNorm));

      return math::clamp<float>(r, l, h);
   }
   
   int random::irand(int l, int h)
   {
      CRNLIB_ASSERT(l < h);
      if (l >= h)
         return l;

      uint32 range = static_cast<uint32>(h - l);

      uint32 rnd = urand32();

#if defined(_M_IX86) && defined(_MSC_VER)
      //uint32 rnd_range = static_cast<uint32>(__emulu(range, rnd) >> 32U);
      uint32 x[2];
      *reinterpret_cast<uint64*>(x) = __emulu(range, rnd);
      uint32 rnd_range = x[1];
#else
      uint32 rnd_range = static_cast<uint32>((((uint64)range) * ((uint64)rnd)) >> 32U);
#endif

      int result = l + rnd_range;
      CRNLIB_ASSERT((result >= l) && (result < h));
      return result;
   }

   int random::irand_inclusive(int l, int h)
   {
      CRNLIB_ASSERT(h < cINT32_MAX);
      return irand(l, h + 1);
   }

   /*
      ALGORITHM 712, COLLECTED ALGORITHMS FROM ACM.
      THIS WORK PUBLISHED IN TRANSACTIONS ON MATHEMATICAL SOFTWARE,
      VOL. 18, NO. 4, DECEMBER, 1992, PP. 434-435.
      The function returns a normally distributed pseudo-random number
      with a given mean and standard devaiation.  Calls are made to a
      function subprogram which must return independent random
      numbers uniform in the interval (0,1).
      The algorithm uses the ratio of uniforms method of A.J. Kinderman
      and J.F. Monahan augmented with quadratic bounding curves.
      */
   double random::gaussian(double mean, double stddev)
   {
      double  q,u,v,x,y;

      /*
      Generate P = (u,v) uniform in rect. enclosing acceptance region
      Make sure that any random numbers <= 0 are rejected, since
      gaussian() requires uniforms > 0, but RandomUniform() delivers >= 0.
      */
      do {
         u = drand(0, 1);
         v = drand(0, 1);
         if (u <= 0.0 || v <= 0.0) {
            u = 1.0;
            v = 1.0;
         }
         v = 1.7156 * (v - 0.5);

         /*  Evaluate the quadratic form */
         x = u - 0.449871;
         y = fabs(v) + 0.386595;
         q = x * x + y * (0.19600 * y - 0.25472 * x);

         /* Accept P if inside inner ellipse */
         if (q < 0.27597)
            break;

         /*  Reject P if outside outer ellipse, or outside acceptance region */
      } while ((q > 0.27846) || (v * v > -4.0 * log(u) * u * u));

      /*  Return ratio of P's coordinates as the normal deviate */
      return (mean + stddev * v / u);
   }

   void random::test()
   {
   }

   fast_random::fast_random() :
      jsr(0xABCD917A),
      jcong(0x17F3DEAD)
   {
   }

   fast_random::fast_random(const fast_random& other) :
      jsr(other.jsr), jcong(other.jcong)
   {
   }

   fast_random::fast_random(uint32 i)
   {
      seed(i);
   }

   fast_random& fast_random::operator=(const fast_random& other)
   {
      jsr = other.jsr;
      jcong = other.jcong;
      return *this;
   }

   void fast_random::seed(uint32 i)
   {
      jsr = i;
      SHR3;
      SHR3;
      jcong = (~i) ^ 0xDEADBEEF;

      SHR3;
      CONG;
   }

   uint32 fast_random::urand32()
   {
      return SHR3 ^ CONG;
   }

   uint64 fast_random::urand64()
   {
      uint64 result = urand32();
      result <<= 32ULL;
      result |= urand32();
      return result;
   }
   int fast_random::irand(int l, int h)
   {
      CRNLIB_ASSERT(l < h);
      if (l >= h)
         return l;

      uint32 range = static_cast<uint32>(h - l);

      uint32 rnd = urand32();

#if defined(_M_IX86) && defined(_MSC_VER)
      //uint32 rnd_range = static_cast<uint32>(__emulu(range, rnd) >> 32U);
      uint32 x[2];
      *reinterpret_cast<uint64*>(x) = __emulu(range, rnd);
      uint32 rnd_range = x[1];
#else
      uint32 rnd_range = static_cast<uint32>((((uint64)range) * ((uint64)rnd)) >> 32U);
#endif

      int result = l + rnd_range;
      CRNLIB_ASSERT((result >= l) && (result < h));
      return result;
   }

   double fast_random::drand(double l, double h)
   {
      CRNLIB_ASSERT(l <= h);
      if (l >= h)
         return l;

      return math::clamp(l + (h - l) * (urand32() * cNorm), l, h);
   }

   float fast_random::frand(float l, float h)
   {
      CRNLIB_ASSERT(l <= h);
      if (l >= h)
         return l;

      float r = static_cast<float>(l + (h - l) * (urand32() * cNorm));

      return math::clamp<float>(r, l, h);
   }

} // namespace crnlib

