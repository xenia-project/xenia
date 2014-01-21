// File: crn_ryg_dxt.cpp
// RYG's real-time DXT compressor - Public domain.
#include "crn_core.h"
#include "crn_ryg_types.hpp"
#include "crn_ryg_dxt.hpp"

#ifdef _MSC_VER
#pragma warning (disable: 4244)              // conversion from 'a' to 'b', possible loss of data
#endif

namespace ryg_dxt
{
   // Couple of tables...
   sU8 Expand5[32];
   sU8 Expand6[64];
   sU8 OMatch5[256][2];
   sU8 OMatch6[256][2];
   sU8 OMatch5_3[256][2];
   sU8 OMatch6_3[256][2];
   sU8 QuantRBTab[256+16];
   sU8 QuantGTab[256+16];

   static sInt Mul8Bit(sInt a,sInt b)
   {
     sInt t = a*b + 128;
     return (t + (t >> 8)) >> 8;
   }

   union Pixel
   {
     struct
     {
       sU8 b,g,r,a;
     };
     sU32 v;

     void From16Bit(sU16 v)
     {
       sInt rv = (v & 0xf800) >> 11;
       sInt gv = (v & 0x07e0) >>  5;
       sInt bv = (v & 0x001f) >>  0;

       a = 0;
       r = Expand5[rv];
       g = Expand6[gv];
       b = Expand5[bv];
     }

     sU16 As16Bit() const
     {
       return (Mul8Bit(r,31) << 11) + (Mul8Bit(g,63) << 5) + Mul8Bit(b,31);
     }

     void LerpRGB(const Pixel &p1,const Pixel &p2,sInt f)
     {
       r = p1.r + Mul8Bit(p2.r - p1.r,f);
       g = p1.g + Mul8Bit(p2.g - p1.g,f);
       b = p1.b + Mul8Bit(p2.b - p1.b,f);
     }
   };

   /****************************************************************************/

   static void PrepareOptTable4(sU8 *Table,const sU8 *expand,sInt size)
   {
     for(sInt i=0;i<256;i++)
     {
       sInt bestErr = 256;

       for(sInt min=0;min<size;min++)
       {
         for(sInt max=0;max<size;max++)
         {
           sInt mine = expand[min];
           sInt maxe = expand[max];
           //sInt err = sAbs(maxe + Mul8Bit(mine-maxe,0x55) - i);
           sInt err = sAbs(((maxe*2+mine)/3) - i);
           err += ((sAbs(maxe-mine)*8)>>8); // approx. .03f

           if(err < bestErr)
           {
             Table[i*2+0] = max;
             Table[i*2+1] = min;
             bestErr = err;
           }
         }
       }
     }
   }

   static void PrepareOptTable3(sU8 *Table,const sU8 *expand,sInt size)
   {
      for(sInt i=0;i<256;i++)
      {
         sInt bestErr = 256;

         for(sInt min=0;min<size;min++)
         {
            for(sInt max=0;max<size;max++)
            {
               sInt mine = expand[min];
               sInt maxe = expand[max];
               sInt err = sAbs(((mine + maxe) >> 1) - i);
               err += ((sAbs(maxe-mine)*8)>>8); // approx. .03f

               if(err < bestErr)
               {
                  Table[i*2+0] = max;
                  Table[i*2+1] = min;
                  bestErr = err;
               }
            }
         }
      }
   }

   static inline void EvalColors(Pixel *color,sU16 c0,sU16 c1)
   {
     color[0].From16Bit(c0);
     color[1].From16Bit(c1);
     color[2].LerpRGB(color[0],color[1],0x55);
     color[3].LerpRGB(color[0],color[1],0xaa);
   }

   // Block dithering function. Simply dithers a block to 565 RGB.
   // (Floyd-Steinberg)
   static void DitherBlock(Pixel *dest,const Pixel *block)
   {
     sInt err[8],*ep1 = err,*ep2 = err+4;

     // process channels seperately
     for(sInt ch=0;ch<3;ch++)
     {
       sU8 *bp = (sU8 *) block;
       sU8 *dp = (sU8 *) dest;
       sU8 *quant = (ch == 1) ? QuantGTab+8 : QuantRBTab+8;

       bp += ch;
       dp += ch;
       sSetMem(err,0,sizeof(err));

       for(sInt y=0;y<4;y++)
       {
         // pixel 0
         dp[ 0] = quant[bp[ 0] + ((3*ep2[1] + 5*ep2[0]) >> 4)];
         ep1[0] = bp[ 0] - dp[ 0];

         // pixel 1
         dp[ 4] = quant[bp[ 4] + ((7*ep1[0] + 3*ep2[2] + 5*ep2[1] + ep2[0]) >> 4)];
         ep1[1] = bp[ 4] - dp[ 4];

         // pixel 2
         dp[ 8] = quant[bp[ 8] + ((7*ep1[1] + 3*ep2[3] + 5*ep2[2] + ep2[1]) >> 4)];
         ep1[2] = bp[ 8] - dp[ 8];

         // pixel 3
         dp[12] = quant[bp[12] + ((7*ep1[2] + 5*ep2[3] + ep2[2]) >> 4)];
         ep1[3] = bp[12] - dp[12];

         // advance to next line
         sSwap(ep1,ep2);
         bp += 16;
         dp += 16;
       }
     }
   }

   // The color matching function
   static sU32 MatchColorsBlock(const Pixel *block,const Pixel *color,sBool dither)
   {
     sU32 mask = 0;
     sInt dirr = color[0].r - color[1].r;
     sInt dirg = color[0].g - color[1].g;
     sInt dirb = color[0].b - color[1].b;

     sInt dots[16];
     for(sInt i=0;i<16;i++)
       dots[i] = block[i].r*dirr + block[i].g*dirg + block[i].b*dirb;

     sInt stops[4];
     for(sInt i=0;i<4;i++)
       stops[i] = color[i].r*dirr + color[i].g*dirg + color[i].b*dirb;

     sInt c0Point = (stops[1] + stops[3]) >> 1;
     sInt halfPoint = (stops[3] + stops[2]) >> 1;
     sInt c3Point = (stops[2] + stops[0]) >> 1;

     if(!dither)
     {
       // the version without dithering is straightforward
       for(sInt i=15;i>=0;i--)
       {
         mask <<= 2;
         sInt dot = dots[i];

         if(dot < halfPoint)
           mask |= (dot < c0Point) ? 1 : 3;
         else
           mask |= (dot < c3Point) ? 2 : 0;
       }
     }
     else
     {
       // with floyd-steinberg dithering (see above)
       sInt err[8],*ep1 = err,*ep2 = err+4;
       sInt *dp = dots;

       c0Point <<= 4;
       halfPoint <<= 4;
       c3Point <<= 4;
       for(sInt i=0;i<8;i++)
         err[i] = 0;

       for(sInt y=0;y<4;y++)
       {
         sInt dot,lmask,step;

         // pixel 0
         dot = (dp[0] << 4) + (3*ep2[1] + 5*ep2[0]);
         if(dot < halfPoint)
           step = (dot < c0Point) ? 1 : 3;
         else
           step = (dot < c3Point) ? 2 : 0;

         ep1[0] = dp[0] - stops[step];
         lmask = step;

         // pixel 1
         dot = (dp[1] << 4) + (7*ep1[0] + 3*ep2[2] + 5*ep2[1] + ep2[0]);
         if(dot < halfPoint)
           step = (dot < c0Point) ? 1 : 3;
         else
           step = (dot < c3Point) ? 2 : 0;

         ep1[1] = dp[1] - stops[step];
         lmask |= step<<2;

         // pixel 2
         dot = (dp[2] << 4) + (7*ep1[1] + 3*ep2[3] + 5*ep2[2] + ep2[1]);
         if(dot < halfPoint)
           step = (dot < c0Point) ? 1 : 3;
         else
           step = (dot < c3Point) ? 2 : 0;

         ep1[2] = dp[2] - stops[step];
         lmask |= step<<4;

         // pixel 3
         dot = (dp[3] << 4) + (7*ep1[2] + 5*ep2[3] + ep2[2]);
         if(dot < halfPoint)
           step = (dot < c0Point) ? 1 : 3;
         else
           step = (dot < c3Point) ? 2 : 0;

         ep1[3] = dp[3] - stops[step];
         lmask |= step<<6;

         // advance to next line
         sSwap(ep1,ep2);
         dp += 4;
         mask |= lmask << (y*8);
       }
     }

     return mask;
   }

   // The color optimization function. (Clever code, part 1)
   static void OptimizeColorsBlock(const Pixel *block,sU16 &max16,sU16 &min16)
   {
     static const sInt nIterPower = 4;

     // determine color distribution
     sInt mu[3],min[3],max[3];

     for(sInt ch=0;ch<3;ch++)
     {
       const sU8 *bp = ((const sU8 *) block) + ch;
       sInt muv,minv,maxv;

       muv = minv = maxv = bp[0];
       for(sInt i=4;i<64;i+=4)
       {
         muv += bp[i];
         minv = sMin<sInt>(minv,bp[i]);
         maxv = sMax<sInt>(maxv,bp[i]);
       }

       mu[ch] = (muv + 8) >> 4;
       min[ch] = minv;
       max[ch] = maxv;
     }

     // determine covariance matrix
     sInt cov[6];
     for(sInt i=0;i<6;i++)
       cov[i] = 0;

     for(sInt i=0;i<16;i++)
     {
       sInt r = block[i].r - mu[2];
       sInt g = block[i].g - mu[1];
       sInt b = block[i].b - mu[0];

       cov[0] += r*r;
       cov[1] += r*g;
       cov[2] += r*b;
       cov[3] += g*g;
       cov[4] += g*b;
       cov[5] += b*b;
     }

     // convert covariance matrix to float, find principal axis via power iter
     sF32 covf[6],vfr,vfg,vfb;
     for(sInt i=0;i<6;i++)
       covf[i] = cov[i] / 255.0f;

     vfr = max[2] - min[2];
     vfg = max[1] - min[1];
     vfb = max[0] - min[0];

     for(sInt iter=0;iter<nIterPower;iter++)
     {
       sF32 r = vfr*covf[0] + vfg*covf[1] + vfb*covf[2];
       sF32 g = vfr*covf[1] + vfg*covf[3] + vfb*covf[4];
       sF32 b = vfr*covf[2] + vfg*covf[4] + vfb*covf[5];

       vfr = r;
       vfg = g;
       vfb = b;
     }

     sF32 magn = sMax(sMax(sFAbs(vfr),sFAbs(vfg)),sFAbs(vfb));
     sInt v_r,v_g,v_b;

     if(magn < 4.0f) // too small, default to luminance
     {
       v_r = 148;
       v_g = 300;
       v_b = 58;
     }
     else
     {
       magn = 512.0f / magn;
       v_r = vfr * magn;
       v_g = vfg * magn;
       v_b = vfb * magn;
     }

     // Pick colors at extreme points
     sInt mind = 0x7fffffff,maxd = -0x7fffffff;
     Pixel minp,maxp;

     for(sInt i=0;i<16;i++)
     {
       sInt dot = block[i].r*v_r + block[i].g*v_g + block[i].b*v_b;

       if(dot < mind)
       {
         mind = dot;
         minp = block[i];
       }

       if(dot > maxd)
       {
         maxd = dot;
         maxp = block[i];
       }
     }

     // Reduce to 16 bit colors
     max16 = maxp.As16Bit();
     min16 = minp.As16Bit();
   }

   // The refinement function. (Clever code, part 2)
   // Tries to optimize colors to suit block contents better.
   // (By solving a least squares system via normal equations+Cramer's rule)
   static sBool RefineBlock(const Pixel *block,sU16 &max16,sU16 &min16,sU32 mask)
   {
     static const sInt w1Tab[4] = { 3,0,2,1 };
     static const sInt prods[4] = { 0x090000,0x000900,0x040102,0x010402 };
     // ^some magic to save a lot of multiplies in the accumulating loop...

     sInt akku = 0;
     sInt At1_r,At1_g,At1_b;
     sInt At2_r,At2_g,At2_b;
     sU32 cm = mask;

     At1_r = At1_g = At1_b = 0;
     At2_r = At2_g = At2_b = 0;
     for(sInt i=0;i<16;i++,cm>>=2)
     {
       sInt step = cm&3;
       sInt w1 = w1Tab[step];
       sInt r = block[i].r;
       sInt g = block[i].g;
       sInt b = block[i].b;

       akku    += prods[step];
       At1_r   += w1*r;
       At1_g   += w1*g;
       At1_b   += w1*b;
       At2_r   += r;
       At2_g   += g;
       At2_b   += b;
     }

     At2_r = 3*At2_r - At1_r;
     At2_g = 3*At2_g - At1_g;
     At2_b = 3*At2_b - At1_b;

     // extract solutions and decide solvability
     sInt xx = akku >> 16;
     sInt yy = (akku >> 8) & 0xff;
     sInt xy = (akku >> 0) & 0xff;

     if(!yy || !xx || xx*yy == xy*xy)
       return sFALSE;

     sF32 frb = 3.0f * 31.0f / 255.0f / (xx*yy - xy*xy);
     sF32 fg = frb * 63.0f / 31.0f;

     sU16 oldMin = min16;
     sU16 oldMax = max16;

     // solve.
     max16 =   sClamp<sInt>((At1_r*yy - At2_r*xy)*frb+0.5f,0,31) << 11;
     max16 |=  sClamp<sInt>((At1_g*yy - At2_g*xy)*fg +0.5f,0,63) << 5;
     max16 |=  sClamp<sInt>((At1_b*yy - At2_b*xy)*frb+0.5f,0,31) << 0;

     min16 =   sClamp<sInt>((At2_r*xx - At1_r*xy)*frb+0.5f,0,31) << 11;
     min16 |=  sClamp<sInt>((At2_g*xx - At1_g*xy)*fg +0.5f,0,63) << 5;
     min16 |=  sClamp<sInt>((At2_b*xx - At1_b*xy)*frb+0.5f,0,31) << 0;

     return oldMin != min16 || oldMax != max16;
   }

   // Color block compression
   static void CompressColorBlock(sU8 *dest,const sU32 *src,sInt quality)
   {
     const Pixel *block = (const Pixel *) src;
     Pixel dblock[16],color[4];

     // check if block is constant
     sU32 min,max;
     min = max = block[0].v;

     for(sInt i=1;i<16;i++)
     {
       min = sMin(min,block[i].v);
       max = sMax(max,block[i].v);
     }

     // perform block compression
     sU16 min16,max16;
     sU32 mask;

     if(min != max) // no constant color
     {
       // first step: compute dithered version for PCA if desired
       if(quality)
         DitherBlock(dblock,block);

       // second step: pca+map along principal axis
       OptimizeColorsBlock(quality ? dblock : block,max16,min16);
       if(max16 != min16)
       {
         EvalColors(color,max16,min16);
         mask = MatchColorsBlock(block,color,quality != 0);
       }
       else
         mask = 0;

       // third step: refine
       if(RefineBlock(quality ? dblock : block,max16,min16,mask))
       {
         if(max16 != min16)
         {
           EvalColors(color,max16,min16);
           mask = MatchColorsBlock(block,color,quality != 0);
         }
         else
           mask = 0;
       }

     }
     else // constant color
     {
       sInt r = block[0].r;
       sInt g = block[0].g;
       sInt b = block[0].b;

       mask  = 0xaaaaaaaa;
       max16 = (OMatch5[r][0]<<11) | (OMatch6[g][0]<<5) | OMatch5[b][0];
       min16 = (OMatch5[r][1]<<11) | (OMatch6[g][1]<<5) | OMatch5[b][1];
     }

     // write the color block
     if(max16 < min16)
     {
       sSwap(max16,min16);
       mask ^= 0x55555555;
     }

     ((sU16 *) dest)[0] = max16;
     ((sU16 *) dest)[1] = min16;
     ((sU32 *) dest)[1] = mask;
   }

   // Alpha block compression (this is easy for a change)
   static void CompressAlphaBlock(sU8 *dest,const sU32 *src,sInt quality)
   {
     quality;
     const Pixel *block = (const Pixel *) src;

     // find min/max color
     sInt min,max;
     min = max = block[0].a;

     for(sInt i=1;i<16;i++)
     {
       min = sMin<sInt>(min,block[i].a);
       max = sMax<sInt>(max,block[i].a);
     }

     // encode them
     *dest++ = max;
     *dest++ = min;

     // determine bias and emit color indices
     sInt dist = max-min;
     sInt bias = min*7 - (dist >> 1);
     sInt dist4 = dist*4;
     sInt dist2 = dist*2;
     sInt bits = 0,mask=0;

     for(sInt i=0;i<16;i++)
     {
       sInt a = block[i].a*7 - bias;
       sInt ind,t;

       // select index (hooray for bit magic)
       t = (dist4 - a) >> 31;  ind =  t & 4; a -= dist4 & t;
       t = (dist2 - a) >> 31;  ind += t & 2; a -= dist2 & t;
       t = (dist - a) >> 31;   ind += t & 1;

       ind = -ind & 7;
       ind ^= (2 > ind);

       // write index
       mask |= ind << bits;
       if((bits += 3) >= 8)
       {
         *dest++ = mask;
         mask >>= 8;
         bits -= 8;
       }
     }
   }

   /****************************************************************************/

   void sInitDXT()
   {
     for(sInt i=0;i<32;i++)
       Expand5[i] = (i<<3)|(i>>2);

     for(sInt i=0;i<64;i++)
       Expand6[i] = (i<<2)|(i>>4);

     for(sInt i=0;i<256+16;i++)
     {
       sInt v = sClamp(i-8,0,255);
       QuantRBTab[i] = Expand5[Mul8Bit(v,31)];
       QuantGTab[i] = Expand6[Mul8Bit(v,63)];
     }

     PrepareOptTable4(&OMatch5[0][0],Expand5,32);
     PrepareOptTable4(&OMatch6[0][0],Expand6,64);

     PrepareOptTable3(&OMatch5_3[0][0],Expand5,32);
     PrepareOptTable3(&OMatch6_3[0][0],Expand6,64);
   }

   void sCompressDXTBlock(sU8 *dest,const sU32 *src,sBool alpha,sInt quality)
   {
     CRNLIB_ASSERT(Expand5[1]);

     // if alpha specified, compress alpha as well
     if(alpha)
     {
       CompressAlphaBlock(dest,src,quality);
       dest += 8;
     }

     // compress the color part
     CompressColorBlock(dest,src,quality);
   }

   void sCompressDXT5ABlock(sU8 *dest,const sU32 *src,sInt quality)
   {
      CRNLIB_ASSERT(Expand5[1]);

      CompressAlphaBlock(dest,src,quality);
   }

} // namespace ryg_dxt

