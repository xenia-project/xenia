// File: data_stream_serializer.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_data_stream.h"

namespace crnlib
{
   // Defaults to little endian mode.
   class data_stream_serializer
   {
   public:
      data_stream_serializer() : m_pStream(NULL), m_little_endian(true) { }
      data_stream_serializer(data_stream* pStream) : m_pStream(pStream), m_little_endian(true) { }
      data_stream_serializer(data_stream& stream) : m_pStream(&stream), m_little_endian(true) { }
      data_stream_serializer(const data_stream_serializer& other) : m_pStream(other.m_pStream), m_little_endian(other.m_little_endian) { }
      
      data_stream_serializer& operator= (const data_stream_serializer& rhs) { m_pStream = rhs.m_pStream; m_little_endian = rhs.m_little_endian; return *this; }

      data_stream* get_stream() const { return m_pStream; }
      void set_stream(data_stream* pStream) { m_pStream = pStream; }

      const dynamic_string& get_name() const { return m_pStream ? m_pStream->get_name() : g_empty_dynamic_string; }

      bool get_error() { return m_pStream ? m_pStream->get_error() : false; }
      
      bool get_little_endian() const { return m_little_endian; }
      void set_little_endian(bool little_endian) { m_little_endian = little_endian; }
      
      bool write(const void* pBuf, uint len)
      {
         return m_pStream->write(pBuf, len) == len;
      }
                  
      bool read(void* pBuf, uint len)
      {
         return m_pStream->read(pBuf, len) == len;
      }
      
      // size = size of each element, count = number of elements, returns actual count of elements written
      uint write(const void* pBuf, uint size, uint count)
      {
         uint actual_size = size * count;
         if (!actual_size)
            return 0;
         uint n = m_pStream->write(pBuf, actual_size);
         if (n == actual_size)
            return count;
         return n / size;
      }

      // size = size of each element, count = number of elements, returns actual count of elements read
      uint read(void* pBuf, uint size, uint count)
      {
         uint actual_size = size * count;
         if (!actual_size)
            return 0;
         uint n = m_pStream->read(pBuf, actual_size);
         if (n == actual_size)
            return count;
         return n / size;
      }
      
      bool write_chars(const char* pBuf, uint len)
      {
         return write(pBuf, len);
      }
      
      bool read_chars(char* pBuf, uint len)
      {
         return read(pBuf, len);
      }
      
      bool skip(uint len)
      {
         return m_pStream->skip(len) == len;
      }
      
      template<typename T>
      bool write_object(const T& obj)
      {
         if (m_little_endian == c_crnlib_little_endian_platform)
            return write(&obj, sizeof(obj));
         else
         {
            uint8 buf[sizeof(T)];
            uint buf_size = sizeof(T);
            void* pBuf = buf;
            utils::write_obj(obj, pBuf, buf_size, m_little_endian);
            
            return write(buf, sizeof(T));
         }
      }
      
      template<typename T>
      bool read_object(T& obj)
      {
         if (m_little_endian == c_crnlib_little_endian_platform)
            return read(&obj, sizeof(obj));
         else
         {
            uint8 buf[sizeof(T)];
            if (!read(buf, sizeof(T)))
               return false;
            
            uint buf_size = sizeof(T);
            const void* pBuf = buf;
            utils::read_obj(obj, pBuf, buf_size, m_little_endian);
            
            return true;
         }
      }
         
      template<typename T>
      bool write_value(T value)
      {
         return write_object(value);
      }
      
      template<typename T>
      T read_value(const T& on_error_value = T())
      {
         T result;
         if (!read_object(result))
            result = on_error_value;
         return result;
      }
      
      template<typename T>
      bool write_enum(T e)
      {
         int val = static_cast<int>(e);
         return write_object(val);
      }

      template<typename T>
      T read_enum()
      {
         return static_cast<T>(read_value<int>());
      }
      
      // Writes uint using a simple variable length code (VLC).
      bool write_uint_vlc(uint val)
      {
         do
         {
            uint8 c = static_cast<uint8>(val) & 0x7F;
            if (val <= 0x7F)
               c |= 0x80;
               
            if (!write_value(c))
               return false;
            
            val >>= 7;
         } while (val);
         
         return true;
      }
      
      // Reads uint using a simple variable length code (VLC).
      bool read_uint_vlc(uint& val)
      {
         val = 0;
         uint shift = 0;
         
         for ( ; ; )
         {
            if (shift >= 32)
               return false;
               
            uint8 c;
            if (!read_object(c))
               return false;
            
            val |= ((c & 0x7F) << shift);
            shift += 7;
            
            if (c & 0x80)
               break;
         }
         
         return true;
      }
      
      bool write_c_str(const char* p)
      {
         uint len = static_cast<uint>(strlen(p));
         if (!write_uint_vlc(len))
            return false;
         
         return write_chars(p, len);
      }
      
      bool read_c_str(char* pBuf, uint buf_size)
      {
         uint len;
         if (!read_uint_vlc(len))
            return false;
         if ((len + 1) > buf_size)
            return false;
         
         pBuf[len] = '\0';
         
         return read_chars(pBuf, len);
      }
      
      bool write_string(const dynamic_string& str)
      {
         if (!write_uint_vlc(str.get_len()))
            return false;

         return write_chars(str.get_ptr(), str.get_len());
      }
      
      bool read_string(dynamic_string& str)
      {
         uint len;
         if (!read_uint_vlc(len))
            return false;
         
         if (!str.set_len(len))
            return false;
         
         if (len)
         {
            if (!read_chars(str.get_ptr_raw(), len))
               return false;
            
            if (memchr(str.get_ptr(), 0, len) != NULL)
            {
               str.truncate(0);
               return false;
            }
         }
         
         return true;
      }
                  
      template<typename T>
      bool write_vector(const T& vec)
      {
         if (!write_uint_vlc(vec.size()))
            return false;
         
         for (uint i = 0; i < vec.size(); i++)
         {
            *this << vec[i];
            if (get_error())
               return false;
         }
         
         return true;            
      };
      
      template<typename T>
      bool read_vector(T& vec, uint num_expected = UINT_MAX)
      {
         uint size;
         if (!read_uint_vlc(size))
            return false;
         
         if ((size * sizeof(T::value_type)) >= 2U*1024U*1024U*1024U)
            return false;
         
         if ((num_expected != UINT_MAX) && (size != num_expected))
            return false;
       
         vec.resize(size);
         for (uint i = 0; i < vec.size(); i++)
         {
            *this >> vec[i];
            
            if (get_error())
               return false;
         }
         
         return true;
      }

      bool read_entire_file(crnlib::vector<uint8>& buf)
      {
         return m_pStream->read_array(buf);
      }

      bool write_entire_file(const crnlib::vector<uint8>& buf)
      {
         return m_pStream->write_array(buf);
      }
      
      // Got this idea from the Molly Rocket forums.
      // fmt may contain the characters "1", "2", or "4".
      bool writef(char *fmt, ...) 
      { 
         va_list v; 
         va_start(v, fmt); 
         
         while (*fmt) 
         { 
            switch (*fmt++) 
            { 
               case '1': 
               { 
                  const uint8 x = static_cast<uint8>(va_arg(v, uint)); 
                  if (!write_value(x))
                     return false;
               } 
               case '2': 
               { 
                  const uint16 x = static_cast<uint16>(va_arg(v, uint)); 
                  if (!write_value(x))
                     return false;
               } 
               case '4': 
               { 
                  const uint32 x = static_cast<uint32>(va_arg(v, uint)); 
                  if (!write_value(x))
                     return false;
               } 
               case ' ':
               case ',':
               {
                  break;
               }
               default: 
               {
                  CRNLIB_ASSERT(0); 
                  return false;
               }                  
            } 
         } 
         
         va_end(v); 
         return true;
      } 
      
      // Got this idea from the Molly Rocket forums.
      // fmt may contain the characters "1", "2", or "4".
      bool readf(char *fmt, ...) 
      { 
         va_list v; 
         va_start(v, fmt); 

         while (*fmt) 
         { 
            switch (*fmt++) 
            { 
               case '1': 
               { 
                  uint8* x = va_arg(v, uint8*); 
                  CRNLIB_ASSERT(x);
                  if (!read_object(*x))
                     return false;
               } 
               case '2': 
               { 
                  uint16* x = va_arg(v, uint16*); 
                  CRNLIB_ASSERT(x);
                  if (!read_object(*x))
                     return false;
               } 
               case '4': 
               { 
                  uint32* x = va_arg(v, uint32*);
                  CRNLIB_ASSERT(x);
                  if (!read_object(*x))
                     return false;
               }
               case ' ':
               case ',':
               {
                  break;
               }
               default: 
               {
                  CRNLIB_ASSERT(0); 
                  return false;
               }                  
            } 
         } 

         va_end(v); 
         return true;
      } 
            
   private:
      data_stream* m_pStream;

      bool m_little_endian;
   };
   
   // Write operators
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, bool val)           { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, int8 val)           { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, uint8 val)          { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, int16 val)          { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, uint16 val)         { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, int32 val)          { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, uint32 val)         { serializer.write_uint_vlc(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, int64 val)          { serializer.write_value(val); return serializer; } 
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, uint64 val)         { serializer.write_value(val); return serializer; } 
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, long val)           { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, unsigned long val)  { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, float val)          { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, double val)         { serializer.write_value(val); return serializer; }
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, const char* p)      { serializer.write_c_str(p); return serializer; }
   
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, const dynamic_string& str)
   {
      serializer.write_string(str);
      return serializer;
   }
   
   template<typename T>
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, const crnlib::vector<T>& vec)
   {
      serializer.write_vector(vec);
      return serializer;
   }
   
   template<typename T>
   inline data_stream_serializer& operator<< (data_stream_serializer& serializer, const T* p)
   {
      serializer.write_object(*p);
      return serializer;
   }
   
   // Read operators
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, bool& val)           { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, int8& val)           { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, uint8& val)          { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, int16& val)          { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, uint16& val)         { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, int32& val)          { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, uint32& val)         { serializer.read_uint_vlc(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, int64& val)          { serializer.read_object(val); return serializer; } 
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, uint64& val)         { serializer.read_object(val); return serializer; } 
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, long& val)           { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, unsigned long& val)  { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, float& val)          { serializer.read_object(val); return serializer; }
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, double& val)         { serializer.read_object(val); return serializer; }
      
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, dynamic_string& str)
   {
      serializer.read_string(str);
      return serializer;
   }
   
   template<typename T>
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, crnlib::vector<T>& vec)
   {
      serializer.read_vector(vec);
      return serializer;
   }
   
   template<typename T>
   inline data_stream_serializer& operator>> (data_stream_serializer& serializer, T* p)
   {
      serializer.read_object(*p);
      return serializer;
   }

} // namespace crnlib







