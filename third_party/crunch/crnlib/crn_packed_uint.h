// File: crn_packed_uint
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   template<unsigned int N>
   struct packed_uint
   {
      inline packed_uint() { }

      inline packed_uint(unsigned int val) { *this = val; }

      inline packed_uint(const packed_uint& other) { *this = other; }

      inline packed_uint& operator= (const packed_uint& rhs)
      {
         if (this != &rhs)
            memcpy(m_buf, rhs.m_buf, sizeof(m_buf));
         return *this;
      }

      inline packed_uint& operator= (unsigned int val)
      {
#ifdef CRNLIB_BUILD_DEBUG
         if (N == 1)
         {
            CRNLIB_ASSERT(val <= 0xFFU);
         }
         else if (N == 2)
         {
            CRNLIB_ASSERT(val <= 0xFFFFU);
         }
         else if (N == 3)
         {
            CRNLIB_ASSERT(val <= 0xFFFFFFU);
         }
#endif      
         
         val <<= (8U * (4U - N));

         for (unsigned int i = 0; i < N; i++)
         {
            m_buf[i] = static_cast<unsigned char>(val >> 24U);
            val <<= 8U;
         }

         return *this;
      }

      inline operator unsigned int() const
      {
         switch (N)
         {
            case 1:  return  m_buf[0];
            case 2:  return (m_buf[0] <<  8U) |  m_buf[1];
            case 3:  return (m_buf[0] << 16U) | (m_buf[1] <<  8U) | (m_buf[2]);
            default: return (m_buf[0] << 24U) | (m_buf[1] << 16U) | (m_buf[2] << 8U) | (m_buf[3]);
         }
      }

      unsigned char m_buf[N];
   };
   template<typename T>
   class packed_value
   {
   public:
      packed_value() { }
      packed_value(T val) { *this = val; }

      inline operator T() const
      {
         T result = 0;
         for (int i = sizeof(T) - 1; i >= 0; i--)
            result = static_cast<T>((result << 8) | m_bytes[i]);
         return result;
      }
      packed_value& operator= (T val)
      {
         for (int i = 0; i < sizeof(T); i++)
         {
            m_bytes[i] = static_cast<uint8>(val);
            val >>= 8;
         }
         return *this;
      }            
   private:
      uint8 m_bytes[sizeof(T)];
   };
} // namespace crnlib
   
