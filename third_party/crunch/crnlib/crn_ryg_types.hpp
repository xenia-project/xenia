// File: ryg_types.hpp
#pragma once
#ifndef __TP_TYPES_HPP__
#define __TP_TYPES_HPP__

#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>

#ifdef _MSC_VER                              // microsoft C++

#define sCONFIG_NATIVEINT         int _w64                // sDInt: an int of the same size as a pointer
#define sCONFIG_INT64             __int64                 // sS64, sU64: a 64 bit int

#define sINLINE                   __forceinline           // use this to inline

#endif

#ifdef __GNUC__                             // GNU C++

#define sCONFIG_NATIVEINT         int
#define sCONFIG_INT64             long long

#define sINLINE                   __inline__

#endif

/****************************************************************************/
/***                                                                      ***/
/***   Basic Types and Functions                                          ***/
/***                                                                      ***/
/****************************************************************************/

typedef unsigned char             sU8;      // for packed arrays
typedef unsigned short            sU16;     // for packed arrays
typedef unsigned int              sU32;     // for packed arrays and bitfields
typedef unsigned sCONFIG_INT64    sU64;     // use as needed
typedef signed char               sS8;      // for packed arrays
typedef short                     sS16;     // for packed arrays
typedef int                       sS32;     // for packed arrays
typedef signed sCONFIG_INT64      sS64;     // use as needed
typedef float                     sF32;     // basic floatingpoint
typedef double                    sF64;     // use as needed
typedef int                       sInt;     // use this most!
typedef signed sCONFIG_NATIVEINT  sDInt;    // type for pointer diff
typedef bool                      sBool;    // use for boolean function results

/****************************************************************************/

#define sTRUE   true
#define sFALSE  false

/****************************************************************************/

template <class Type> sINLINE Type sMin(Type a,Type b)              {return (a<b) ? a : b;}
template <class Type> sINLINE Type sMax(Type a,Type b)              {return (a>b) ? a : b;}
template <class Type> sINLINE Type sSign(Type a)                    {return (a==0) ? Type(0) : (a>0) ? Type(1) : Type(-1);}
template <class Type> sINLINE Type sClamp(Type a,Type min,Type max) {return (a>=max) ? max : (a<=min) ? min : a;}
template <class Type> sINLINE void sSwap(Type &a,Type &b)           {Type s; s=a; a=b; b=s;}
template <class Type> sINLINE Type sAlign(Type a,sInt b)            {return (Type)((((sDInt)a)+b-1)&(~(b-1)));}

template <class Type> sINLINE Type sSquare(Type a)                  {return a*a;}

/****************************************************************************/

#define sPI     3.1415926535897932384626433832795
#define sPI2    6.28318530717958647692528676655901
#define sPIF    3.1415926535897932384626433832795f
#define sPI2F   6.28318530717958647692528676655901f
#define sSQRT2  1.4142135623730950488016887242097
#define sSQRT2F 1.4142135623730950488016887242097f

sINLINE sInt sAbs(sInt i)                                  { return abs(i); }
sINLINE void sSetMem(void *dd,sInt s,sInt c)               { memset(dd,s,c); }
sINLINE void sCopyMem(void *dd,const void *ss,sInt c)      { memcpy(dd,ss,c); }
sINLINE sInt sCmpMem(const void *dd,const void *ss,sInt c) { return (sInt)memcmp(dd,ss,c); }

sINLINE sF64 sFATan(sF64 f)         { return atan(f); }
sINLINE sF64 sFATan2(sF64 a,sF64 b) { return atan2(a,b); }
sINLINE sF64 sFCos(sF64 f)          { return cos(f); }
sINLINE sF64 sFAbs(sF64 f)          { return fabs(f); }
sINLINE sF64 sFLog(sF64 f)          { return log(f); }
sINLINE sF64 sFLog10(sF64 f)        { return log10(f); }
sINLINE sF64 sFSin(sF64 f)          { return sin(f); }
sINLINE sF64 sFSqrt(sF64 f)         { return sqrt(f); }
sINLINE sF64 sFTan(sF64 f)          { return tan(f); }

sINLINE sF64 sFACos(sF64 f)         { return acos(f); }
sINLINE sF64 sFASin(sF64 f)         { return asin(f); }
sINLINE sF64 sFCosH(sF64 f)         { return cosh(f); }
sINLINE sF64 sFSinH(sF64 f)         { return sinh(f); }
sINLINE sF64 sFTanH(sF64 f)         { return tanh(f); }

sINLINE sF64 sFInvSqrt(sF64 f)      { return 1.0/sqrt(f); }

sINLINE sF64 sFFloor(sF64 f)        { return floor(f); }

sINLINE sF64 sFPow(sF64 a,sF64 b)   { return pow(a,b); }
sINLINE sF64 sFMod(sF64 a,sF64 b)   { return fmod(a,b); }
sINLINE sF64 sFExp(sF64 f)          { return exp(f); }

/****************************************************************************/
/***                                                                      ***/
/***   Debugging                                                          ***/
/***                                                                      ***/
/****************************************************************************/

#define sVERIFY(x)    {assert(x);}
#define sVERIFYFALSE  {assert(false);}

/****************************************************************************/

#endif