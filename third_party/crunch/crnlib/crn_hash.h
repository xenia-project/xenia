// File: crn_hash.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   uint32 fast_hash (const void* p, int len);
   
   // 4-byte integer hash, full avalanche
   inline uint32 bitmix32c(uint32 a)
   {
      a = (a+0x7ed55d16) + (a<<12);
      a = (a^0xc761c23c) ^ (a>>19);
      a = (a+0x165667b1) + (a<<5);
      a = (a+0xd3a2646c) ^ (a<<9);
      a = (a+0xfd7046c5) + (a<<3);
      a = (a^0xb55a4f09) ^ (a>>16);
      return a;
   }
   
   // 4-byte integer hash, full avalanche, no constants
   inline uint32 bitmix32(uint32 a)
   {
      a -= (a<<6);
      a ^= (a>>17);
      a -= (a<<9);
      a ^= (a<<4);
      a -= (a<<3);
      a ^= (a<<10);
      a ^= (a>>15);
      return a;
   }
      
}  // namespace crnlib
