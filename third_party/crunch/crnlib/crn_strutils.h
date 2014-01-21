// File: crn_strutils.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#ifdef WIN32
   #define CRNLIB_PATH_SEPERATOR_CHAR '\\'
#else
   #define CRNLIB_PATH_SEPERATOR_CHAR '/'
#endif

namespace crnlib
{
   char* crn_strdup(const char* pStr);
   int crn_stricmp(const char *p, const char *q);

   char* strcpy_safe(char* pDst, uint dst_len, const char* pSrc);

   bool int_to_string(int value, char* pDst, uint len);
   bool uint_to_string(uint value, char* pDst, uint len);

   bool string_to_int(const char*& pBuf, int& value);

   bool string_to_uint(const char*& pBuf, uint& value);

   bool string_to_int64(const char*& pBuf, int64& value);
   bool string_to_uint64(const char*& pBuf, uint64& value);

   bool string_to_bool(const char* p, bool& value);

   bool string_to_float(const char*& p, float& value, uint round_digit = 512U);

   bool string_to_double(const char*& p, double& value, uint round_digit = 512U);
   bool string_to_double(const char*& p, const char *pEnd, double& value, uint round_digit = 512U);

} // namespace crnlib
