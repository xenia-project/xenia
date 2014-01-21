// File: crn_cfile_stream.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_data_stream.h"

namespace crnlib
{
   class cfile_stream : public data_stream
   {
   public:
      cfile_stream() : data_stream(), m_pFile(NULL), m_size(0), m_ofs(0), m_has_ownership(false)
      {
      }

      cfile_stream(FILE* pFile, const char* pFilename, uint attribs, bool has_ownership) :
         data_stream(), m_pFile(NULL), m_size(0), m_ofs(0), m_has_ownership(false)
      {
         open(pFile, pFilename, attribs, has_ownership);
      }

      cfile_stream(const char* pFilename, uint attribs = cDataStreamReadable | cDataStreamSeekable, bool open_existing = false) :
         data_stream(), m_pFile(NULL), m_size(0), m_ofs(0), m_has_ownership(false)
      {
         open(pFilename, attribs, open_existing);
      }

      virtual ~cfile_stream()
      {
         close();
      }

      virtual bool close()
      {
         clear_error();

         if (m_opened)
         {
            bool status = true;
            if (m_has_ownership)
            {
               if (EOF == fclose(m_pFile))
                  status = false;
            }

            m_pFile = NULL;
            m_opened = false;
            m_size = 0;
            m_ofs = 0;
            m_has_ownership = false;

            return status;
         }

         return false;
      }

      bool open(FILE* pFile, const char* pFilename, uint attribs, bool has_ownership)
      {
         CRNLIB_ASSERT(pFile);
         CRNLIB_ASSERT(pFilename);

         close();

         set_name(pFilename);
         m_pFile = pFile;
         m_has_ownership = has_ownership;
         m_attribs = static_cast<uint16>(attribs);

         m_ofs = crn_ftell(m_pFile);
         crn_fseek(m_pFile, 0, SEEK_END);
         m_size = crn_ftell(m_pFile);
         crn_fseek(m_pFile, m_ofs, SEEK_SET);

         m_opened = true;

         return true;
      }

      bool open(const char* pFilename, uint attribs = cDataStreamReadable | cDataStreamSeekable, bool open_existing = false)
      {
         CRNLIB_ASSERT(pFilename);

         close();

         m_attribs = static_cast<uint16>(attribs);

         const char* pMode;
         if ((is_readable()) && (is_writable()))
            pMode = open_existing ? "r+b" : "w+b";
         else if (is_writable())
            pMode = open_existing ? "ab" : "wb";
         else if (is_readable())
            pMode = "rb";
         else
         {
            set_error();
            return false;
         }

         FILE* pFile = NULL;
         crn_fopen(&pFile, pFilename, pMode);
         m_has_ownership = true;

         if (!pFile)
         {
            set_error();
            return false;
         }

         // TODO: Change stream class to support UCS2 filenames.

         return open(pFile, pFilename, attribs, true);
      }

      FILE* get_file() const { return m_pFile; }

      virtual uint read(void* pBuf, uint len)
      {
         CRNLIB_ASSERT(pBuf && (len <= 0x7FFFFFFF));

         if (!m_opened || (!is_readable()) || (!len))
            return 0;

         len = static_cast<uint>(math::minimum<uint64>(len, get_remaining()));

         if (fread(pBuf, 1, len, m_pFile) != len)
         {
            set_error();
            return 0;
         }

         m_ofs += len;
         return len;
      }

      virtual uint write(const void* pBuf, uint len)
      {
         CRNLIB_ASSERT(pBuf && (len <= 0x7FFFFFFF));

         if (!m_opened || (!is_writable()) || (!len))
            return 0;

         if (fwrite(pBuf, 1, len, m_pFile) != len)
         {
            set_error();
            return 0;
         }

         m_ofs += len;
         m_size = math::maximum(m_size, m_ofs);

         return len;
      }

      virtual bool flush()
      {
         if ((!m_opened) || (!is_writable()))
            return false;

         if (EOF == fflush(m_pFile))
         {
            set_error();
            return false;
         }

         return true;
      }

      virtual uint64 get_size()
      {
         if (!m_opened)
            return 0;

         return m_size;
      }

      virtual uint64 get_remaining()
      {
         if (!m_opened)
            return 0;

         CRNLIB_ASSERT(m_ofs <= m_size);
         return m_size - m_ofs;
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
         else if (static_cast<uint64>(new_ofs) > m_size)
            return false;

         if (static_cast<uint64>(new_ofs) != m_ofs)
         {
            if (crn_fseek(m_pFile, new_ofs, SEEK_SET) != 0)
            {
               set_error();
               return false;
            }

            m_ofs = new_ofs;
         }

         return true;
      }

      static bool read_file_into_array(const char* pFilename, vector<uint8>& buf)
      {
         cfile_stream in_stream(pFilename);
         if (!in_stream.is_opened())
            return false;
         return in_stream.read_array(buf);
      }

      static bool write_array_to_file(const char* pFilename, const vector<uint8>& buf)
      {
         cfile_stream out_stream(pFilename, cDataStreamWritable|cDataStreamSeekable);
         if (!out_stream.is_opened())
            return false;
         return out_stream.write_array(buf);
      }

   private:
      FILE* m_pFile;
      uint64 m_size, m_ofs;
      bool m_has_ownership;
   };

} // namespace crnlib
