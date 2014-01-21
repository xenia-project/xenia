// File: crn_ktx_texture.h
#ifndef _KTX_TEXTURE_H_
#define _KTX_TEXTURE_H_
#ifdef _MSC_VER
#pragma once
#endif

#include "crn_data_stream_serializer.h"

#define KTX_ENDIAN 0x04030201
#define KTX_OPPOSITE_ENDIAN 0x01020304

namespace crnlib
{
   extern const uint8 s_ktx_file_id[12];

   struct ktx_header
   {
      uint8 m_identifier[12];
      uint32 m_endianness;
      uint32 m_glType;
      uint32 m_glTypeSize;
      uint32 m_glFormat;
      uint32 m_glInternalFormat;
      uint32 m_glBaseInternalFormat;
      uint32 m_pixelWidth;
      uint32 m_pixelHeight;
      uint32 m_pixelDepth;
      uint32 m_numberOfArrayElements;
      uint32 m_numberOfFaces;
      uint32 m_numberOfMipmapLevels;
      uint32 m_bytesOfKeyValueData;

      void clear()
      {
         memset(this, 0, sizeof(*this));
      }

      void endian_swap()
      {
         utils::endian_swap_mem32(&m_endianness, (sizeof(*this) - sizeof(m_identifier)) / sizeof(uint32));
      }
   };

   typedef crnlib::vector<uint8_vec> ktx_key_value_vec;
   typedef crnlib::vector<uint8_vec> ktx_image_data_vec;

   // Compressed pixel data formats: ETC1, DXT1, DXT3, DXT5
   enum 
   { 
      KTX_ETC1_RGB8_OES = 0x8D64, KTX_RGB_S3TC = 0x83A0, KTX_RGB4_S3TC = 0x83A1, KTX_COMPRESSED_RGB_S3TC_DXT1_EXT = 0x83F0,
      KTX_COMPRESSED_RGBA_S3TC_DXT1_EXT = 0x83F1, KTX_COMPRESSED_SRGB_S3TC_DXT1_EXT = 0x8C4C, KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT = 0x8C4D,
      KTX_RGBA_S3TC = 0x83A2, KTX_RGBA4_S3TC = 0x83A3, KTX_COMPRESSED_RGBA_S3TC_DXT3_EXT = 0x83F2, KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT = 0x8C4E,
      KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT = 0x83F3, KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT = 0x8C4F, KTX_RGBA_DXT5_S3TC = 0x83A4, KTX_RGBA4_DXT5_S3TC = 0x83A5,
      KTX_COMPRESSED_RED_RGTC1_EXT = 0x8DBB, KTX_COMPRESSED_SIGNED_RED_RGTC1_EXT = 0x8DBC, KTX_COMPRESSED_RED_GREEN_RGTC2_EXT = 0x8DBD, KTX_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT = 0x8DBE,
      KTX_COMPRESSED_LUMINANCE_LATC1_EXT = 0x8C70, KTX_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT = 0x8C71, KTX_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT = 0x8C72, KTX_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT = 0x8C73
   };

   // Pixel formats (various internal, base, and base internal formats)
   enum 
   { 
      KTX_R8 = 0x8229, KTX_R8UI = 0x8232, KTX_RGB8 = 0x8051, KTX_SRGB8 = 0x8C41, KTX_SRGB = 0x8C40, KTX_SRGB_ALPHA = 0x8C42,
      KTX_SRGB8_ALPHA8 = 0x8C43, KTX_RGBA8 = 0x8058, KTX_STENCIL_INDEX = 0x1901, KTX_DEPTH_COMPONENT = 0x1902, KTX_DEPTH_STENCIL = 0x84F9, KTX_RED = 0x1903,
      KTX_GREEN = 0x1904, KTX_BLUE = 0x1905, KTX_ALPHA = 0x1906, KTX_RG = 0x8227, KTX_RGB = 0x1907, KTX_RGBA = 0x1908, KTX_BGR = 0x80E0, KTX_BGRA = 0x80E1,
      KTX_RED_INTEGER = 0x8D94, KTX_GREEN_INTEGER = 0x8D95, KTX_BLUE_INTEGER = 0x8D96, KTX_ALPHA_INTEGER = 0x8D97, KTX_RGB_INTEGER = 0x8D98, KTX_RGBA_INTEGER = 0x8D99,
      KTX_BGR_INTEGER = 0x8D9A, KTX_BGRA_INTEGER = 0x8D9B, KTX_LUMINANCE = 0x1909, KTX_LUMINANCE_ALPHA = 0x190A, KTX_RG_INTEGER = 0x8228, KTX_RG8 = 0x822B,
      KTX_ALPHA8 = 0x803C, KTX_LUMINANCE8 = 0x8040, KTX_LUMINANCE8_ALPHA8 = 0x8045
   };

   // Pixel data types
   enum 
   {
      KTX_UNSIGNED_BYTE = 0x1401, KTX_BYTE = 0x1400, KTX_UNSIGNED_SHORT = 0x1403, KTX_SHORT = 0x1402,
      KTX_UNSIGNED_INT = 0x1405, KTX_INT = 0x1404, KTX_HALF_FLOAT = 0x140B, KTX_FLOAT = 0x1406,
      KTX_UNSIGNED_BYTE_3_3_2 = 0x8032, KTX_UNSIGNED_BYTE_2_3_3_REV = 0x8362, KTX_UNSIGNED_SHORT_5_6_5 = 0x8363,
      KTX_UNSIGNED_SHORT_5_6_5_REV = 0x8364, KTX_UNSIGNED_SHORT_4_4_4_4 = 0x8033, KTX_UNSIGNED_SHORT_4_4_4_4_REV = 0x8365,
      KTX_UNSIGNED_SHORT_5_5_5_1 = 0x8034, KTX_UNSIGNED_SHORT_1_5_5_5_REV = 0x8366, KTX_UNSIGNED_INT_8_8_8_8 = 0x8035,
      KTX_UNSIGNED_INT_8_8_8_8_REV = 0x8367, KTX_UNSIGNED_INT_10_10_10_2 = 0x8036, KTX_UNSIGNED_INT_2_10_10_10_REV = 0x8368,
      KTX_UNSIGNED_INT_24_8 = 0x84FA, KTX_UNSIGNED_INT_10F_11F_11F_REV = 0x8C3B, KTX_UNSIGNED_INT_5_9_9_9_REV = 0x8C3E, 
      KTX_FLOAT_32_UNSIGNED_INT_24_8_REV = 0x8DAD
   };
   
   bool is_packed_pixel_ogl_type(uint32 ogl_type);
   uint get_ogl_type_size(uint32 ogl_type);
   bool get_ogl_fmt_desc(uint32 ogl_fmt, uint32 ogl_type, uint& block_dim, uint& bytes_per_block);
   uint get_ogl_type_size(uint32 ogl_type);
   uint32 get_ogl_base_internal_fmt(uint32 ogl_fmt);

   class ktx_texture
   {
   public:
      ktx_texture()
      {
         clear();
      }

      ktx_texture(const ktx_texture& other)
      {
         *this = other;
      }

      ktx_texture& operator= (const ktx_texture& rhs)
      {
         if (this == &rhs)
            return *this;

         clear();

         m_header = rhs.m_header;
         m_key_values = rhs.m_key_values;
         m_image_data = rhs.m_image_data;
         m_block_dim = rhs.m_block_dim;
         m_bytes_per_block = rhs.m_bytes_per_block;
         m_opposite_endianness = rhs.m_opposite_endianness;

         return *this;
      }

      void clear()
      {
         m_header.clear();
         m_key_values.clear();
         m_image_data.clear();
         
         m_block_dim = 0;
         m_bytes_per_block = 0;

         m_opposite_endianness = false;
      }

      // High level methods
      bool read_from_stream(data_stream_serializer& serializer);
      bool write_to_stream(data_stream_serializer& serializer, bool no_keyvalue_data = false);
      
      bool init_2D(uint width, uint height, uint num_mips, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type);
      bool init_2D_array(uint width, uint height, uint num_mips, uint array_size, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type);
      bool init_3D(uint width, uint height, uint depth, uint num_mips, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type);
      bool init_cubemap(uint dim, uint num_mips, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type);

      bool check_header() const;
      bool consistency_check() const;

      // General info

      bool is_valid() const { return (m_header.m_pixelWidth > 0) && (m_image_data.size() > 0); }

      uint get_width() const { return m_header.m_pixelWidth; }
      uint get_height() const { return CRNLIB_MAX(m_header.m_pixelHeight, 1); }
      uint get_depth() const { return CRNLIB_MAX(m_header.m_pixelDepth, 1); }
      uint get_num_mips() const { return CRNLIB_MAX(m_header.m_numberOfMipmapLevels, 1); }
      uint get_array_size() const { return CRNLIB_MAX(m_header.m_numberOfArrayElements, 1); }
      uint get_num_faces() const { return m_header.m_numberOfFaces; }

      uint32 get_ogl_type() const { return m_header.m_glType; }
      uint32 get_ogl_fmt() const { return m_header.m_glFormat; }
      uint32 get_ogl_base_fmt() const { return m_header.m_glBaseInternalFormat; }
      uint32 get_ogl_internal_fmt() const { return m_header.m_glInternalFormat; }
            
      uint get_total_images() const { return get_num_mips() * (get_depth() * get_num_faces() * get_array_size()); }

      bool is_compressed() const { return m_block_dim > 1; }
      bool is_uncompressed() const { return !is_compressed(); }
     
      bool get_opposite_endianness() const { return m_opposite_endianness; }
      void set_opposite_endianness(bool flag) { m_opposite_endianness = flag; }

      uint32 get_block_dim() const { return m_block_dim; }
      uint32 get_bytes_per_block() const { return m_bytes_per_block; }

      const ktx_header& get_header() const { return m_header; }

      // Key values
      const ktx_key_value_vec& get_key_value_vec() const { return m_key_values; }
            ktx_key_value_vec& get_key_value_vec()       { return m_key_values; }

      const uint8_vec* find_key(const char* pKey) const;
      bool get_key_value_as_string(const char* pKey, dynamic_string& str) const;

      uint add_key_value(const char* pKey, const void* pVal, uint val_size);
      uint add_key_value(const char* pKey, const char* pVal) { return add_key_value(pKey, pVal, static_cast<uint>(strlen(pVal)) + 1); }

      // Image data
      uint get_num_images() const { return m_image_data.size(); }

      const uint8_vec& get_image_data(uint image_index) const  { return m_image_data[image_index]; }
            uint8_vec& get_image_data(uint image_index)        { return m_image_data[image_index]; }

      const uint8_vec& get_image_data(uint mip_index, uint array_index, uint face_index, uint zslice_index) const  { return get_image_data(get_image_index(mip_index, array_index, face_index, zslice_index)); }
            uint8_vec& get_image_data(uint mip_index, uint array_index, uint face_index, uint zslice_index)        { return get_image_data(get_image_index(mip_index, array_index, face_index, zslice_index)); }
      
      const ktx_image_data_vec& get_image_data_vec() const  { return m_image_data; }
            ktx_image_data_vec& get_image_data_vec()        { return m_image_data; }

      void add_image(uint face_index, uint mip_index, const void* pImage, uint image_size) 
      { 
         const uint image_index = get_image_index(mip_index, 0, face_index, 0);
         if (image_index >= m_image_data.size())
            m_image_data.resize(image_index + 1);
         if (image_size)
         {  
            uint8_vec& v = m_image_data[image_index];
            v.resize(image_size);
            memcpy(&v[0], pImage, image_size);
         }
      }

      uint get_image_index(uint mip_index, uint array_index, uint face_index, uint zslice_index) const
      {
         CRNLIB_ASSERT((mip_index < get_num_mips()) && (array_index < get_array_size()) && (face_index < get_num_faces()) && (zslice_index < get_depth()));
         return zslice_index + (face_index * get_depth()) + (array_index * (get_depth() * get_num_faces())) + (mip_index * (get_depth() * get_num_faces() * get_array_size()));
      }
      
      void get_mip_dim(uint mip_index, uint& mip_width, uint& mip_height) const
      {
         CRNLIB_ASSERT(mip_index < get_num_mips());
         mip_width = CRNLIB_MAX(get_width() >> mip_index, 1);
         mip_height = CRNLIB_MAX(get_height() >> mip_index, 1);
      }

      void get_mip_dim(uint mip_index, uint& mip_width, uint& mip_height, uint& mip_depth) const
      {
         CRNLIB_ASSERT(mip_index < get_num_mips());
         mip_width = CRNLIB_MAX(get_width() >> mip_index, 1);
         mip_height = CRNLIB_MAX(get_height() >> mip_index, 1);
         mip_depth = CRNLIB_MAX(get_depth() >> mip_index, 1);
      }
         
   private:
      ktx_header                 m_header;
      
      ktx_key_value_vec          m_key_values;
      ktx_image_data_vec         m_image_data;

      uint32                     m_block_dim;
      uint32                     m_bytes_per_block;
            
      bool                       m_opposite_endianness;
      
      bool compute_pixel_info();
   };
   
} // namespace crnlib

#endif // #ifndef _KTX_TEXTURE_H_
