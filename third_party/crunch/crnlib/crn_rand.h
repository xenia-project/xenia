// File: crn_rand.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once 

namespace crnlib
{
   class kiss99
   {
   public:
      kiss99();
            
      void seed(uint32 i, uint32 j, uint32 k);
            
      inline uint32 next();
      
   private:
      uint32 x;
      uint32 y;
      uint32 z;
      uint32 c;
   };
      
   class well512
   {
   public:
      well512();
      
      enum { cStateSize = 16 };
      void seed(uint32 seed[cStateSize]);
      void seed(uint32 seed);
      void seed(uint32 seed1, uint32 seed2, uint32 seed3);
            
      inline uint32 next();
   
   private:      
      uint32 m_state[cStateSize];
      uint32 m_index;
   };
   
   class ranctx 
   { 
   public:
      ranctx() { seed(0xDE149737); }
            
      void seed(uint32 seed);
      
      inline uint32 next();
   
   private:
      uint32 a; 
      uint32 b; 
      uint32 c; 
      uint32 d; 
   };
   
   class random
   {
   public:
      random();
      random(uint32 i);
      
      void seed(uint32 i);
      void seed(uint32 i1, uint32 i2, uint32 i3);
            
      uint32 urand32();
      uint64 urand64();
      
      // "Fast" variant uses no multiplies.
      uint32 fast_urand32();
      
      uint32 bit();
      
      // Returns random between [0, 1)
      double drand(double l, double h);
      
      float frand(float l, float h);
      
      // Returns random between [l, h)
      int irand(int l, int h);

      // Returns random between [l, h]
      int irand_inclusive(int l, int h);
                  
      double gaussian(double mean, double stddev);
      
      void test();

   private:
      ranctx m_ranctx;
      kiss99 m_kiss99;
      well512 m_well512;
   };
   
   // Simpler, minimal state PRNG
   class fast_random
   {
   public:
      fast_random();
      fast_random(uint32 i);
      fast_random(const fast_random& other);
      fast_random& operator=(const fast_random& other);
            
      void seed(uint32 i);
      
      uint32 urand32();
      uint64 urand64();
      
      int irand(int l, int h);
      
      double drand(double l, double h);

      float frand(float l, float h);
      
   private:      
      uint32 jsr;
      uint32 jcong;
   };

} // namespace crnlib
