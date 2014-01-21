// File: crn_dynamic_string.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_strutils.h"

namespace crnlib
{
   dynamic_string g_empty_dynamic_string;

   dynamic_string::dynamic_string(eVarArg dummy, const char* p, ...) :
      m_buf_size(0), m_len(0), m_pStr(NULL)
   {
      dummy;

      CRNLIB_ASSERT(p);

      va_list args;
      va_start(args, p);
      format_args(p, args);
      va_end(args);
   }

   dynamic_string::dynamic_string(const char* p) :
      m_buf_size(0), m_len(0), m_pStr(NULL)
   {
      CRNLIB_ASSERT(p);
      set(p);
   }

   dynamic_string::dynamic_string(const char* p, uint len) :
      m_buf_size(0), m_len(0), m_pStr(NULL)
   {
      CRNLIB_ASSERT(p);
      set_from_buf(p, len);
   }

   dynamic_string::dynamic_string(const dynamic_string& other) :
      m_buf_size(0), m_len(0), m_pStr(NULL)
   {
      set(other);
   }

   void dynamic_string::clear()
   {
      check();

      if (m_pStr)
      {
         crnlib_delete_array(m_pStr);
         m_pStr = NULL;

         m_len = 0;
         m_buf_size = 0;
      }
   }

   void dynamic_string::empty()
   {
      truncate(0);
   }

   void dynamic_string::optimize()
   {
      if (!m_len)
         clear();
      else
      {
         uint min_buf_size = math::next_pow2((uint)m_len + 1);
         if (m_buf_size > min_buf_size)
         {
            char* p = crnlib_new_array<char>(min_buf_size);
            memcpy(p, m_pStr, m_len + 1);

            crnlib_delete_array(m_pStr);
            m_pStr = p;

            m_buf_size = static_cast<uint16>(min_buf_size);

            check();
         }
      }
   }

   int dynamic_string::compare(const char* p, bool case_sensitive) const
   {
      CRNLIB_ASSERT(p);

      const int result = (case_sensitive ? strcmp : crn_stricmp)(get_ptr_priv(), p);

      if (result < 0)
         return -1;
      else if (result > 0)
         return 1;

      return 0;
   }

   int dynamic_string::compare(const dynamic_string& rhs, bool case_sensitive) const
   {
      return compare(rhs.get_ptr_priv(), case_sensitive);
   }

   dynamic_string& dynamic_string::set(const char* p, uint max_len)
   {
      CRNLIB_ASSERT(p);

      const uint len = math::minimum<uint>(max_len, static_cast<uint>(strlen(p)));
      CRNLIB_ASSERT(len < cUINT16_MAX);

      if ((!len) || (len >= cUINT16_MAX))
         clear();
      else if ((m_pStr) && (p >= m_pStr) && (p < (m_pStr + m_buf_size)))
      {
         if (m_pStr != p)
            memmove(m_pStr, p, len);
         m_pStr[len] = '\0';
         m_len = static_cast<uint16>(len);
      }
      else if (ensure_buf(len, false))
      {
         m_len = static_cast<uint16>(len);
         memcpy(m_pStr, p, m_len + 1);
      }

      check();

      return *this;
   }

   dynamic_string& dynamic_string::set(const dynamic_string& other, uint max_len)
   {
      if (this == &other)
      {
         if (max_len < m_len)
         {
            m_pStr[max_len] = '\0';
            m_len = static_cast<uint16>(max_len);
         }
      }
      else
      {
         const uint len = math::minimum<uint>(max_len, other.m_len);

         if (!len)
            clear();
         else if (ensure_buf(len, false))
         {
            m_len = static_cast<uint16>(len);
            memcpy(m_pStr, other.get_ptr_priv(), m_len);
            m_pStr[len] = '\0';
         }
      }

      check();

      return *this;
   }

   bool dynamic_string::set_len(uint new_len, char fill_char)
   {
      if ((new_len >= cUINT16_MAX) || (!fill_char))
      {
         CRNLIB_ASSERT(0);
         return false;
      }

      uint cur_len = m_len;

      if (ensure_buf(new_len, true))
      {
         if (new_len > cur_len)
            memset(m_pStr + cur_len, fill_char, new_len - cur_len);

         m_pStr[new_len] = 0;

         m_len = static_cast<uint16>(new_len);

         check();
      }

      return true;
   }

   dynamic_string& dynamic_string::set_from_raw_buf_and_assume_ownership(char *pBuf, uint buf_size_in_chars, uint len_in_chars)
   {
      CRNLIB_ASSERT(buf_size_in_chars <= cUINT16_MAX);
      CRNLIB_ASSERT(math::is_power_of_2(buf_size_in_chars) || (buf_size_in_chars == cUINT16_MAX));
      CRNLIB_ASSERT((len_in_chars + 1) <= buf_size_in_chars);

      clear();

      m_pStr = pBuf;
      m_buf_size = static_cast<uint16>(buf_size_in_chars);
      m_len = static_cast<uint16>(len_in_chars);

      check();

      return *this;
   }

   dynamic_string& dynamic_string::set_from_buf(const void* pBuf, uint buf_size)
   {
      CRNLIB_ASSERT(pBuf);

      if (buf_size >= cUINT16_MAX)
      {
         clear();
         return *this;
      }

#ifdef CRNLIB_BUILD_DEBUG
      if ((buf_size) && (memchr(pBuf, 0, buf_size) != NULL))
      {
         CRNLIB_ASSERT(0);
         clear();
         return *this;
      }
#endif

      if (ensure_buf(buf_size, false))
      {
         if (buf_size)
            memcpy(m_pStr, pBuf, buf_size);

         m_pStr[buf_size] = 0;

         m_len = static_cast<uint16>(buf_size);

         check();
      }

      return *this;
   }

   dynamic_string& dynamic_string::set_char(uint index, char c)
   {
      CRNLIB_ASSERT(index <= m_len);

      if (!c)
         truncate(index);
      else if (index < m_len)
      {
         m_pStr[index] = c;

         check();
      }
      else if (index == m_len)
         append_char(c);

      return *this;
   }

   dynamic_string& dynamic_string::append_char(char c)
   {
      if (ensure_buf(m_len + 1))
      {
         m_pStr[m_len] = c;
         m_pStr[m_len + 1] = '\0';
         m_len++;
         check();
      }

      return *this;
   }

   dynamic_string& dynamic_string::truncate(uint new_len)
   {
      if (new_len < m_len)
      {
         m_pStr[new_len] = '\0';
         m_len = static_cast<uint16>(new_len);
         check();
      }
      return *this;
   }

   dynamic_string& dynamic_string::tolower()
   {
      if (m_len)
      {
#ifdef _MSC_VER
         _strlwr_s(get_ptr_priv(), m_buf_size);
#else
         strlwr(get_ptr_priv());
#endif
      }
      return *this;
   }

   dynamic_string& dynamic_string::toupper()
   {
      if (m_len)
      {
#ifdef _MSC_VER
         _strupr_s(get_ptr_priv(), m_buf_size);
#else
         strupr(get_ptr_priv());
#endif
      }
      return *this;
   }

   dynamic_string& dynamic_string::append(const char* p)
   {
      CRNLIB_ASSERT(p);

      uint len = static_cast<uint>(strlen(p));
      uint new_total_len = m_len + len;
      if ((new_total_len) && ensure_buf(new_total_len))
      {
         memcpy(m_pStr + m_len, p, len + 1);
         m_len = static_cast<uint16>(m_len + len);
         check();
      }

      return *this;
   }

   dynamic_string& dynamic_string::append(const dynamic_string& other)
   {
      uint len = other.m_len;
      uint new_total_len = m_len + len;
      if ((new_total_len) && ensure_buf(new_total_len))
      {
         memcpy(m_pStr + m_len, other.get_ptr_priv(), len + 1);
         m_len = static_cast<uint16>(m_len + len);
         check();
      }

      return *this;
   }

   dynamic_string operator+ (const char* p, const dynamic_string& a)
   {
      return dynamic_string(p).append(a);
   }

   dynamic_string operator+ (const dynamic_string& a, const char* p)
   {
      return dynamic_string(a).append(p);
   }

   dynamic_string operator+ (const dynamic_string& a, const dynamic_string& b)
   {
      return dynamic_string(a).append(b);
   }

   dynamic_string& dynamic_string::format_args(const char* p, va_list args)
   {
      CRNLIB_ASSERT(p);

      const uint cBufSize = 4096;
      char buf[cBufSize];

#ifdef _MSC_VER
      int l = vsnprintf_s(buf, cBufSize, _TRUNCATE, p, args);
#else
      int l = vsnprintf(buf, cBufSize, p, args);
#endif
      if (l <= 0)
         clear();
      else if (ensure_buf(l, false))
      {
         memcpy(m_pStr, buf, l + 1);

         m_len = static_cast<uint16>(l);

         check();
      }

      return *this;
   }

   dynamic_string& dynamic_string::format(const char* p, ...)
   {
      CRNLIB_ASSERT(p);

      va_list args;
      va_start(args, p);
      format_args(p, args);
      va_end(args);
      return *this;
   }

   dynamic_string& dynamic_string::crop(uint start, uint len)
   {
      if (start >= m_len)
      {
         clear();
         return *this;
      }

      len = math::minimum<uint>(len, m_len - start);

      if (start)
         memmove(get_ptr_priv(), get_ptr_priv() + start, len);

      m_pStr[len] = '\0';

      m_len = static_cast<uint16>(len);

      check();

      return *this;
   }

   dynamic_string& dynamic_string::substring(uint start, uint end)
   {
      CRNLIB_ASSERT(start <= end);
      if (start > end)
         return *this;
      return crop(start, end - start);
   }

   dynamic_string& dynamic_string::left(uint len)
   {
      return substring(0, len);
   }

   dynamic_string& dynamic_string::mid(uint start, uint len)
   {
      return crop(start, len);
   }

   dynamic_string& dynamic_string::right(uint start)
   {
      return substring(start, get_len());
   }

   dynamic_string& dynamic_string::tail(uint num)
   {
      return substring(math::maximum<int>(static_cast<int>(get_len()) - static_cast<int>(num), 0), get_len());
   }

   dynamic_string& dynamic_string::unquote()
   {
      if (m_len >= 2)
      {
         if ( ((*this)[0] == '\"') && ((*this)[m_len - 1] == '\"') )
         {
            return mid(1, m_len - 2);
         }
      }

      return *this;
   }

   int dynamic_string::find_left(const char* p, bool case_sensitive) const
   {
      CRNLIB_ASSERT(p);

      const int p_len = (int)strlen(p);

      for (int i = 0; i <= (m_len - p_len); i++)
         if ((case_sensitive ? strncmp : _strnicmp)(p, &m_pStr[i], p_len) == 0)
            return i;

      return -1;
   }

   bool dynamic_string::contains(const char* p, bool case_sensitive) const
   {
      return find_left(p, case_sensitive) >= 0;
   }

   uint dynamic_string::count_char(char c) const
   {
      uint count = 0;
      for (uint i = 0; i < m_len; i++)
         if (m_pStr[i] == c)
            count++;
      return count;
   }

   int dynamic_string::find_left(char c) const
   {
      for (uint i = 0; i < m_len; i++)
         if (m_pStr[i] == c)
            return i;
      return -1;
   }

   int dynamic_string::find_right(char c) const
   {
      for (int i = (int)m_len - 1; i >= 0; i--)
         if (m_pStr[i] == c)
            return i;
      return -1;
   }

   int dynamic_string::find_right(const char* p, bool case_sensitive) const
   {
      CRNLIB_ASSERT(p);
      const int p_len = (int)strlen(p);

      for (int i = m_len - p_len; i >= 0; i--)
         if ((case_sensitive ? strncmp : _strnicmp)(p, &m_pStr[i], p_len) == 0)
            return i;

      return -1;
   }

   dynamic_string& dynamic_string::trim()
   {
      int s, e;
      for (s = 0; s < (int)m_len; s++)
         if (!isspace(m_pStr[s]))
            break;

      for (e = m_len - 1; e > s; e--)
         if (!isspace(m_pStr[e]))
            break;

      return crop(s, e - s + 1);
   }

   dynamic_string& dynamic_string::trim_crlf()
   {
      int s = 0, e;

      for (e = m_len - 1; e > s; e--)
         if ((m_pStr[e] != 13) && (m_pStr[e] != 10))
            break;

      return crop(s, e - s + 1);
   }

   dynamic_string& dynamic_string::remap(int from_char, int to_char)
   {
      for (uint i = 0; i < m_len; i++)
         if (m_pStr[i] == from_char)
            m_pStr[i] = (char)to_char;
      return *this;
   }

#ifdef CRNLIB_BUILD_DEBUG
   void dynamic_string::check() const
   {
      if (!m_pStr)
      {
         CRNLIB_ASSERT(!m_buf_size && !m_len);
      }
      else
      {
         CRNLIB_ASSERT(m_buf_size);
         CRNLIB_ASSERT((m_buf_size == cUINT16_MAX) || math::is_power_of_2((uint32)m_buf_size));
         CRNLIB_ASSERT(m_len < m_buf_size);
         CRNLIB_ASSERT(!m_pStr[m_len]);
#if CRNLIB_SLOW_STRING_LEN_CHECKS
         CRNLIB_ASSERT(strlen(m_pStr) == m_len);
#endif
      }
   }
#endif

   bool dynamic_string::ensure_buf(uint len, bool preserve_contents)
   {
      uint buf_size_needed = len + 1;

      CRNLIB_ASSERT(buf_size_needed <= cUINT16_MAX);

      if (buf_size_needed <= cUINT16_MAX)
      {
         if (buf_size_needed > m_buf_size)
            expand_buf(buf_size_needed, preserve_contents);
      }

      return m_buf_size >= buf_size_needed;
   }

   bool dynamic_string::expand_buf(uint new_buf_size, bool preserve_contents)
   {
      new_buf_size = math::minimum<uint>(cUINT16_MAX, math::next_pow2(math::maximum<uint>(m_buf_size, new_buf_size)));

      if (new_buf_size != m_buf_size)
      {
         char* p = crnlib_new_array<char>(new_buf_size);

         if (preserve_contents)
            memcpy(p, get_ptr_priv(), m_len + 1);

         crnlib_delete_array(m_pStr);
         m_pStr = p;

         m_buf_size = static_cast<uint16>(new_buf_size);

         if (preserve_contents)
            check();
      }

      return m_buf_size >= new_buf_size;
   }

   void dynamic_string::swap(dynamic_string& other)
   {
      utils::swap(other.m_buf_size, m_buf_size);
      utils::swap(other.m_len, m_len);
      utils::swap(other.m_pStr, m_pStr);
   }

   int dynamic_string::serialize(void* pBuf, uint buf_size, bool little_endian) const
   {
      uint buf_left = buf_size;

      //if (m_len > cUINT16_MAX)
      //   return -1;
      CRNLIB_ASSUME(sizeof(m_len) == sizeof(uint16));

      if (!utils::write_val((uint16)m_len, pBuf, buf_left, little_endian))
         return -1;

      if (buf_left < m_len)
         return -1;

      memcpy(pBuf, get_ptr(), m_len);

      buf_left -= m_len;

      return buf_size - buf_left;
   }

   int dynamic_string::deserialize(const void* pBuf, uint buf_size, bool little_endian)
   {
      uint buf_left = buf_size;

      if (buf_left < sizeof(uint16)) return -1;

      uint16 l;
      if (!utils::read_obj(l, pBuf, buf_left, little_endian))
         return -1;

      if (buf_left < l)
         return -1;

      set_from_buf(pBuf, l);

      buf_left -= l;

      return buf_size - buf_left;
   }

   void dynamic_string::translate_lf_to_crlf()
   {
      if (find_left(0x0A) < 0)
         return;

      dynamic_string tmp;
      tmp.ensure_buf(m_len + 2);

      // normal sequence is 0x0D 0x0A (CR LF, \r\n)

      int prev_char = -1;
      for (uint i = 0; i < get_len(); i++)
      {
         const int cur_char = (*this)[i];

         if ((cur_char == 0x0A) && (prev_char != 0x0D))
            tmp.append_char(0x0D);

         tmp.append_char(cur_char);

         prev_char = cur_char;
      }

      swap(tmp);
   }

} // namespace crnlib
