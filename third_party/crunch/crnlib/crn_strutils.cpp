// File: crn_strutils.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_strutils.h"

namespace crnlib
{
   char* crn_strdup(const char* pStr)
   {
      if (!pStr)
         pStr = "";

      size_t l = strlen(pStr) + 1;
      char *p = (char *)crnlib_malloc(l);
      if (p)
         memcpy(p, pStr, l);

      return p;
   }

   int crn_stricmp(const char *p, const char *q)
   {
      return _stricmp(p, q);
   }

   char* strcpy_safe(char* pDst, uint dst_len, const char* pSrc)
   {
      CRNLIB_ASSERT(pDst && pSrc && dst_len);
      if (!dst_len)
         return pDst;

      char* q = pDst;
      char c;

      do
      {
         if (dst_len == 1)
         {
            *q++ = '\0';
            break;
         }

         c = *pSrc++;
         *q++ = c;

         dst_len--;

      } while (c);

      CRNLIB_ASSERT((q - pDst) <= (int)dst_len);

      return pDst;
   }

   bool int_to_string(int value, char* pDst, uint len)
   {
      CRNLIB_ASSERT(pDst);

      const uint cBufSize = 16;
      char buf[cBufSize];

      uint j = static_cast<uint>((value < 0) ? -value : value);

      char* p = buf + cBufSize - 1;

      *p-- = '\0';

      do
      {
         *p-- = static_cast<uint8>('0' + (j % 10));
         j /= 10;
      } while (j);

      if (value < 0)
         *p-- = '-';

      const size_t total_bytes = (buf + cBufSize - 1) - p;
      if (total_bytes > len)
         return false;

      for (size_t i = 0; i < total_bytes; i++)
         pDst[i] = p[1 + i];

      return true;
   }

   bool uint_to_string(uint value, char* pDst, uint len)
   {
      CRNLIB_ASSERT(pDst);

      const uint cBufSize = 16;
      char buf[cBufSize];

      char* p = buf + cBufSize - 1;

      *p-- = '\0';

      do
      {
         *p-- = static_cast<uint8>('0' + (value % 10));
         value /= 10;
      } while (value);

      const size_t total_bytes = (buf + cBufSize - 1) - p;
      if (total_bytes > len)
         return false;

      for (size_t i = 0; i < total_bytes; i++)
         pDst[i] = p[1 + i];

      return true;
   }

   bool string_to_int(const char*& pBuf, int& value)
   {
      value = 0;

      CRNLIB_ASSERT(pBuf);
      const char* p = pBuf;

      while (*p && isspace(*p))
         p++;

      uint result = 0;
      bool negative = false;

      if (!isdigit(*p))
      {
         if (p[0] == '-')
         {
            negative = true;
            p++;
         }
         else
            return false;
      }

      while (*p && isdigit(*p))
      {
         if (result & 0xE0000000U)
            return false;

         const uint result8 = result << 3U;
         const uint result2 = result << 1U;

         if (result2 > (0xFFFFFFFFU - result8))
            return false;

         result = result8 + result2;

         uint c = p[0] - '0';
         if (c > (0xFFFFFFFFU - result))
            return false;

         result += c;

         p++;
      }

      if (negative)
      {
         if (result > 0x80000000U)
         {
            value = 0;
            return false;
         }
         value = -static_cast<int>(result);
      }
      else
      {
         if (result > 0x7FFFFFFFU)
         {
            value = 0;
            return false;
         }
         value = static_cast<int>(result);
      }

      pBuf = p;

      return true;
   }

   bool string_to_int64(const char*& pBuf, int64& value)
   {
      value = 0;

      CRNLIB_ASSERT(pBuf);
      const char* p = pBuf;

      while (*p && isspace(*p))
         p++;

      uint64 result = 0;
      bool negative = false;

      if (!isdigit(*p))
      {
         if (p[0] == '-')
         {
            negative = true;
            p++;
         }
         else
            return false;
      }

      while (*p && isdigit(*p))
      {
         if (result & 0xE000000000000000ULL)
            return false;

         const uint64 result8 = result << 3U;
         const uint64 result2 = result << 1U;

         if (result2 > (0xFFFFFFFFFFFFFFFFULL - result8))
            return false;

         result = result8 + result2;

         uint c = p[0] - '0';
         if (c > (0xFFFFFFFFFFFFFFFFULL - result))
            return false;

         result += c;

         p++;
      }

      if (negative)
      {
         if (result > 0x8000000000000000ULL)
         {
            value = 0;
            return false;
         }
         value = -static_cast<int64>(result);
      }
      else
      {
         if (result > 0x7FFFFFFFFFFFFFFFULL)
         {
            value = 0;
            return false;
         }
         value = static_cast<int64>(result);
      }

      pBuf = p;

      return true;
   }

   bool string_to_uint(const char*& pBuf, uint& value)
   {
      value = 0;

      CRNLIB_ASSERT(pBuf);
      const char* p = pBuf;

      while (*p && isspace(*p))
         p++;

      uint result = 0;

      if (!isdigit(*p))
         return false;

      while (*p && isdigit(*p))
      {
         if (result & 0xE0000000U)
            return false;

         const uint result8 = result << 3U;
         const uint result2 = result << 1U;

         if (result2 > (0xFFFFFFFFU - result8))
            return false;

         result = result8 + result2;

         uint c = p[0] - '0';
         if (c > (0xFFFFFFFFU - result))
            return false;

         result += c;

         p++;
      }

      value = result;

      pBuf = p;

      return true;
   }

   bool string_to_uint64(const char*& pBuf, uint64& value)
   {
      value = 0;

      CRNLIB_ASSERT(pBuf);
      const char* p = pBuf;

      while (*p && isspace(*p))
         p++;

      uint64 result = 0;

      if (!isdigit(*p))
         return false;

      while (*p && isdigit(*p))
      {
         if (result & 0xE000000000000000ULL)
            return false;

         const uint64 result8 = result << 3U;
         const uint64 result2 = result << 1U;

         if (result2 > (0xFFFFFFFFFFFFFFFFULL - result8))
            return false;

         result = result8 + result2;

         uint c = p[0] - '0';
         if (c > (0xFFFFFFFFFFFFFFFFULL - result))
            return false;

         result += c;

         p++;
      }

      value = result;

      pBuf = p;

      return true;
   }

   bool string_to_bool(const char* p, bool& value)
   {
      CRNLIB_ASSERT(p);

      value = false;

      if (_stricmp(p, "false") == 0)
         return true;

      if (_stricmp(p, "true") == 0)
      {
         value = true;
         return true;
      }

      const char* q = p;
      uint v;
      if (string_to_uint(q, v))
      {
         if (!v)
            return true;
         else if (v == 1)
         {
            value = true;
            return true;
         }
      }

      return false;
   }

   bool string_to_float(const char*& p, float& value, uint round_digit)
   {
      double d;
      if (!string_to_double(p, d, round_digit))
      {
         value = 0;
         return false;
      }
      value = static_cast<float>(d);
      return true;
   }

   bool string_to_double(const char*& p, double& value, uint round_digit)
   {
      return string_to_double(p, p + 128, value, round_digit);
   }

   // I wrote this approx. 20 years ago in C/assembly using a limited FP emulator package, so it's a bit crude.
   bool string_to_double(const char*& p, const char *pEnd, double& value, uint round_digit)
   {
      CRNLIB_ASSERT(p);

      value = 0;

      enum { AF_BLANK = 1, AF_SIGN = 2, AF_DPOINT = 3, AF_BADCHAR = 4, AF_OVRFLOW = 5, AF_EXPONENT = 6, AF_NODIGITS = 7 };
      int status = 0;

      const char* buf = p;

      int got_sign_flag = 0, got_dp_flag   = 0, got_num_flag  = 0;
      int got_e_flag = 0, got_e_sign_flag = 0, e_sign = 0;
      uint whole_count = 0, frac_count = 0;

      double whole = 0, frac = 0, scale = 1, exponent = 1;

      if (p >= pEnd)
      {
         status = AF_NODIGITS;
         goto af_exit;
      }

      while (*buf)
      {
         if (!isspace(*buf))
            break;
         if (++buf >= pEnd)
         {
            status = AF_NODIGITS;
            goto af_exit;
         }
      }

      p = buf;

      while (*buf)
      {
         p = buf;
         if (buf >= pEnd)
            break;

         int i = *buf++;

         switch (i)
         {
            case 'e':
            case 'E':
            {
              got_e_flag = 1;
              goto exit_while;
            }
            case '+':
            {
              if ((got_num_flag) || (got_sign_flag))
              {
                status = AF_SIGN;
                goto af_exit;
              }

              got_sign_flag = 1;

              break;
            }
            case '-':
            {
              if ((got_num_flag) || (got_sign_flag))
              {
                status = AF_SIGN;
                goto af_exit;
              }

              got_sign_flag = -1;

              break;
            }
            case '.':
            {
              if (got_dp_flag)
              {
                status = AF_DPOINT;
                goto af_exit;
              }

              got_dp_flag = 1;

              break;
            }
            default:
            {
              if ((i < '0') || (i > '9'))
                goto exit_while;
              else
              {
                i -= '0';

                got_num_flag = 1;

                if (got_dp_flag)
                {
                  if (frac_count < round_digit)
                  {
                    frac = frac * 10.0f + i;

                    scale = scale * 10.0f;
                  }
                  else if (frac_count == round_digit)
                  {
                    if (i >= 5)               /* check for round */
                      frac = frac + 1.0f;
                  }

                  frac_count++;
                }
                else
                {
                  whole = whole * 10.0f + i;

                  whole_count++;

                  if (whole > 1e+100)
                  {
                    status = AF_OVRFLOW;
                    goto af_exit;
                  }
                }
              }

              break;
            }
         }
      }

    exit_while:

      if (got_e_flag)
      {
         if ((got_num_flag == 0) && (got_dp_flag))
         {
            status = AF_EXPONENT;
            goto af_exit;
         }

         int e = 0;
         e_sign = 1;
         got_num_flag = 0;
         got_e_sign_flag = 0;

         while (*buf)
         {
            p = buf;
            if (buf >= pEnd)
               break;

            int i = *buf++;

            if (i == '+')
            {
              if ((got_num_flag) || (got_e_sign_flag))
              {
                status = AF_EXPONENT;
                goto af_exit;
              }

              e_sign = 1;
              got_e_sign_flag = 1;
            }
            else if (i == '-')
            {
              if ((got_num_flag) || (got_e_sign_flag))
              {
                status = AF_EXPONENT;
                goto af_exit;
              }

              e_sign = -1;
              got_e_sign_flag = 1;
            }
            else if ((i >= '0') && (i <= '9'))
            {
              got_num_flag = 1;

              if ((e = (e * 10) + (i - 48)) > 100)
              {
                status = AF_EXPONENT;
                goto af_exit;
              }
            }
            else
              break;
         }

         for (int i = 1; i <= e; i++)   /* compute 10^e */
            exponent = exponent * 10.0f;
      }

      if (((whole_count + frac_count) == 0) && (got_e_flag == 0))
      {
         status = AF_NODIGITS;
         goto af_exit;
      }

      if (frac)
         whole = whole + (frac / scale);

      if (got_e_flag)
      {
         if (e_sign > 0)
            whole = whole * exponent;
         else
            whole = whole / exponent;
      }

      if (got_sign_flag < 0)
         whole = -whole;

      value = whole;

   af_exit:
      return (status == 0);
   }

} // namespace crnlib
