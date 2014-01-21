// File: crn_etc.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "../inc/crnlib.h"
#include "crn_dxt.h"

namespace crnlib
{
   enum etc_constants
   {
      cETC1BytesPerBlock = 8U,

      cETC1SelectorBits = 2U,
      cETC1SelectorValues = 1U << cETC1SelectorBits,
      cETC1SelectorMask = cETC1SelectorValues - 1U,

      cETC1BlockShift = 2U,
      cETC1BlockSize = 1U << cETC1BlockShift,

      cETC1LSBSelectorIndicesBitOffset = 0,
      cETC1MSBSelectorIndicesBitOffset = 16,
      
      cETC1FlipBitOffset = 32,
      cETC1DiffBitOffset = 33,
      
      cETC1IntenModifierNumBits = 3,
      cETC1IntenModifierValues = 1 << cETC1IntenModifierNumBits,
      cETC1RightIntenModifierTableBitOffset = 34,
      cETC1LeftIntenModifierTableBitOffset = 37,

      // Base+Delta encoding (5 bit bases, 3 bit delta)
      cETC1BaseColorCompNumBits = 5,
      cETC1BaseColorCompMax = 1 << cETC1BaseColorCompNumBits,

      cETC1DeltaColorCompNumBits = 3,
      cETC1DeltaColorComp = 1 << cETC1DeltaColorCompNumBits,
      cETC1DeltaColorCompMax = 1 << cETC1DeltaColorCompNumBits,

      cETC1BaseColor5RBitOffset = 59,
      cETC1BaseColor5GBitOffset = 51,
      cETC1BaseColor5BBitOffset = 43,

      cETC1DeltaColor3RBitOffset = 56,
      cETC1DeltaColor3GBitOffset = 48,
      cETC1DeltaColor3BBitOffset = 40,
      
      // Absolute (non-delta) encoding (two 4-bit per component bases)
      cETC1AbsColorCompNumBits = 4,
      cETC1AbsColorCompMax = 1 << cETC1AbsColorCompNumBits,

      cETC1AbsColor4R1BitOffset = 60,
      cETC1AbsColor4G1BitOffset = 52,
      cETC1AbsColor4B1BitOffset = 44,

      cETC1AbsColor4R2BitOffset = 56,
      cETC1AbsColor4G2BitOffset = 48,
      cETC1AbsColor4B2BitOffset = 40,

      cETC1ColorDeltaMin = -4,
      cETC1ColorDeltaMax = 3,

      // Delta3:
      // 0   1   2   3   4   5   6   7
      // 000 001 010 011 100 101 110 111
      // 0   1   2   3   -4  -3  -2  -1
   };

   extern const int g_etc1_inten_tables[cETC1IntenModifierValues][cETC1SelectorValues];
   extern const uint8 g_etc1_to_selector_index[cETC1SelectorValues];
   extern const uint8 g_selector_index_to_etc1[cETC1SelectorValues];

   struct etc1_coord2
   { 
      uint8 m_x, m_y; 
   };
   extern const etc1_coord2 g_etc1_pixel_coords[2][2][8]; // [flipped][subblock][subblock_pixel]

   struct etc1_block
   {
      // big endian uint64:
      // bit ofs:  56  48  40  32  24  16   8   0
      // byte ofs: b0, b1, b2, b3, b4, b5, b6, b7 
      union 
      {
         uint64 m_uint64;
         uint8 m_bytes[8];
      };

      uint8 m_low_color[2];
      uint8 m_high_color[2];

      enum { cNumSelectorBytes = 4 };
      uint8 m_selectors[cNumSelectorBytes];

      inline void clear()
      {
         utils::zero_this(this);
      }

      inline uint get_general_bits(uint ofs, uint num) const
      {
         CRNLIB_ASSERT((ofs + num) <= 64U);
         CRNLIB_ASSERT(num && (num < 32U));
         return (utils::read_be64(&m_uint64) >> ofs) & ((1UL << num) - 1UL);
      }

      inline void set_general_bits(uint ofs, uint num, uint bits)
      {
         CRNLIB_ASSERT((ofs + num) <= 64U);
         CRNLIB_ASSERT(num && (num < 32U));
         
         uint64 x = utils::read_be64(&m_uint64);
         uint64 msk = ((1ULL << static_cast<uint64>(num)) - 1ULL) << static_cast<uint64>(ofs);
         x &= ~msk;
         x |= (static_cast<uint64>(bits) << static_cast<uint64>(ofs));
         utils::write_be64(&m_uint64, x);
      }

      inline uint get_byte_bits(uint ofs, uint num) const
      {
         CRNLIB_ASSERT((ofs + num) <= 64U);
         CRNLIB_ASSERT(num && (num <= 8U));
         CRNLIB_ASSERT((ofs >> 3) == ((ofs + num - 1) >> 3));
         const uint byte_ofs = 7 - (ofs >> 3);
         const uint byte_bit_ofs = ofs & 7;
         return (m_bytes[byte_ofs] >> byte_bit_ofs) & ((1 << num) - 1);
      }

      inline void set_byte_bits(uint ofs, uint num, uint bits)
      {
         CRNLIB_ASSERT((ofs + num) <= 64U);
         CRNLIB_ASSERT(num && (num < 32U));
         CRNLIB_ASSERT((ofs >> 3) == ((ofs + num - 1) >> 3));
         CRNLIB_ASSERT(bits < (1U << num));
         const uint byte_ofs = 7 - (ofs >> 3);
         const uint byte_bit_ofs = ofs & 7;
         const uint mask = (1 << num) - 1;
         m_bytes[byte_ofs] &= ~(mask << byte_bit_ofs);
         m_bytes[byte_ofs] |= (bits << byte_bit_ofs);
      }

      // false = left/right subblocks
      // true = upper/lower subblocks
      inline bool get_flip_bit() const 
      {
         return (m_bytes[3] & 1) != 0;
      }   

      inline void set_flip_bit(bool flip)
      {
         m_bytes[3] &= ~1;
         m_bytes[3] |= static_cast<uint8>(flip);
      }

      inline bool get_diff_bit() const
      {
         return (m_bytes[3] & 2) != 0;
      }

      inline void set_diff_bit(bool diff)
      {
         m_bytes[3] &= ~2;
         m_bytes[3] |= (static_cast<uint>(diff) << 1);
      }

      // Returns intensity modifier table (0-7) used by subblock subblock_id.
      // subblock_id=0 left/top (CW 1), 1=right/bottom (CW 2)
      inline uint get_inten_table(uint subblock_id) const
      {
         CRNLIB_ASSERT(subblock_id < 2);
         const uint ofs = subblock_id ? 2 : 5;
         return (m_bytes[3] >> ofs) & 7;
      }

      // Sets intensity modifier table (0-7) used by subblock subblock_id (0 or 1)
      inline void set_inten_table(uint subblock_id, uint t)
      {
         CRNLIB_ASSERT(subblock_id < 2);
         CRNLIB_ASSERT(t < 8);
         const uint ofs = subblock_id ? 2 : 5;
         m_bytes[3] &= ~(7 << ofs);
         m_bytes[3] |= (t << ofs);
      }

      // Returned selector value ranges from 0-3 and is a direct index into g_etc1_inten_tables.
      inline uint get_selector(uint x, uint y) const
      {
         CRNLIB_ASSERT((x | y) < 4);
                  
         const uint bit_index = x * 4 + y;
         const uint byte_bit_ofs = bit_index & 7;
         const uint8 *p = &m_bytes[7 - (bit_index >> 3)];
         const uint lsb = (p[0] >> byte_bit_ofs) & 1;
         const uint msb = (p[-2] >> byte_bit_ofs) & 1;
         const uint val = lsb | (msb << 1);
         
         return g_etc1_to_selector_index[val];
      }

      // Selector "val" ranges from 0-3 and is a direct index into g_etc1_inten_tables.
      inline void set_selector(uint x, uint y, uint val)
      {
         CRNLIB_ASSERT((x | y | val) < 4);
         const uint bit_index = x * 4 + y;
         
         uint8 *p = &m_bytes[7 - (bit_index >> 3)];
         
         const uint byte_bit_ofs = bit_index & 7;
         const uint mask = 1 << byte_bit_ofs;
         
         const uint etc1_val = g_selector_index_to_etc1[val];

         const uint lsb = etc1_val & 1;
         const uint msb = etc1_val >> 1;

         p[0] &= ~mask;
         p[0] |= (lsb << byte_bit_ofs);

         p[-2] &= ~mask;
         p[-2] |= (msb << byte_bit_ofs);
      }

      inline void set_base4_color(uint idx, uint16 c)
      {
         if (idx)
         {
            set_byte_bits(cETC1AbsColor4R2BitOffset, 4, (c >> 8) & 15);
            set_byte_bits(cETC1AbsColor4G2BitOffset, 4, (c >> 4) & 15);
            set_byte_bits(cETC1AbsColor4B2BitOffset, 4, c & 15);
         }
         else
         {
            set_byte_bits(cETC1AbsColor4R1BitOffset, 4, (c >> 8) & 15);
            set_byte_bits(cETC1AbsColor4G1BitOffset, 4, (c >> 4) & 15);
            set_byte_bits(cETC1AbsColor4B1BitOffset, 4, c & 15);
         }
      }

      inline uint16 get_base4_color(uint idx) const
      {
         uint r, g, b;
         if (idx)
         {
            r = get_byte_bits(cETC1AbsColor4R2BitOffset, 4);
            g = get_byte_bits(cETC1AbsColor4G2BitOffset, 4);
            b = get_byte_bits(cETC1AbsColor4B2BitOffset, 4);
         }
         else
         {
            r = get_byte_bits(cETC1AbsColor4R1BitOffset, 4);
            g = get_byte_bits(cETC1AbsColor4G1BitOffset, 4);
            b = get_byte_bits(cETC1AbsColor4B1BitOffset, 4);
         }
         return static_cast<uint16>(b | (g << 4U) | (r << 8U));
      }

      inline void set_base5_color(uint16 c)
      {
         set_byte_bits(cETC1BaseColor5RBitOffset, 5, (c >> 10) & 31);
         set_byte_bits(cETC1BaseColor5GBitOffset, 5, (c >> 5) & 31);
         set_byte_bits(cETC1BaseColor5BBitOffset, 5, c & 31);
      }

      inline uint16 get_base5_color() const
      {
         const uint r = get_byte_bits(cETC1BaseColor5RBitOffset, 5);
         const uint g = get_byte_bits(cETC1BaseColor5GBitOffset, 5);
         const uint b = get_byte_bits(cETC1BaseColor5BBitOffset, 5);
         return static_cast<uint16>(b | (g << 5U) | (r << 10U));
      }

      void set_delta3_color(uint16 c)
      {
         set_byte_bits(cETC1DeltaColor3RBitOffset, 3, (c >> 6) & 7);
         set_byte_bits(cETC1DeltaColor3GBitOffset, 3, (c >> 3) & 7);
         set_byte_bits(cETC1DeltaColor3BBitOffset, 3, c & 7);
      }

      inline uint16 get_delta3_color() const
      {
         const uint r = get_byte_bits(cETC1DeltaColor3RBitOffset, 3);
         const uint g = get_byte_bits(cETC1DeltaColor3GBitOffset, 3);
         const uint b = get_byte_bits(cETC1DeltaColor3BBitOffset, 3);
         return static_cast<uint16>(b | (g << 3U) | (r << 6U));
      }
      
      // Base color 5
      static uint16 pack_color5(const color_quad_u8& color, bool scaled, uint bias = 127U);
      static uint16 pack_color5(uint r, uint g, uint b, bool scaled, uint bias = 127U);

      static color_quad_u8 unpack_color5(uint16 packed_color5, bool scaled, uint alpha = 255U);
      static void unpack_color5(uint& r, uint& g, uint& b, uint16 packed_color, bool scaled);
      
      static bool unpack_color5(color_quad_u8& result, uint16 packed_color5, uint16 packed_delta3, bool scaled, uint alpha = 255U);
      static bool unpack_color5(uint& r, uint& g, uint& b, uint16 packed_color5, uint16 packed_delta3, bool scaled, uint alpha = 255U);

      // Delta color 3
      // Inputs range from -4 to 3 (cETC1ColorDeltaMin to cETC1ColorDeltaMax)
      static uint16 pack_delta3(const color_quad_i16& color);
      static uint16 pack_delta3(int r, int g, int b);

      // Results range from -4 to 3 (cETC1ColorDeltaMin to cETC1ColorDeltaMax)
      static color_quad_i16 unpack_delta3(uint16 packed_delta3);
      static void unpack_delta3(int& r, int& g, int& b, uint16 packed_delta3);

      // Abs color 4
      static uint16 pack_color4(const color_quad_u8& color, bool scaled, uint bias = 127U);
      static uint16 pack_color4(uint r, uint g, uint b, bool scaled, uint bias = 127U);

      static color_quad_u8 unpack_color4(uint16 packed_color4, bool scaled, uint alpha = 255U);
      static void unpack_color4(uint& r, uint& g, uint& b, uint16 packed_color4, bool scaled);

      // subblock colors
      static void get_diff_subblock_colors(color_quad_u8* pDst, uint16 packed_color5, uint table_idx);
      static bool get_diff_subblock_colors(color_quad_u8* pDst, uint16 packed_color5, uint16 packed_delta3, uint table_idx);
      static void get_abs_subblock_colors(color_quad_u8* pDst, uint16 packed_color4, uint table_idx);

      static inline void unscaled_to_scaled_color(color_quad_u8& dst, const color_quad_u8& src, bool color4)
      {
         if (color4)
         {
            dst.r = src.r | (src.r << 4);
            dst.g = src.g | (src.g << 4);
            dst.b = src.b | (src.b << 4);
         }
         else
         {
            dst.r = (src.r >> 2) | (src.r << 3);
            dst.g = (src.g >> 2) | (src.g << 3);
            dst.b = (src.b >> 2) | (src.b << 3);
         }
         dst.a = src.a;
      }
   };

   CRNLIB_DEFINE_BITWISE_COPYABLE(etc1_block);

   // Returns false if the block is invalid (it will still be unpacked with clamping).
   bool unpack_etc1(const etc1_block& block, color_quad_u8 *pDst, bool preserve_alpha = false);

   enum crn_etc_quality
   { 
      cCRNETCQualityFast,
      cCRNETCQualityMedium,
      cCRNETCQualitySlow,

      cCRNETCQualityTotal,

      cCRNETCQualityForceDWORD = 0xFFFFFFFF
   };

   struct crn_etc1_pack_params
   {
      crn_etc_quality m_quality;
      bool m_perceptual;
      bool m_dithering;
                              
      inline crn_etc1_pack_params() 
      {
         clear();
      }

      void clear()
      {
         m_quality = cCRNETCQualitySlow;
         m_perceptual = true;
         m_dithering = false;
      }
   };

   struct etc1_solution_coordinates
   {
      inline etc1_solution_coordinates() :
         m_unscaled_color(0, 0, 0, 0),
         m_inten_table(0),
         m_color4(false)
      {
      }

      inline etc1_solution_coordinates(uint r, uint g, uint b, uint inten_table, bool color4) : 
         m_unscaled_color(r, g, b, 255),
         m_inten_table(inten_table),
         m_color4(color4)
      {
      }

      inline etc1_solution_coordinates(const color_quad_u8& c, uint inten_table, bool color4) : 
         m_unscaled_color(c),
         m_inten_table(inten_table),
         m_color4(color4)
      {
      }

      inline etc1_solution_coordinates(const etc1_solution_coordinates& other)
      {
         *this = other;
      }

      inline etc1_solution_coordinates& operator= (const etc1_solution_coordinates& rhs)
      {
         m_unscaled_color = rhs.m_unscaled_color;
         m_inten_table = rhs.m_inten_table;
         m_color4 = rhs.m_color4;
         return *this;
      }

      inline void clear()
      {
         m_unscaled_color.clear();
         m_inten_table = 0;
         m_color4 = false;
      }

      inline color_quad_u8 get_scaled_color() const
      {
         int br, bg, bb;
         if (m_color4)
         {
            br = m_unscaled_color.r | (m_unscaled_color.r << 4);
            bg = m_unscaled_color.g | (m_unscaled_color.g << 4);
            bb = m_unscaled_color.b | (m_unscaled_color.b << 4);
         }
         else
         {
            br = (m_unscaled_color.r >> 2) | (m_unscaled_color.r << 3);
            bg = (m_unscaled_color.g >> 2) | (m_unscaled_color.g << 3);
            bb = (m_unscaled_color.b >> 2) | (m_unscaled_color.b << 3);
         }
         return color_quad_u8(br, bg, bb);
      }
      
      inline void get_block_colors(color_quad_u8* pBlock_colors)
      {
         int br, bg, bb;
         if (m_color4)
         {
            br = m_unscaled_color.r | (m_unscaled_color.r << 4);
            bg = m_unscaled_color.g | (m_unscaled_color.g << 4);
            bb = m_unscaled_color.b | (m_unscaled_color.b << 4);
         }
         else
         {
            br = (m_unscaled_color.r >> 2) | (m_unscaled_color.r << 3);
            bg = (m_unscaled_color.g >> 2) | (m_unscaled_color.g << 3);
            bb = (m_unscaled_color.b >> 2) | (m_unscaled_color.b << 3);
         }
         const int* pInten_table = g_etc1_inten_tables[m_inten_table];
         pBlock_colors[0].set(br + pInten_table[0], bg + pInten_table[0], bb + pInten_table[0]);
         pBlock_colors[1].set(br + pInten_table[1], bg + pInten_table[1], bb + pInten_table[1]);
         pBlock_colors[2].set(br + pInten_table[2], bg + pInten_table[2], bb + pInten_table[2]);
         pBlock_colors[3].set(br + pInten_table[3], bg + pInten_table[3], bb + pInten_table[3]);
      }

      color_quad_u8 m_unscaled_color;
      uint m_inten_table;
      bool m_color4;
   };
  
   class etc1_optimizer
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(etc1_optimizer);
   
   public:
      etc1_optimizer()
      {
         clear();
      }
      
      void clear()
      {
         m_pParams = NULL;
         m_pResult = NULL;
         m_pSorted_luma = NULL;
         m_pSorted_luma_indices = NULL;
      }

      struct params : crn_etc1_pack_params
      {
         params()
         {
            clear();
         }

         params(const crn_etc1_pack_params& base_params) : 
           crn_etc1_pack_params(base_params)
         {
            clear_optimizer_params();
         }

         void clear()
         {
            crn_etc1_pack_params::clear();
            clear_optimizer_params();
         }

         void clear_optimizer_params()
         {
            m_num_src_pixels = 0;
            m_pSrc_pixels = 0;

            m_use_color4 = false;
            static const int s_default_scan_delta[] = { 0 };
            m_pScan_deltas = s_default_scan_delta;
            m_scan_delta_size = 1;

            m_base_color5.clear();
            m_constrain_against_base_color5 = false;
         }
                  
         uint m_num_src_pixels;
         const color_quad_u8* m_pSrc_pixels;

         bool m_use_color4;
         const int* m_pScan_deltas;
         uint m_scan_delta_size;
                           
         color_quad_u8 m_base_color5;
         bool m_constrain_against_base_color5;
      };

      struct results
      {
         uint64 m_error;
         color_quad_u8 m_block_color_unscaled;
         uint m_block_inten_table;
         uint m_n;
         uint8* m_pSelectors;
         bool m_block_color4;

         inline results& operator= (const results& rhs)
         {
            m_block_color_unscaled = rhs.m_block_color_unscaled;
            m_block_color4 = rhs.m_block_color4;
            m_block_inten_table = rhs.m_block_inten_table;
            m_error = rhs.m_error;
            CRNLIB_ASSERT(m_n == rhs.m_n);
            memcpy(m_pSelectors, rhs.m_pSelectors, rhs.m_n);
            return *this;
         }
      };

      void init(const params& params, results& result);
      bool compute();
            
   private:      
      struct potential_solution
      {
         potential_solution() : m_coords(), m_error(cUINT64_MAX), m_valid(false)
         {
         }

         etc1_solution_coordinates  m_coords;
         crnlib::vector<uint8>      m_selectors;
         uint64                     m_error;
         bool                       m_valid;

         void clear()
         {
            m_coords.clear();
            m_selectors.resize(0);
            m_error = cUINT64_MAX;
            m_valid = false;
         }

         bool are_selectors_all_equal() const
         {
            if (m_selectors.empty())
               return false;
            const uint s = m_selectors[0];
            for (uint i = 1; i < m_selectors.size(); i++)
               if (m_selectors[i] != s)
                  return false;
            return true;
         }
      };

      const params* m_pParams;
      results* m_pResult;

      int m_limit;

      vec3F m_avg_color;
      int m_br, m_bg, m_bb;
      crnlib::vector<uint16> m_luma;
      crnlib::vector<uint32> m_sorted_luma[2];
      const uint32* m_pSorted_luma_indices;
      uint32* m_pSorted_luma;

      crnlib::vector<uint8> m_selectors;
      crnlib::vector<uint8> m_best_selectors;

      potential_solution m_best_solution;
      potential_solution m_trial_solution;
      crnlib::vector<uint8> m_temp_selectors;
      
      bool evaluate_solution(const etc1_solution_coordinates& coords, potential_solution& trial_solution, potential_solution* pBest_solution);
      bool evaluate_solution_fast(const etc1_solution_coordinates& coords, potential_solution& trial_solution, potential_solution* pBest_solution);
   };

   struct pack_etc1_block_context
   {
      etc1_optimizer m_optimizer;
   };
   
   void pack_etc1_block_init();

   uint64 pack_etc1_block(etc1_block& block, const color_quad_u8* pSrc_pixels, crn_etc1_pack_params& pack_params, pack_etc1_block_context& context);
         
} // namespace crnlib
