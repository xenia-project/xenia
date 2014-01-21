// File: crn_assert.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

const unsigned int CRNLIB_FAIL_EXCEPTION_CODE = 256U;
void crnlib_enable_fail_exceptions(bool enabled);

void crnlib_assert(const char* pExp, const char* pFile, unsigned line);
void crnlib_fail(const char* pExp, const char* pFile, unsigned line);

#ifdef NDEBUG
   #define CRNLIB_ASSERT(x) ((void)0)
   #undef  CRNLIB_ASSERTS_ENABLED
#else
   #define CRNLIB_ASSERT(_exp) (void)( (!!(_exp)) || (crnlib_assert(#_exp, __FILE__, __LINE__), 0) )
   #define CRNLIB_ASSERTS_ENABLED
#endif

#define CRNLIB_VERIFY(_exp) (void)( (!!(_exp)) || (crnlib_assert(#_exp, __FILE__, __LINE__), 0) )

#define CRNLIB_FAIL(msg) do { crnlib_fail(#msg, __FILE__, __LINE__); } while(0)

#define CRNLIB_ASSERT_OPEN_RANGE(x, l, h) CRNLIB_ASSERT((x >= l) && (x < h))
#define CRNLIB_ASSERT_CLOSED_RANGE(x, l, h) CRNLIB_ASSERT((x >= l) && (x <= h))

void trace(const char* pFmt, va_list args);
void trace(const char* pFmt, ...);

// Borrowed from boost libraries.
template <bool x>  struct crnlib_assume_failure;
template <> struct crnlib_assume_failure<true> { enum { blah = 1 }; };
template<int x> struct crnlib_assume_try { };

#define CRNLIB_JOINER_FINAL(a, b) a##b
#define CRNLIB_JOINER(a, b) CRNLIB_JOINER_FINAL(a, b)
#define CRNLIB_JOIN(a, b) CRNLIB_JOINER(a, b)
#define CRNLIB_ASSUME(p) typedef crnlib_assume_try < sizeof(crnlib_assume_failure< (bool)(p) > ) > CRNLIB_JOIN(crnlib_assume_typedef, __COUNTER__)

#ifdef NDEBUG
template<typename T> inline T crnlib_assert_range(T i, T m)
{
   m;
   return i;
}
template<typename T> inline T crnlib_assert_range_incl(T i, T m)
{
   m;
   return i;
}
#else
template<typename T> inline T crnlib_assert_range(T i, T m)
{
   CRNLIB_ASSERT((i >= 0) && (i < m));
   return i;
}
template<typename T> inline T crnlib_assert_range_incl(T i, T m)
{
   CRNLIB_ASSERT((i >= 0) && (i <= m));
   return i;
}
#endif
