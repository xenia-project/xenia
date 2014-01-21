// File: crn_dxt.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "../inc/crnlib.h"
#include "crn_color.h"
#include "crn_vec.h"
#include "crn_rand.h"
#include "crn_sparse_bit_array.h"
#include "crn_hash_map.h"
#include <map>

#define CRNLIB_DXT_ALT_ROUNDING 1

namespace crnlib
{
   enum dxt_constants
   {
      cDXT1BytesPerBlock = 8U,
      cDXT5NBytesPerBlock = 16U,

      cDXT5SelectorBits = 3U,
      cDXT5SelectorValues = 1U << cDXT5SelectorBits,
      cDXT5SelectorMask = cDXT5SelectorValues - 1U,

      cDXT1SelectorBits = 2U,
      cDXT1SelectorValues = 1U << cDXT1SelectorBits,
      cDXT1SelectorMask = cDXT1SelectorValues - 1U,

      cDXTBlockShift = 2U,
      cDXTBlockSize = 1U << cDXTBlockShift
   };

   enum dxt_format
   {
      cDXTInvalid = -1,

      // cDXT1/1A must appear first!
      cDXT1,
      cDXT1A,

      cDXT3,
      cDXT5,
      cDXT5A,

      cDXN_XY,    // inverted relative to standard ATI2, 360's DXN
      cDXN_YX,    // standard ATI2,

      cETC1       // Ericsson texture compression (color only, 4x4 blocks, 4bpp, 64-bits/block)
   };

   const float cDXT1MaxLinearValue = 3.0f;
   const float cDXT1InvMaxLinearValue = 1.0f/3.0f;

   const float cDXT5MaxLinearValue = 7.0f;
   const float cDXT5InvMaxLinearValue = 1.0f/7.0f;

   // Converts DXT1 raw color selector index to a linear value.
   extern const uint8 g_dxt1_to_linear[cDXT1SelectorValues];

   // Converts DXT5 raw alpha selector index to a linear value.
   extern const uint8 g_dxt5_to_linear[cDXT5SelectorValues];

   // Converts DXT1 linear color selector index to a raw value (inverse of g_dxt1_to_linear).
   extern const uint8 g_dxt1_from_linear[cDXT1SelectorValues];

   // Converts DXT5 linear alpha selector index to a raw value (inverse of g_dxt5_to_linear).
   extern const uint8 g_dxt5_from_linear[cDXT5SelectorValues];

   extern const uint8 g_dxt5_alpha6_to_linear[cDXT5SelectorValues];

   extern const uint8 g_six_alpha_invert_table[cDXT5SelectorValues];
   extern const uint8 g_eight_alpha_invert_table[cDXT5SelectorValues];

   const char* get_dxt_format_string(dxt_format fmt);
   uint get_dxt_format_bits_per_pixel(dxt_format fmt);
   bool get_dxt_format_has_alpha(dxt_format fmt);

   const char* get_dxt_quality_string(crn_dxt_quality q);

   const char* get_dxt_compressor_name(crn_dxt_compressor_type c);

   struct dxt1_block
   {
      uint8 m_low_color[2];
      uint8 m_high_color[2];

      enum { cNumSelectorBytes = 4 };
      uint8 m_selectors[cNumSelectorBytes];

      inline void clear()
      {
         utils::zero_this(this);
      }

      // These methods assume the in-memory rep is in LE byte order.
      inline uint get_low_color() const
      {
         return m_low_color[0] | (m_low_color[1] << 8U);
      }

      inline uint get_high_color() const
      {
         return m_high_color[0] | (m_high_color[1] << 8U);
      }

      inline void set_low_color(uint16 c)
      {
         m_low_color[0] = static_cast<uint8>(c & 0xFF);
         m_low_color[1] = static_cast<uint8>((c >> 8) & 0xFF);
      }

      inline void set_high_color(uint16 c)
      {
         m_high_color[0] = static_cast<uint8>(c & 0xFF);
         m_high_color[1] = static_cast<uint8>((c >> 8) & 0xFF);
      }

      inline bool is_constant_color_block() const { return get_low_color() == get_high_color(); }
      inline bool is_alpha_block() const { return get_low_color() <= get_high_color(); }
      inline bool is_non_alpha_block() const { return !is_alpha_block(); }

      inline uint get_selector(uint x, uint y) const
      {
         CRNLIB_ASSERT((x < 4U) && (y < 4U));
         return (m_selectors[y] >> (x * cDXT1SelectorBits)) & cDXT1SelectorMask;
      }

      inline void set_selector(uint x, uint y, uint val)
      {
         CRNLIB_ASSERT((x < 4U) && (y < 4U) && (val < 4U));

         m_selectors[y] &= (~(cDXT1SelectorMask << (x * cDXT1SelectorBits)));
         m_selectors[y] |= (val << (x * cDXT1SelectorBits));
      }

      inline void flip_x(uint w = 4, uint h = 4)
      {
         for (uint x = 0; x < (w / 2); x++)
         {
            for (uint y = 0; y < h; y++)
            {
               const uint c = get_selector(x, y);
               set_selector(x, y, get_selector((w - 1) - x, y));
               set_selector((w - 1) - x, y, c);
            }
         }
      }

      inline void flip_y(uint w = 4, uint h = 4)
      {
         for (uint y = 0; y < (h / 2); y++)
         {
            for (uint x = 0; x < w; x++)
            {
               const uint c = get_selector(x, y);
               set_selector(x, y, get_selector(x, (h - 1) - y));
               set_selector(x, (h - 1) - y, c);
            }
         }
      }
      
      static uint16        pack_color(const color_quad_u8& color, bool scaled, uint bias = 127U);
      static uint16        pack_color(uint r, uint g, uint b, bool scaled, uint bias = 127U);

      static color_quad_u8 unpack_color(uint16 packed_color, bool scaled, uint alpha = 255U);
      static void          unpack_color(uint& r, uint& g, uint& b, uint16 packed_color, bool scaled);

      static uint          get_block_colors3(color_quad_u8* pDst, uint16 color0, uint16 color1);
      static uint          get_block_colors3_round(color_quad_u8* pDst, uint16 color0, uint16 color1);

      static uint          get_block_colors4(color_quad_u8* pDst, uint16 color0, uint16 color1);
      static uint          get_block_colors4_round(color_quad_u8* pDst, uint16 color0, uint16 color1);

      // pDst must point to an array at least cDXT1SelectorValues long.
      static uint          get_block_colors(color_quad_u8* pDst, uint16 color0, uint16 color1);

      static uint          get_block_colors_round(color_quad_u8* pDst, uint16 color0, uint16 color1);

      static color_quad_u8 unpack_endpoint(uint32 endpoints, uint index, bool scaled, uint alpha = 255U);
      static uint          pack_endpoints(uint lo, uint hi);

      static void          get_block_colors_NV5x(color_quad_u8* pDst, uint16 packed_col0, uint16 packed_col1, bool color4);
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt1_block);

   struct dxt3_block
   {
      enum { cNumAlphaBytes = 8 };
      uint8 m_alpha[cNumAlphaBytes];

      void set_alpha(uint x, uint y, uint value, bool scaled);
      uint get_alpha(uint x, uint y, bool scaled) const;

      inline void flip_x(uint w = 4, uint h = 4)
      {
         for (uint x = 0; x < (w / 2); x++)
         {
            for (uint y = 0; y < h; y++)
            {
               const uint c = get_alpha(x, y, false);
               set_alpha(x, y, get_alpha((w - 1) - x, y, false), false);
               set_alpha((w - 1) - x, y, c, false);
            }
         }
      }

      inline void flip_y(uint w = 4, uint h = 4)
      {
         for (uint y = 0; y < (h / 2); y++)
         {
            for (uint x = 0; x < w; x++)
            {
               const uint c = get_alpha(x, y, false);
               set_alpha(x, y, get_alpha(x, (h - 1) - y, false), false);
               set_alpha(x, (h - 1) - y, c, false);
            }
         }
      }
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt3_block);

   struct dxt5_block
   {
      uint8 m_endpoints[2];

      enum { cNumSelectorBytes = 6 };
      uint8 m_selectors[cNumSelectorBytes];

      inline void clear()
      {
         utils::zero_this(this);
      }

      inline uint get_low_alpha() const
      {
         return m_endpoints[0];
      }

      inline uint get_high_alpha() const
      {
         return m_endpoints[1];
      }

      inline void set_low_alpha(uint i)
      {
         CRNLIB_ASSERT(i <= cUINT8_MAX);
         m_endpoints[0] = static_cast<uint8>(i);
      }

      inline void set_high_alpha(uint i)
      {
         CRNLIB_ASSERT(i <= cUINT8_MAX);
         m_endpoints[1] = static_cast<uint8>(i);
      }

      inline bool is_alpha6_block() const { return get_low_alpha() <= get_high_alpha(); }

      uint get_endpoints_as_word() const { return m_endpoints[0] | (m_endpoints[1] << 8); }
      uint get_selectors_as_word(uint index) { CRNLIB_ASSERT(index < 3); return m_selectors[index * 2] | (m_selectors[index * 2 + 1] << 8); }

      inline uint get_selector(uint x, uint y) const
      {
         CRNLIB_ASSERT((x < 4U) && (y < 4U));

         uint selector_index = (y * 4) + x;
         uint bit_index = selector_index * cDXT5SelectorBits;

         uint byte_index = bit_index >> 3;
         uint bit_ofs = bit_index & 7;

         uint v = m_selectors[byte_index];
         if (byte_index < (cNumSelectorBytes - 1))
            v |= (m_selectors[byte_index + 1] << 8);

         return (v >> bit_ofs) & 7;
      }

      inline void set_selector(uint x, uint y, uint val)
      {
         CRNLIB_ASSERT((x < 4U) && (y < 4U) && (val < 8U));

         uint selector_index = (y * 4) + x;
         uint bit_index = selector_index * cDXT5SelectorBits;

         uint byte_index = bit_index >> 3;
         uint bit_ofs = bit_index & 7;

         uint v = m_selectors[byte_index];
         if (byte_index < (cNumSelectorBytes - 1))
            v |= (m_selectors[byte_index + 1] << 8);

         v &= (~(7 << bit_ofs));
         v |= (val << bit_ofs);

         m_selectors[byte_index] = static_cast<uint8>(v);
         if (byte_index < (cNumSelectorBytes - 1))
            m_selectors[byte_index + 1] = static_cast<uint8>(v >> 8);
      }

      inline void flip_x(uint w = 4, uint h = 4)
      {
         for (uint x = 0; x < (w / 2); x++)
         {
            for (uint y = 0; y < h; y++)
            {
               const uint c = get_selector(x, y);
               set_selector(x, y, get_selector((w - 1) - x, y));
               set_selector((w - 1) - x, y, c);
            }
         }
      }

      inline void flip_y(uint w = 4, uint h = 4)
      {
         for (uint y = 0; y < (h / 2); y++)
         {
            for (uint x = 0; x < w; x++)
            {
               const uint c = get_selector(x, y);
               set_selector(x, y, get_selector(x, (h - 1) - y));
               set_selector(x, (h - 1) - y, c);
            }
         }
      }

      enum { cMaxSelectorValues = 8 };

      // Results written to alpha channel.
      static uint          get_block_values6(color_quad_u8* pDst, uint l, uint h);
      static uint          get_block_values8(color_quad_u8* pDst, uint l, uint h);
      static uint          get_block_values(color_quad_u8* pDst, uint l, uint h);

      static uint          get_block_values6(uint* pDst, uint l, uint h);
      static uint          get_block_values8(uint* pDst, uint l, uint h);
      // pDst must point to an array at least cDXT5SelectorValues long.
      static uint          get_block_values(uint* pDst, uint l, uint h);

      static uint          unpack_endpoint(uint packed, uint index);
      static uint          pack_endpoints(uint lo, uint hi);
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt5_block);

   struct dxt_pixel_block
   {
      color_quad_u8 m_pixels[cDXTBlockSize][cDXTBlockSize]; // [y][x]

      inline void clear()
      {
         utils::zero_object(*this);
      }
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(dxt_pixel_block);

} // namespace crnlib



