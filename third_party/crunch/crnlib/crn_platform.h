// File: crn_platform.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

bool crnlib_is_debugger_present(void);
void crnlib_debug_break(void);
void crnlib_output_debug_string(const char* p);

// actually in crnlib_assert.cpp
void crnlib_assert(const char* pExp, const char* pFile, unsigned line);
void crnlib_fail(const char* pExp, const char* pFile, unsigned line);

#if CRNLIB_LITTLE_ENDIAN_CPU
   const bool c_crnlib_little_endian_platform = true;
#else
   const bool c_crnlib_little_endian_platform = false;
#endif

const bool c_crnlib_big_endian_platform = !c_crnlib_little_endian_platform;

#ifdef __GNUC__
   #define crn_fopen(pDstFile, f, m) *(pDstFile) = fopen64(f, m)
   #define crn_fseek fseeko64
   #define crn_ftell ftello64
#elif defined( _MSC_VER )
   #define crn_fopen(pDstFile, f, m) fopen_s(pDstFile, f, m)
   #define crn_fseek _fseeki64
   #define crn_ftell _ftelli64
#else
   #define crn_fopen(pDstFile, f, m) *(pDstFile) = fopen(f, m)
   #define crn_fseek(s, o, w) fseek(s, static_cast<long>(o), w)
   #define crn_ftell ftell
#endif

#if CRNLIB_USE_WIN32_API
   #define CRNLIB_BREAKPOINT DebugBreak();
   #define CRNLIB_BUILTIN_EXPECT(c, v) c
#elif defined(__GNUC__)
   #define CRNLIB_BREAKPOINT asm("int $3");
   #define CRNLIB_BUILTIN_EXPECT(c, v) __builtin_expect(c, v)
#else
   #define CRNLIB_BREAKPOINT
   #define CRNLIB_BUILTIN_EXPECT(c, v) c
#endif

#if defined(__GNUC__)
   #define CRNLIB_ALIGNED(x) __attribute__((aligned(x)))
   #define CRNLIB_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
   #define CRNLIB_ALIGNED(x) __declspec(align(x))
   #define CRNLIB_NOINLINE __declspec(noinline) 
#else
   #define CRNLIB_ALIGNED(x)
   #define CRNLIB_NOINLINE
#endif

#define CRNLIB_GET_ALIGNMENT(v) ((!sizeof(v)) ? 1 : (__alignof(v) ? __alignof(v) : sizeof(uint32)))

#ifndef _MSC_VER
   int sprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, ...);
   int vsprintf_s(char *buffer, size_t sizeOfBuffer, const char *format, va_list args);
   char* strlwr(char* p);
   char* strupr(char *p);
   #define _stricmp strcasecmp
   #define _strnicmp strncasecmp
#endif

inline bool crnlib_is_little_endian() { return c_crnlib_little_endian_platform; }
inline bool crnlib_is_big_endian() { return c_crnlib_big_endian_platform; }

inline bool crnlib_is_pc()
{
#ifdef CRNLIB_PLATFORM_PC
   return true;
#else
   return false;
#endif
}

inline bool crnlib_is_x86()
{
#ifdef CRNLIB_PLATFORM_PC_X86
   return true;
#else
   return false;
#endif
}

inline bool crnlib_is_x64()
{
#ifdef CRNLIB_PLATFORM_PC_X64
   return true;
#else
   return false;
#endif
}
