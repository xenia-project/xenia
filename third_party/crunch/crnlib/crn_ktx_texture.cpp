// File: crn_ktx_texture.cpp
#include "crn_core.h"
#include "crn_ktx_texture.h"
#include "crn_console.h"

// Set #if CRNLIB_KTX_PVRTEX_WORKAROUNDS to 1 to enable various workarounds for oddball KTX files written by PVRTexTool.
#define CRNLIB_KTX_PVRTEX_WORKAROUNDS 1

namespace crnlib
{
   const uint8 s_ktx_file_id[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x31, 0x31, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };

   bool is_packed_pixel_ogl_type(uint32 ogl_type)
   {
      switch (ogl_type)
      {
         case KTX_UNSIGNED_BYTE_3_3_2:
         case KTX_UNSIGNED_BYTE_2_3_3_REV:
         case KTX_UNSIGNED_SHORT_5_6_5:
         case KTX_UNSIGNED_SHORT_5_6_5_REV:
         case KTX_UNSIGNED_SHORT_4_4_4_4:
         case KTX_UNSIGNED_SHORT_4_4_4_4_REV:
         case KTX_UNSIGNED_SHORT_5_5_5_1:
         case KTX_UNSIGNED_SHORT_1_5_5_5_REV:
         case KTX_UNSIGNED_INT_8_8_8_8:
         case KTX_UNSIGNED_INT_8_8_8_8_REV:
         case KTX_UNSIGNED_INT_10_10_10_2:
         case KTX_UNSIGNED_INT_2_10_10_10_REV:
         case KTX_UNSIGNED_INT_24_8:
         case KTX_UNSIGNED_INT_10F_11F_11F_REV:
         case KTX_UNSIGNED_INT_5_9_9_9_REV:
            return true;
      }
      return false;
   }

   uint get_ogl_type_size(uint32 ogl_type)
   {
      switch (ogl_type)
      {
         case KTX_UNSIGNED_BYTE:
         case KTX_BYTE:
            return 1;
         case KTX_HALF_FLOAT:
         case KTX_UNSIGNED_SHORT:
         case KTX_SHORT:
            return 2;
         case KTX_FLOAT:
         case KTX_UNSIGNED_INT:
         case KTX_INT:
            return 4;
         case KTX_UNSIGNED_BYTE_3_3_2:
         case KTX_UNSIGNED_BYTE_2_3_3_REV:
            return 1;
         case KTX_UNSIGNED_SHORT_5_6_5:
         case KTX_UNSIGNED_SHORT_5_6_5_REV:
         case KTX_UNSIGNED_SHORT_4_4_4_4:
         case KTX_UNSIGNED_SHORT_4_4_4_4_REV:
         case KTX_UNSIGNED_SHORT_5_5_5_1:
         case KTX_UNSIGNED_SHORT_1_5_5_5_REV:
            return 2;
         case KTX_UNSIGNED_INT_8_8_8_8:
         case KTX_UNSIGNED_INT_8_8_8_8_REV:
         case KTX_UNSIGNED_INT_10_10_10_2:
         case KTX_UNSIGNED_INT_2_10_10_10_REV:
         case KTX_UNSIGNED_INT_24_8:
         case KTX_UNSIGNED_INT_10F_11F_11F_REV:
         case KTX_UNSIGNED_INT_5_9_9_9_REV:
            return 4;
      }
      return 0;
   }

   uint32 get_ogl_base_internal_fmt(uint32 ogl_fmt)
   {
      switch (ogl_fmt)
      {
         case KTX_ETC1_RGB8_OES:
         case KTX_RGB_S3TC:
         case KTX_RGB4_S3TC:
         case KTX_COMPRESSED_RGB_S3TC_DXT1_EXT:
         case KTX_COMPRESSED_SRGB_S3TC_DXT1_EXT:
            return KTX_RGB;
         case KTX_COMPRESSED_RGBA_S3TC_DXT1_EXT:
         case KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
         case KTX_RGBA_S3TC:
         case KTX_RGBA4_S3TC:
         case KTX_COMPRESSED_RGBA_S3TC_DXT3_EXT:
         case KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
         case KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT:
         case KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
         case KTX_RGBA_DXT5_S3TC:
         case KTX_RGBA4_DXT5_S3TC:
            return KTX_RGBA;
         case 1:
         case KTX_RED:
         case KTX_RED_INTEGER:
         case KTX_GREEN:
         case KTX_GREEN_INTEGER:
         case KTX_BLUE:
         case KTX_BLUE_INTEGER:
         case KTX_R8:
         case KTX_R8UI:
         case KTX_LUMINANCE8:
         case KTX_ALPHA:
         case KTX_LUMINANCE:
         case KTX_COMPRESSED_RED_RGTC1_EXT:
         case KTX_COMPRESSED_SIGNED_RED_RGTC1_EXT:
         case KTX_COMPRESSED_LUMINANCE_LATC1_EXT:
         case KTX_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT:
            return KTX_RED;
         case 2:
         case KTX_RG:
         case KTX_RG8:
         case KTX_RG_INTEGER:
         case KTX_LUMINANCE_ALPHA:
         case KTX_COMPRESSED_RED_GREEN_RGTC2_EXT:
         case KTX_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:
         case KTX_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT:
         case KTX_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT:
            return KTX_RG;
         case 3:
         case KTX_SRGB:
         case KTX_RGB:
         case KTX_RGB_INTEGER:
         case KTX_BGR:
         case KTX_BGR_INTEGER:
         case KTX_RGB8:
         case KTX_SRGB8:
            return KTX_RGB;
         case 4:
         case KTX_RGBA:
         case KTX_BGRA:
         case KTX_RGBA_INTEGER:
         case KTX_BGRA_INTEGER:
         case KTX_SRGB_ALPHA:
         case KTX_SRGB8_ALPHA8:
         case KTX_RGBA8:
            return KTX_RGBA;
      }
      return 0;
   }

   bool get_ogl_fmt_desc(uint32 ogl_fmt, uint32 ogl_type, uint& block_dim, uint& bytes_per_block)
   {
      uint ogl_type_size = get_ogl_type_size(ogl_type);

      block_dim = 1;
      bytes_per_block = 0;

      switch (ogl_fmt)
      {
         case KTX_COMPRESSED_RED_RGTC1_EXT:
         case KTX_COMPRESSED_SIGNED_RED_RGTC1_EXT:
         case KTX_COMPRESSED_LUMINANCE_LATC1_EXT:
         case KTX_COMPRESSED_SIGNED_LUMINANCE_LATC1_EXT:
         case KTX_ETC1_RGB8_OES:
         case KTX_RGB_S3TC:
         case KTX_RGB4_S3TC:
         case KTX_COMPRESSED_RGB_S3TC_DXT1_EXT:
         case KTX_COMPRESSED_RGBA_S3TC_DXT1_EXT:
         case KTX_COMPRESSED_SRGB_S3TC_DXT1_EXT:
         case KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT1_EXT:
         {
            block_dim = 4;
            bytes_per_block = 8;
            break;
         }
         case KTX_COMPRESSED_LUMINANCE_ALPHA_LATC2_EXT:
         case KTX_COMPRESSED_SIGNED_LUMINANCE_ALPHA_LATC2_EXT:
         case KTX_COMPRESSED_RED_GREEN_RGTC2_EXT:
         case KTX_COMPRESSED_SIGNED_RED_GREEN_RGTC2_EXT:
         case KTX_RGBA_S3TC:
         case KTX_RGBA4_S3TC:
         case KTX_COMPRESSED_RGBA_S3TC_DXT3_EXT:
         case KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT3_EXT:
         case KTX_COMPRESSED_RGBA_S3TC_DXT5_EXT:
         case KTX_COMPRESSED_SRGB_ALPHA_S3TC_DXT5_EXT:
         case KTX_RGBA_DXT5_S3TC:
         case KTX_RGBA4_DXT5_S3TC:
         {
            block_dim = 4;
            bytes_per_block = 16;
            break;
         }
         case 1:
         case KTX_ALPHA:
         case KTX_RED:
         case KTX_GREEN:
         case KTX_BLUE:
         case KTX_RED_INTEGER:
         case KTX_GREEN_INTEGER:
         case KTX_BLUE_INTEGER:
         case KTX_LUMINANCE:
         {
            bytes_per_block = ogl_type_size;
            break;
         }
         case KTX_R8:
         case KTX_R8UI:
         case KTX_ALPHA8:
         case KTX_LUMINANCE8:
         {
            bytes_per_block = 1;
            break;
         }
         case 2:
         case KTX_RG:
         case KTX_RG_INTEGER:
         case KTX_LUMINANCE_ALPHA:
         {
            bytes_per_block = 2 * ogl_type_size;
            break;
         }
         case KTX_RG8:
         case KTX_LUMINANCE8_ALPHA8:
         {
            bytes_per_block = 2;
            break;
         }
         case 3:
         case KTX_SRGB:
         case KTX_RGB:
         case KTX_BGR:
         case KTX_RGB_INTEGER:
         case KTX_BGR_INTEGER:
         {
            bytes_per_block = is_packed_pixel_ogl_type(ogl_type) ? ogl_type_size : (3 * ogl_type_size);
            break;
         }
         case KTX_RGB8:
         case KTX_SRGB8:
         {
            bytes_per_block = 3;
            break;
         }
         case 4:
         case KTX_RGBA:
         case KTX_BGRA:
         case KTX_RGBA_INTEGER:
         case KTX_BGRA_INTEGER:
         case KTX_SRGB_ALPHA:
         {
            bytes_per_block = is_packed_pixel_ogl_type(ogl_type) ? ogl_type_size : (4 * ogl_type_size);
            break;
         }
         case KTX_SRGB8_ALPHA8:
         case KTX_RGBA8:
         {
            bytes_per_block = 4;
            break;
         }
         default:
            return false;
      }
      return true;
   }

   bool ktx_texture::compute_pixel_info()
   {
      if ((!m_header.m_glType) || (!m_header.m_glFormat))
      {
         if ((m_header.m_glType) || (m_header.m_glFormat))
            return false;

         // Must be a compressed format.
         if (!get_ogl_fmt_desc(m_header.m_glInternalFormat, m_header.m_glType, m_block_dim, m_bytes_per_block))
         {
#if CRNLIB_KTX_PVRTEX_WORKAROUNDS
            if ((!m_header.m_glInternalFormat) && (!m_header.m_glType) && (!m_header.m_glTypeSize) && (!m_header.m_glBaseInternalFormat))
            {
               // PVRTexTool writes bogus headers when outputting ETC1.
               console::warning("ktx_texture::compute_pixel_info: Header doesn't specify any format, assuming ETC1 and hoping for the best");
               m_header.m_glBaseInternalFormat = KTX_RGB;
               m_header.m_glInternalFormat = KTX_ETC1_RGB8_OES;
               m_header.m_glTypeSize = 1;
               m_block_dim = 4;
               m_bytes_per_block = 8;
               return true;
            }
#endif
            return false;
         }

         if (m_block_dim == 1)
            return false;
      }
      else
      {
         // Must be an uncompressed format.
         if (!get_ogl_fmt_desc(m_header.m_glFormat, m_header.m_glType, m_block_dim, m_bytes_per_block))
            return false;

         if (m_block_dim > 1)
            return false;
      }
      return true;
   }

   bool ktx_texture::read_from_stream(data_stream_serializer& serializer)
   {
      clear();

      // Read header
      if (serializer.read(&m_header, 1, sizeof(m_header)) != sizeof(ktx_header))
         return false;

      // Check header
      if (memcmp(s_ktx_file_id, m_header.m_identifier, sizeof(m_header.m_identifier)))
         return false;

      if ((m_header.m_endianness != KTX_OPPOSITE_ENDIAN) && (m_header.m_endianness != KTX_ENDIAN))
         return false;

      m_opposite_endianness = (m_header.m_endianness == KTX_OPPOSITE_ENDIAN);
      if (m_opposite_endianness)
      {
         m_header.endian_swap();

         if ((m_header.m_glTypeSize != sizeof(uint8)) && (m_header.m_glTypeSize != sizeof(uint16)) && (m_header.m_glTypeSize != sizeof(uint32)))
            return false;
      }

      if (!check_header())
         return false;

      if (!compute_pixel_info())
         return false;

      uint8 pad_bytes[3];

      // Read the key value entries
      uint num_key_value_bytes_remaining = m_header.m_bytesOfKeyValueData;
      while (num_key_value_bytes_remaining)
      {
         if (num_key_value_bytes_remaining < sizeof(uint32))
            return false;

         uint32 key_value_byte_size;
         if (serializer.read(&key_value_byte_size, 1, sizeof(uint32)) != sizeof(uint32))
            return false;

         num_key_value_bytes_remaining -= sizeof(uint32);

         if (m_opposite_endianness)
            key_value_byte_size = utils::swap32(key_value_byte_size);

         if (key_value_byte_size > num_key_value_bytes_remaining)
            return false;

         uint8_vec key_value_data;
         if (key_value_byte_size)
         {
            key_value_data.resize(key_value_byte_size);
            if (serializer.read(&key_value_data[0], 1, key_value_byte_size) != key_value_byte_size)
               return false;
         }

         m_key_values.push_back(key_value_data);

         uint padding = 3 - ((key_value_byte_size + 3) % 4);
         if (padding)
         {
            if (serializer.read(pad_bytes, 1, padding) != padding)
               return false;
         }

         num_key_value_bytes_remaining -= key_value_byte_size;
         if (num_key_value_bytes_remaining < padding)
            return false;
         num_key_value_bytes_remaining -= padding;
      }

      // Now read the mip levels
      uint total_faces = get_num_mips() * get_array_size() * get_num_faces() * get_depth();
      if ((!total_faces) || (total_faces > 65535))
         return false;

      // See Section 2.8 of KTX file format: No rounding to block sizes should be applied for block compressed textures.
      // OK, I'm going to break that rule otherwise KTX can only store a subset of textures that DDS can handle for no good reason.
#if 0
      const uint mip0_row_blocks = m_header.m_pixelWidth / m_block_dim;
      const uint mip0_col_blocks = CRNLIB_MAX(1, m_header.m_pixelHeight) / m_block_dim;
#else
      const uint mip0_row_blocks = (m_header.m_pixelWidth + m_block_dim - 1) / m_block_dim;
      const uint mip0_col_blocks = (CRNLIB_MAX(1, m_header.m_pixelHeight) + m_block_dim - 1) / m_block_dim;
#endif
      if ((!mip0_row_blocks) || (!mip0_col_blocks))
         return false;

      const uint mip0_depth = CRNLIB_MAX(1, m_header.m_pixelDepth); mip0_depth;

      bool has_valid_image_size_fields = true;
      bool disable_mip_and_cubemap_padding = false;

#if CRNLIB_KTX_PVRTEX_WORKAROUNDS
      {
         // PVRTexTool has a bogus KTX writer that doesn't write any imageSize fields. Nice.
         size_t expected_bytes_remaining = 0;
         for (uint mip_level = 0; mip_level < get_num_mips(); mip_level++)
         {
            uint mip_width, mip_height, mip_depth;
            get_mip_dim(mip_level, mip_width, mip_height, mip_depth);

            const uint mip_row_blocks = (mip_width + m_block_dim - 1) / m_block_dim;
            const uint mip_col_blocks = (mip_height + m_block_dim - 1) / m_block_dim;
            if ((!mip_row_blocks) || (!mip_col_blocks))
               return false;

            expected_bytes_remaining += sizeof(uint32);

            if ((!m_header.m_numberOfArrayElements) && (get_num_faces() == 6))
            {
               for (uint face = 0; face < get_num_faces(); face++)
               {
                  uint slice_size = mip_row_blocks * mip_col_blocks * m_bytes_per_block;
                  expected_bytes_remaining += slice_size;

                  uint num_cube_pad_bytes = 3 - ((slice_size + 3) % 4);
                  expected_bytes_remaining += num_cube_pad_bytes;
               }
            }
            else
            {
               uint total_mip_size = 0;
               for (uint array_element = 0; array_element < get_array_size(); array_element++)
               {
                  for (uint face = 0; face < get_num_faces(); face++)
                  {
                     for (uint zslice = 0; zslice < mip_depth; zslice++)
                     {
                        uint slice_size = mip_row_blocks * mip_col_blocks * m_bytes_per_block;
                        total_mip_size += slice_size;
                     }
                  }
               }
               expected_bytes_remaining += total_mip_size;

               uint num_mip_pad_bytes = 3 - ((total_mip_size + 3) % 4);
               expected_bytes_remaining += num_mip_pad_bytes;
            }
         }

         if (serializer.get_stream()->get_remaining() < expected_bytes_remaining)
         {
            has_valid_image_size_fields = false;
            disable_mip_and_cubemap_padding = true;
            console::warning("ktx_texture::read_from_stream: KTX file size is smaller than expected - trying to read anyway without imageSize fields");
         }
      }
#endif

      for (uint mip_level = 0; mip_level < get_num_mips(); mip_level++)
      {
         uint mip_width, mip_height, mip_depth;
         get_mip_dim(mip_level, mip_width, mip_height, mip_depth);

         const uint mip_row_blocks = (mip_width + m_block_dim - 1) / m_block_dim;
         const uint mip_col_blocks = (mip_height + m_block_dim - 1) / m_block_dim;
         if ((!mip_row_blocks) || (!mip_col_blocks))
            return false;

         uint32 image_size = 0;
         if (!has_valid_image_size_fields)
            image_size = mip_depth * mip_row_blocks * mip_col_blocks * m_bytes_per_block * get_array_size() * get_num_faces();
         else
         {
            if (serializer.read(&image_size, 1, sizeof(image_size)) != sizeof(image_size))
               return false;

            if (m_opposite_endianness)
               image_size = utils::swap32(image_size);
         }

         if (!image_size)
            return false;

         uint total_mip_size = 0;

         if ((!m_header.m_numberOfArrayElements) && (get_num_faces() == 6))
         {
            // plain non-array cubemap
            for (uint face = 0; face < get_num_faces(); face++)
            {
               CRNLIB_ASSERT(m_image_data.size() == get_image_index(mip_level, 0, face, 0));

               m_image_data.push_back(uint8_vec());
               uint8_vec& image_data = m_image_data.back();

               image_data.resize(image_size);
               if (serializer.read(&image_data[0], 1, image_size) != image_size)
                  return false;

               if (m_opposite_endianness)
                  utils::endian_swap_mem(&image_data[0], image_size, m_header.m_glTypeSize);

               uint num_cube_pad_bytes = disable_mip_and_cubemap_padding ? 0 : (3 - ((image_size + 3) % 4));
               if (serializer.read(pad_bytes, 1, num_cube_pad_bytes) != num_cube_pad_bytes)
                  return false;

               total_mip_size += image_size + num_cube_pad_bytes;
            }
         }
         else
         {
            // 1D, 2D, 3D (normal or array texture), or array cubemap
            uint num_image_bytes_remaining = image_size;

            for (uint array_element = 0; array_element < get_array_size(); array_element++)
            {
               for (uint face = 0; face < get_num_faces(); face++)
               {
                  for (uint zslice = 0; zslice < mip_depth; zslice++)
                  {
                     CRNLIB_ASSERT(m_image_data.size() == get_image_index(mip_level, array_element, face, zslice));

                     uint slice_size = mip_row_blocks * mip_col_blocks * m_bytes_per_block;
                     if ((!slice_size) || (slice_size > num_image_bytes_remaining))
                        return false;

                     m_image_data.push_back(uint8_vec());
                     uint8_vec& image_data = m_image_data.back();

                     image_data.resize(slice_size);
                     if (serializer.read(&image_data[0], 1, slice_size) != slice_size)
                        return false;

                     if (m_opposite_endianness)
                        utils::endian_swap_mem(&image_data[0], slice_size, m_header.m_glTypeSize);

                     num_image_bytes_remaining -= slice_size;

                     total_mip_size += slice_size;
                  }
               }
            }

            if (num_image_bytes_remaining)
               return false;
         }

         uint num_mip_pad_bytes = disable_mip_and_cubemap_padding ? 0 : (3 - ((total_mip_size + 3) % 4));
         if (serializer.read(pad_bytes, 1, num_mip_pad_bytes) != num_mip_pad_bytes)
            return false;
      }
      return true;
   }

   bool ktx_texture::write_to_stream(data_stream_serializer& serializer, bool no_keyvalue_data)
   {
      if (!consistency_check())
      {
         CRNLIB_ASSERT(0);
         return false;
      }

      memcpy(m_header.m_identifier, s_ktx_file_id, sizeof(m_header.m_identifier));
      m_header.m_endianness = m_opposite_endianness ? KTX_OPPOSITE_ENDIAN : KTX_ENDIAN;

      if (m_block_dim == 1)
      {
         m_header.m_glTypeSize = get_ogl_type_size(m_header.m_glType);
         m_header.m_glBaseInternalFormat = m_header.m_glFormat;
      }
      else
      {
         m_header.m_glBaseInternalFormat = get_ogl_base_internal_fmt(m_header.m_glInternalFormat);
      }

      m_header.m_bytesOfKeyValueData = 0;
      if (!no_keyvalue_data)
      {
         for (uint i = 0; i < m_key_values.size(); i++)
            m_header.m_bytesOfKeyValueData += sizeof(uint32) + ((m_key_values[i].size() + 3) & ~3);
      }

      if (m_opposite_endianness)
         m_header.endian_swap();

      bool success = (serializer.write(&m_header, sizeof(m_header), 1) == 1);

      if (m_opposite_endianness)
         m_header.endian_swap();

      if (!success)
         return success;

      uint total_key_value_bytes = 0;
      const uint8 padding[3] = { 0, 0, 0 };

      if (!no_keyvalue_data)
      {
         for (uint i = 0; i < m_key_values.size(); i++)
         {
            uint32 key_value_size = m_key_values[i].size();

            if (m_opposite_endianness)
               key_value_size = utils::swap32(key_value_size);

            success = (serializer.write(&key_value_size, sizeof(key_value_size), 1) == 1);
            total_key_value_bytes += sizeof(key_value_size);

            if (m_opposite_endianness)
               key_value_size = utils::swap32(key_value_size);

            if (!success)
               return false;

            if (key_value_size)
            {
               if (serializer.write(&m_key_values[i][0], key_value_size, 1) != 1)
                  return false;
               total_key_value_bytes += key_value_size;

               uint num_padding = 3 - ((key_value_size + 3) % 4);
               if ((num_padding) && (serializer.write(padding, num_padding, 1) != 1))
                  return false;
               total_key_value_bytes += num_padding;
            }
         }
         (void)total_key_value_bytes;
      }

      CRNLIB_ASSERT(total_key_value_bytes == m_header.m_bytesOfKeyValueData);

      for (uint mip_level = 0; mip_level < get_num_mips(); mip_level++)
      {
         uint mip_width, mip_height, mip_depth;
         get_mip_dim(mip_level, mip_width, mip_height, mip_depth);

         const uint mip_row_blocks = (mip_width + m_block_dim - 1) / m_block_dim;
         const uint mip_col_blocks = (mip_height + m_block_dim - 1) / m_block_dim;
         if ((!mip_row_blocks) || (!mip_col_blocks))
            return false;

         uint32 image_size = mip_row_blocks * mip_col_blocks * m_bytes_per_block;
         if ((m_header.m_numberOfArrayElements) || (get_num_faces() == 1))
            image_size *= (get_array_size() * get_num_faces() * get_depth());

         if (!image_size)
            return false;

         if (m_opposite_endianness)
            image_size = utils::swap32(image_size);

         success = (serializer.write(&image_size, sizeof(image_size), 1) == 1);

         if (m_opposite_endianness)
            image_size = utils::swap32(image_size);

         if (!success)
            return false;

         uint total_mip_size = 0;

         if ((!m_header.m_numberOfArrayElements) && (get_num_faces() == 6))
         {
            // plain non-array cubemap
            for (uint face = 0; face < get_num_faces(); face++)
            {
               const uint8_vec& image_data = get_image_data(get_image_index(mip_level, 0, face, 0));
               if ((!image_data.size()) || (image_data.size() != image_size))
                  return false;

               if (m_opposite_endianness)
               {
                  uint8_vec tmp_image_data(image_data);
                  utils::endian_swap_mem(&tmp_image_data[0], tmp_image_data.size(), m_header.m_glTypeSize);
                  if (serializer.write(&tmp_image_data[0], tmp_image_data.size(), 1) != 1)
                     return false;
               }
               else if (serializer.write(&image_data[0], image_data.size(), 1) != 1)
                  return false;

               uint num_cube_pad_bytes = 3 - ((image_data.size() + 3) % 4);
               if ((num_cube_pad_bytes) && (serializer.write(padding, num_cube_pad_bytes, 1) != 1))
                  return false;

               total_mip_size += image_size + num_cube_pad_bytes;
            }
         }
         else
         {
            // 1D, 2D, 3D (normal or array texture), or array cubemap
            for (uint array_element = 0; array_element < get_array_size(); array_element++)
            {
               for (uint face = 0; face < get_num_faces(); face++)
               {
                  for (uint zslice = 0; zslice < mip_depth; zslice++)
                  {
                     const uint8_vec& image_data = get_image_data(get_image_index(mip_level, array_element, face, zslice));
                     if (!image_data.size())
                        return false;

                     if (m_opposite_endianness)
                     {
                        uint8_vec tmp_image_data(image_data);
                        utils::endian_swap_mem(&tmp_image_data[0], tmp_image_data.size(), m_header.m_glTypeSize);
                        if (serializer.write(&tmp_image_data[0], tmp_image_data.size(), 1) != 1)
                           return false;
                     }
                     else if (serializer.write(&image_data[0], image_data.size(), 1) != 1)
                        return false;

                     total_mip_size += image_data.size();
                  }
               }
            }

            uint num_mip_pad_bytes = 3 - ((total_mip_size + 3) % 4);
            if ((num_mip_pad_bytes) && (serializer.write(padding, num_mip_pad_bytes, 1) != 1))
               return false;
            total_mip_size += num_mip_pad_bytes;
         }
         CRNLIB_ASSERT((total_mip_size & 3) == 0);
      }

      return true;
   }

   bool ktx_texture::init_2D(uint width, uint height, uint num_mips, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type)
   {
      clear();

      m_header.m_pixelWidth = width;
      m_header.m_pixelHeight = height;
      m_header.m_numberOfMipmapLevels = num_mips;
      m_header.m_glInternalFormat = ogl_internal_fmt;
      m_header.m_glFormat = ogl_fmt;
      m_header.m_glType = ogl_type;
      m_header.m_numberOfFaces = 1;

      if (!compute_pixel_info())
         return false;

      return true;
   }

   bool ktx_texture::init_2D_array(uint width, uint height, uint num_mips, uint array_size, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type)
   {
      clear();

      m_header.m_pixelWidth = width;
      m_header.m_pixelHeight = height;
      m_header.m_numberOfMipmapLevels = num_mips;
      m_header.m_numberOfArrayElements = array_size;
      m_header.m_glInternalFormat = ogl_internal_fmt;
      m_header.m_glFormat = ogl_fmt;
      m_header.m_glType = ogl_type;
      m_header.m_numberOfFaces = 1;

      if (!compute_pixel_info())
         return false;

      return true;
   }

   bool ktx_texture::init_3D(uint width, uint height, uint depth, uint num_mips, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type)
   {
      clear();

      m_header.m_pixelWidth = width;
      m_header.m_pixelHeight = height;
      m_header.m_pixelDepth = depth;
      m_header.m_numberOfMipmapLevels = num_mips;
      m_header.m_glInternalFormat = ogl_internal_fmt;
      m_header.m_glFormat = ogl_fmt;
      m_header.m_glType = ogl_type;
      m_header.m_numberOfFaces = 1;

      if (!compute_pixel_info())
         return false;

      return true;
   }

   bool ktx_texture::init_cubemap(uint dim, uint num_mips, uint32 ogl_internal_fmt, uint32 ogl_fmt, uint32 ogl_type)
   {
      clear();

      m_header.m_pixelWidth = dim;
      m_header.m_pixelHeight = dim;
      m_header.m_numberOfMipmapLevels = num_mips;
      m_header.m_glInternalFormat = ogl_internal_fmt;
      m_header.m_glFormat = ogl_fmt;
      m_header.m_glType = ogl_type;
      m_header.m_numberOfFaces = 6;

      if (!compute_pixel_info())
         return false;

      return true;
   }

   bool ktx_texture::check_header() const
   {
      if (((get_num_faces() != 1) && (get_num_faces() != 6)) || (!m_header.m_pixelWidth))
         return false;

      if ((!m_header.m_pixelHeight) && (m_header.m_pixelDepth))
         return false;

      if ((get_num_faces() == 6) && ((m_header.m_pixelDepth) || (!m_header.m_pixelHeight)))
         return false;

      if (m_header.m_numberOfMipmapLevels)
      {
         const uint max_mipmap_dimension = 1U << (m_header.m_numberOfMipmapLevels - 1U);
         if (max_mipmap_dimension > (CRNLIB_MAX(CRNLIB_MAX(m_header.m_pixelWidth, m_header.m_pixelHeight), m_header.m_pixelDepth)))
            return false;
      }

      return true;
   }

   bool ktx_texture::consistency_check() const
   {
      if (!check_header())
         return false;

      uint block_dim = 0, bytes_per_block = 0;
      if ((!m_header.m_glType) || (!m_header.m_glFormat))
      {
         if ((m_header.m_glType) || (m_header.m_glFormat))
            return false;
         if (!get_ogl_fmt_desc(m_header.m_glInternalFormat, m_header.m_glType, block_dim, bytes_per_block))
            return false;
         if (block_dim == 1)
            return false;
         //if ((get_width() % block_dim) || (get_height() % block_dim))
         //   return false;
      }
      else
      {
         if (!get_ogl_fmt_desc(m_header.m_glFormat, m_header.m_glType, block_dim, bytes_per_block))
            return false;
         if (block_dim > 1)
            return false;
      }
      if ((m_block_dim != block_dim) || (m_bytes_per_block != bytes_per_block))
         return false;

      if (m_image_data.size() != get_total_images())
         return false;

      for (uint mip_level = 0; mip_level < get_num_mips(); mip_level++)
      {
         uint mip_width, mip_height, mip_depth;
         get_mip_dim(mip_level, mip_width, mip_height, mip_depth);

         const uint mip_row_blocks = (mip_width + m_block_dim - 1) / m_block_dim;
         const uint mip_col_blocks = (mip_height + m_block_dim - 1) / m_block_dim;
         if ((!mip_row_blocks) || (!mip_col_blocks))
            return false;

         for (uint array_element = 0; array_element < get_array_size(); array_element++)
         {
            for (uint face = 0; face < get_num_faces(); face++)
            {
               for (uint zslice = 0; zslice < mip_depth; zslice++)
               {
                  const uint8_vec& image_data = get_image_data(get_image_index(mip_level, array_element, face, zslice));

                  uint expected_image_size = mip_row_blocks * mip_col_blocks * m_bytes_per_block;
                  if (image_data.size() != expected_image_size)
                     return false;
               }
            }
         }
      }

      return true;
   }

   const uint8_vec* ktx_texture::find_key(const char* pKey) const
   {
      const size_t n = strlen(pKey) + 1;
      for (uint i = 0; i < m_key_values.size(); i++)
      {
         const uint8_vec& v = m_key_values[i];
         if ((v.size() >= n) && (!memcmp(&v[0], pKey, n)))
            return &v;
      }

      return NULL;
   }

   bool ktx_texture::get_key_value_as_string(const char* pKey, dynamic_string& str) const
   {
      const uint8_vec* p = find_key(pKey);
      if (!p)
      {
         str.clear();
         return false;
      }

      const uint ofs = (static_cast<uint>(strlen(pKey)) + 1);
      const uint8* pValue = p->get_ptr() + ofs;
      const uint n = p->size() - ofs;

      uint i;
      for (i = 0; i < n; i++)
         if (!pValue[i])
            break;

      str.set_from_buf(pValue, i);
      return true;
   }

   uint ktx_texture::add_key_value(const char* pKey, const void* pVal, uint val_size)
   {
      const uint idx = m_key_values.size();
      m_key_values.resize(idx + 1);
      uint8_vec& v = m_key_values.back();
      v.append(reinterpret_cast<const uint8*>(pKey), static_cast<uint>(strlen(pKey)) + 1);
      v.append(static_cast<const uint8*>(pVal), val_size);
      return idx;
   }

} // namespace crnlib
