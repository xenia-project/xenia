// File: crn_utils.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

#define CRNLIB_MIN(a, b) (((a) < (b)) ? (a) : (b))
#define CRNLIB_MAX(a, b) (((a) < (b)) ? (b) : (a))

#define CRNLIB_ARRAYSIZE(x) (sizeof(x)/sizeof(x[0]))

#ifdef _MSC_VER
   // Need to explictly extern these with MSVC, but not MinGW.
   extern "C" unsigned long __cdecl _lrotl(unsigned long, int);
   #pragma intrinsic(_lrotl)

   extern "C" unsigned long __cdecl _lrotr(unsigned long, int);
   #pragma intrinsic(_lrotr)
#endif

#ifdef WIN32
   #define CRNLIB_ROTATE_LEFT(x, k) _lrotl(x, k)
   #define CRNLIB_ROTATE_RIGHT(x, k) _lrotr(x, k)
#else
   #define CRNLIB_ROTATE_LEFT(x, k) (((x) << (k)) | ((x) >> (32-(k))))
   #define CRNLIB_ROTATE_RIGHT(x, k) (((x) >> (k)) | ((x) << (32-(k))))
#endif

template<class T, size_t N> T decay_array_to_subtype(T (&a)[N]);
#define CRNLIB_ARRAY_SIZE(X) (sizeof(X) / sizeof(decay_array_to_subtype(X)))

#define CRNLIB_SIZEOF_U32(x) static_cast<uint32>(sizeof(x))

namespace crnlib
{
   namespace utils
   {
      template<typename T> inline void swap(T& l, T& r)
      {
         T temp(l);
         l = r;
         r = temp;
      }

      template<typename T> inline void zero_object(T& obj)
      {
         memset((void*)&obj, 0, sizeof(obj));
      }

      template<typename T> inline void zero_this(T* pObj)
      {
         memset((void*)pObj, 0, sizeof(*pObj));
      }

      inline bool is_bit_set(uint bits, uint mask)
      {
         return (bits & mask) != 0;
      }

      inline void set_bit(uint& bits, uint mask, bool state)
      {
         if (state)
            bits |= mask;
         else
            bits &= ~mask;
      }

      inline bool is_flag_set(uint bits, uint flag)
      {
         CRNLIB_ASSERT(flag < 32U);
         return is_bit_set(bits, 1U << flag);
      }

      inline void set_flag(uint& bits, uint flag, bool state)
      {
         CRNLIB_ASSERT(flag < 32U);
         set_bit(bits, 1U << flag, state);
      }

      inline void invert_buf(void* pBuf, uint size)
      {
         uint8* p = static_cast<uint8*>(pBuf);

         const uint half_size = size >> 1;
         for (uint i = 0; i < half_size; i++)
            utils::swap(p[i], p[size - 1U - i]);
      }

      // buffer_is_little_endian is the endianness of the buffer's data
      template<typename T>
      inline void write_obj(const T& obj, void* pBuf, bool buffer_is_little_endian)
      {
         const uint8* pSrc = reinterpret_cast<const uint8*>(&obj);
         uint8* pDst = static_cast<uint8*>(pBuf);

         if (c_crnlib_little_endian_platform == buffer_is_little_endian)
            memcpy(pDst, pSrc, sizeof(T));
         else
         {
            for (uint i = 0; i < sizeof(T); i++)
               pDst[i] = pSrc[sizeof(T) - 1 - i];
         }
      }

      // buffer_is_little_endian is the endianness of the buffer's data
      template<typename T>
      inline void read_obj(T& obj, const void* pBuf, bool buffer_is_little_endian)
      {
         const uint8* pSrc = reinterpret_cast<const uint8*>(pBuf);
         uint8* pDst = reinterpret_cast<uint8*>(&obj);

         if (c_crnlib_little_endian_platform == buffer_is_little_endian)
            memcpy(pDst, pSrc, sizeof(T));
         else
         {
            for (uint i = 0; i < sizeof(T); i++)
               pDst[i] = pSrc[sizeof(T) - 1 - i];
         }
      }

      template<typename T>
      inline bool write_obj(const T& obj, void*& pBuf, uint& buf_size, bool buffer_is_little_endian)
      {
         if (buf_size < sizeof(T))
            return false;

         utils::write_obj(obj, pBuf, buffer_is_little_endian);

         pBuf = static_cast<uint8*>(pBuf) + sizeof(T);
         buf_size -= sizeof(T);

         return true;
      }

      inline bool write_val(uint8 val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }
      inline bool write_val(uint16 val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }
      inline bool write_val(uint val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }
      inline bool write_val(int val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }
      inline bool write_val(uint64 val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }
      inline bool write_val(float val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }
      inline bool write_val(double val, void*& pBuf, uint& buf_size, bool buffer_is_little_endian) { return write_obj(val, pBuf, buf_size, buffer_is_little_endian); }

      template<typename T>
      inline bool read_obj(T& obj, const void*& pBuf, uint& buf_size, bool buffer_is_little_endian)
      {
         if (buf_size < sizeof(T))
         {
            zero_object(obj);
            return false;
         }

         utils::read_obj(obj, pBuf, buffer_is_little_endian);

         pBuf = static_cast<const uint8*>(pBuf) + sizeof(T);
         buf_size -= sizeof(T);

         return true;
      }

#if defined(_MSC_VER)
      static CRNLIB_FORCE_INLINE uint16 swap16(uint16 x) { return _byteswap_ushort(x); }
      static CRNLIB_FORCE_INLINE uint32 swap32(uint32 x) { return _byteswap_ulong(x); }
      static CRNLIB_FORCE_INLINE uint64 swap64(uint64 x) { return _byteswap_uint64(x); }
#elif defined(__GNUC__)
      static CRNLIB_FORCE_INLINE uint16 swap16(uint16 x) { return static_cast<uint16>((x << 8U) | (x >> 8U)); }
      static CRNLIB_FORCE_INLINE uint32 swap32(uint32 x) { return __builtin_bswap32(x); }
      static CRNLIB_FORCE_INLINE uint64 swap64(uint64 x) { return __builtin_bswap64(x); }
#else
      static CRNLIB_FORCE_INLINE uint16 swap16(uint16 x) { return static_cast<uint16>((x << 8U) | (x >> 8U)); }
      static CRNLIB_FORCE_INLINE uint32 swap32(uint32 x) { return ((x << 24U) | ((x << 8U) & 0x00FF0000U) | ((x >> 8U) & 0x0000FF00U) | (x >> 24U)); }
      static CRNLIB_FORCE_INLINE uint64 swap64(uint64 x) { return (static_cast<uint64>(swap32(static_cast<uint32>(x))) << 32ULL) | swap32(static_cast<uint32>(x >> 32U)); }
#endif

      // Assumes x has been read from memory as a little endian value, converts to native endianness for manipulation.
      CRNLIB_FORCE_INLINE uint16 swap_le16_to_native(uint16 x) { return c_crnlib_little_endian_platform ? x : swap16(x); }
      CRNLIB_FORCE_INLINE uint32 swap_le32_to_native(uint32 x) { return c_crnlib_little_endian_platform ? x : swap32(x); }
      CRNLIB_FORCE_INLINE uint64 swap_le64_to_native(uint64 x) { return c_crnlib_little_endian_platform ? x : swap64(x); }

      // Assumes x has been read from memory as a big endian value, converts to native endianness for manipulation.
      CRNLIB_FORCE_INLINE uint16 swap_be16_to_native(uint16 x) { return c_crnlib_big_endian_platform ? x : swap16(x); }
      CRNLIB_FORCE_INLINE uint32 swap_be32_to_native(uint32 x) { return c_crnlib_big_endian_platform ? x : swap32(x); }
      CRNLIB_FORCE_INLINE uint64 swap_be64_to_native(uint64 x) { return c_crnlib_big_endian_platform ? x : swap64(x); }

      CRNLIB_FORCE_INLINE uint32 read_le32(const void* p) { return swap_le32_to_native(*static_cast<const uint32*>(p)); }
      CRNLIB_FORCE_INLINE void   write_le32(void* p, uint32 x) { *static_cast<uint32*>(p) = swap_le32_to_native(x); }
      CRNLIB_FORCE_INLINE uint64 read_le64(const void* p) { return swap_le64_to_native(*static_cast<const uint64*>(p)); }
      CRNLIB_FORCE_INLINE void   write_le64(void* p, uint64 x) { *static_cast<uint64*>(p) = swap_le64_to_native(x); }

      CRNLIB_FORCE_INLINE uint32 read_be32(const void* p) { return swap_be32_to_native(*static_cast<const uint32*>(p)); }
      CRNLIB_FORCE_INLINE void   write_be32(void* p, uint32 x) { *static_cast<uint32*>(p) = swap_be32_to_native(x); }
      CRNLIB_FORCE_INLINE uint64 read_be64(const void* p) { return swap_be64_to_native(*static_cast<const uint64*>(p)); }
      CRNLIB_FORCE_INLINE void   write_be64(void* p, uint64 x) { *static_cast<uint64*>(p) = swap_be64_to_native(x); }

      inline void endian_swap_mem16(uint16* p, uint n) { while (n--) { *p = swap16(*p); ++p; } }
      inline void endian_swap_mem32(uint32* p, uint n) { while (n--) { *p = swap32(*p); ++p; } }
      inline void endian_swap_mem64(uint64* p, uint n) { while (n--) { *p = swap64(*p); ++p; } }

      inline void endian_swap_mem(void* p, uint size_in_bytes, uint type_size)
      {
         switch (type_size)
         {
            case sizeof(uint16): endian_swap_mem16(static_cast<uint16*>(p), size_in_bytes / type_size); break;
            case sizeof(uint32): endian_swap_mem32(static_cast<uint32*>(p), size_in_bytes / type_size); break;
            case sizeof(uint64): endian_swap_mem64(static_cast<uint64*>(p), size_in_bytes / type_size); break;
         }
      }

      inline void fast_memset(void* pDst, int val, size_t size)
      {
         memset(pDst, val, size);
      }

      inline void fast_memcpy(void* pDst, const void* pSrc, size_t size)
      {
         memcpy(pDst, pSrc, size);
      }

      inline uint count_leading_zeros(uint v)
      {
         uint temp;
         uint n = 32;

         temp = v >> 16;
         if (temp) { n -= 16; v = temp; }

         temp = v >> 8;
         if (temp) { n -=  8; v = temp; }

         temp = v >> 4;
         if (temp) { n -=  4; v = temp; }

         temp = v >> 2;
         if (temp) { n -=  2; v = temp; }

         temp = v >> 1;
         if (temp) { n -=  1; v = temp; }

         if (v & 1) n--;

         return n;
      }

      inline uint count_leading_zeros16(uint v)
      {
         CRNLIB_ASSERT(v < 0x10000);

         uint temp;
         uint n = 16;

         temp = v >> 8;
         if (temp) { n -=  8; v = temp; }

         temp = v >> 4;
         if (temp) { n -=  4; v = temp; }

         temp = v >> 2;
         if (temp) { n -=  2; v = temp; }

         temp = v >> 1;
         if (temp) { n -=  1; v = temp; }

         if (v & 1) n--;

         return n;
      }

      void endian_switch_words(uint16* p, uint num);
      void endian_switch_dwords(uint32* p, uint num);
      void copy_words(uint16* pDst, const uint16* pSrc, uint num, bool endian_switch);
      void copy_dwords(uint32* pDst, const uint32* pSrc, uint num, bool endian_switch);

      uint compute_max_mips(uint width, uint height);

   }   // namespace utils

} // namespace crnlib

