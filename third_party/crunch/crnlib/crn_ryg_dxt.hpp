// File: ryg_dxt.hpp
#pragma once

#include "crn_ryg_types.hpp"

namespace ryg_dxt
{
   extern sU8 Expand5[32];
   extern sU8 Expand6[64];
   extern sU8 OMatch5[256][2];
   extern sU8 OMatch6[256][2];
   extern sU8 OMatch5_3[256][2];
   extern sU8 OMatch6_3[256][2];
   extern sU8 QuantRBTab[256+16];
   extern sU8 QuantGTab[256+16];

   // initialize DXT codec. only needs to be called once.
   void sInitDXT();

   // input: a 4x4 pixel block, A8R8G8B8. you need to handle boundary cases
   // yourself.
   // alpha=sTRUE => use DXT5 (else use DXT1)
   // quality: 0=fastest (no dither), 1=medium (dither)
   void sCompressDXTBlock(sU8 *dest,const sU32 *src,sBool alpha,sInt quality);
   
   void sCompressDXT5ABlock(sU8 *dest,const sU32 *src,sInt quality);

} // namespace ryg_dxt

