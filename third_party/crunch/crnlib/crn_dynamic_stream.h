// File: crn_dynamic_stream.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_data_stream.h"

namespace crnlib
{
   class dynamic_stream : public data_stream
   {
   public:
      dynamic_stream(uint initial_size, const char* pName = "dynamic_stream", uint attribs = cDataStreamSeekable | cDataStreamWritable | cDataStreamReadable) :
         data_stream(pName, attribs),
         m_ofs(0)
      {
         open(initial_size, pName, attribs);
      }

      dynamic_stream(const void* pBuf, uint size, const char* pName = "dynamic_stream", uint attribs = cDataStreamSeekable | cDataStreamWritable | cDataStreamReadable) :
         data_stream(pName, attribs),
         m_ofs(0)
      {
         open(pBuf, size, pName, attribs);
      }

      dynamic_stream() :
         data_stream(),
         m_ofs(0)
      {
         open();
      }

      virtual ~dynamic_stream()
      {
      }

      bool open(uint initial_size = 0, const char* pName = "dynamic_stream", uint attribs = cDataStreamSeekable | cDataStreamWritable | cDataStreamReadable)
      {
         close();

         m_opened = true;
         m_buf.clear();
         m_buf.resize(initial_size);
         m_ofs = 0;
         m_name.set(pName ? pName : "dynamic_stream");
         m_attribs = static_cast<attribs_t>(attribs);
         return true;
      }

      bool reopen(const char* pName, uint attribs)
      {
         if (!m_opened)
         {
            return open(0, pName, attribs);
         }

         m_name.set(pName ? pName : "dynamic_stream");
         m_attribs = static_cast<attribs_t>(attribs);
         return true;
      }

      bool open(const void* pBuf, uint size, const char* pName = "dynamic_stream", uint attribs = cDataStreamSeekable | cDataStreamWritable | cDataStreamReadable)
      {
         if (!m_opened)
         {
            m_opened = true;
            m_buf.resize(size);
            if (size)
            {
               CRNLIB_ASSERT(pBuf);
               memcpy(&m_buf[0], pBuf, size);
            }
            m_ofs = 0;
            m_name.set(pName ? pName : "dynamic_stream");
            m_attribs = static_cast<attribs_t>(attribs);
            return true;
         }

         return false;
      }

      virtual bool close()
      {
         if (m_opened)
         {
            m_opened = false;
            m_buf.clear();
            m_ofs = 0;
            return true;
         }

         return false;
      }

      const crnlib::vector<uint8>& get_buf() const { return m_buf; }
            crnlib::vector<uint8>& get_buf()       { return m_buf; }

      void reserve(uint size)
      {
         if (m_opened)
         {
            m_buf.reserve(size);
         }
      }

      virtual const void* get_ptr() const { return m_buf.empty() ? NULL : &m_buf[0]; }

      virtual uint read(void* pBuf, uint len)
      {
         CRNLIB_ASSERT(pBuf && (len <= 0x7FFFFFFF));

         if ((!m_opened) || (!is_readable()) || (!len))
            return 0;

         CRNLIB_ASSERT(m_ofs <= m_buf.size());

         uint bytes_left = m_buf.size() - m_ofs;

         len = math::minimum<uint>(len, bytes_left);

         if (len)
            memcpy(pBuf, &m_buf[m_ofs], len);

         m_ofs += len;

         return len;
      }

      virtual uint write(const void* pBuf, uint len)
      {
         CRNLIB_ASSERT(pBuf && (len <= 0x7FFFFFFF));

         if ((!m_opened) || (!is_writable()) || (!len))
            return 0;

         CRNLIB_ASSERT(m_ofs <= m_buf.size());

         uint new_ofs = m_ofs + len;
         if (new_ofs > m_buf.size())
            m_buf.resize(new_ofs);

         memcpy(&m_buf[m_ofs], pBuf, len);
         m_ofs = new_ofs;

         return len;
      }

      virtual bool flush()
      {
         if (!m_opened)
            return false;

         return true;
      }

      virtual uint64 get_size()
      {
         if (!m_opened)
            return 0;

         return m_buf.size();
      }

      virtual uint64 get_remaining()
      {
         if (!m_opened)
            return 0;

         CRNLIB_ASSERT(m_ofs <= m_buf.size());

         return m_buf.size() - m_ofs;
      }

      virtual uint64 get_ofs()
      {
         if (!m_opened)
            return 0;

         return m_ofs;
      }

      virtual bool seek(int64 ofs, bool relative)
      {
         if ((!m_opened) || (!is_seekable()))
            return false;

         int64 new_ofs = relative ? (m_ofs + ofs) : ofs;

         if (new_ofs < 0)
            return false;
         else if (new_ofs > m_buf.size())
            return false;

         m_ofs = static_cast<uint>(new_ofs);

         post_seek();

         return true;
      }

   private:
      crnlib::vector<uint8>    m_buf;
      uint                    m_ofs;
   };

} // namespace crnlib

