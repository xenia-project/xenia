// File: crn_dxt_image.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt1.h"
#include "crn_dxt5a.h"
#include "crn_etc.h"
#if CRNLIB_SUPPORT_ETC_A1
#include "crn_etc_a1.h"
#endif
#include "crn_image.h"

#define CRNLIB_SUPPORT_ATI_COMPRESS 0

namespace crnlib
{
   class task_pool;

   class dxt_image
   {
   public:
      dxt_image();
      dxt_image(const dxt_image& other);
      dxt_image& operator= (const dxt_image& rhs);
         
      void clear();

      inline bool is_valid() const { return m_blocks_x > 0; }
      
      uint get_width() const { return m_width; }
      uint get_height() const { return m_height; }
      
      uint get_blocks_x() const { return m_blocks_x; }
      uint get_blocks_y() const { return m_blocks_y; }
      uint get_total_blocks() const { return m_blocks_x * m_blocks_y; }
      
      uint get_elements_per_block() const { return m_num_elements_per_block; }
      uint get_bytes_per_block() const { return m_bytes_per_block; }
      
      dxt_format get_format() const { return m_format; }
      
      bool has_color() const { return (m_format == cDXT1) || (m_format == cDXT1A) || (m_format == cDXT3) || (m_format == cDXT5) || (m_format == cETC1); }
      
      // Will be pretty slow if the image is DXT1, as this method scans for alpha blocks/selectors.
      bool has_alpha() const;
            
      enum element_type
      { 
         cUnused = 0,
         
         cColorDXT1,    // DXT1 color block
         
         cAlphaDXT3,    // DXT3 alpha block (only)
         cAlphaDXT5,    // DXT5 alpha block (only)

         cColorETC1,    // ETC1 color block
      };
      
      element_type get_element_type(uint element_index) const { CRNLIB_ASSERT(element_index < m_num_elements_per_block); return m_element_type[element_index]; }
            
      //Returns -1 for RGB, or [0,3]
      int8 get_element_component_index(uint element_index) const { CRNLIB_ASSERT(element_index < m_num_elements_per_block); return m_element_component_index[element_index]; }
      
      struct element
      {
         uint8 m_bytes[8];
         
         uint get_le_word(uint index) const { CRNLIB_ASSERT(index < 4); return m_bytes[index*2] | (m_bytes[index * 2 + 1] << 8); }
         uint get_be_word(uint index) const { CRNLIB_ASSERT(index < 4); return m_bytes[index*2 + 1] | (m_bytes[index * 2] << 8); }
         
         void set_le_word(uint index, uint val) { CRNLIB_ASSERT((index < 4) && (val <= cUINT16_MAX)); m_bytes[index*2] = static_cast<uint8>(val & 0xFF); m_bytes[index * 2 + 1] = static_cast<uint8>((val >> 8) & 0xFF); }
         void set_be_word(uint index, uint val) { CRNLIB_ASSERT((index < 4) && (val <= cUINT16_MAX)); m_bytes[index*2+1] = static_cast<uint8>(val & 0xFF); m_bytes[index * 2] = static_cast<uint8>((val >> 8) & 0xFF); }
         
         void clear()
         {
            memset(this, 0, sizeof(*this));
         }
      };
      
      typedef crnlib::vector<element> element_vec;
                  
      bool init(dxt_format fmt, uint width, uint height, bool clear_elements);
      bool init(dxt_format fmt, uint width, uint height, uint num_elements, element* pElements, bool create_copy);
      
      struct pack_params
      {
         pack_params()
         {
            clear();
         }

         void clear()
         {
            m_quality = cCRNDXTQualityUber;
            m_perceptual = true;
            m_dithering = false;
            m_grayscale_sampling = false;
            m_use_both_block_types = true;
            m_endpoint_caching = true;
            m_compressor = cCRNDXTCompressorCRN;
            m_pProgress_callback = NULL;
            m_pProgress_callback_user_data_ptr = NULL;
            m_dxt1a_alpha_threshold = 128;
            m_num_helper_threads = 0;
            m_progress_start = 0;
            m_progress_range = 100;
            m_use_transparent_indices_for_black = false;
            m_pTask_pool = NULL;
            m_color_weights[0] = 1;
            m_color_weights[1] = 1;
            m_color_weights[2] = 1;
         }

         void init(const crn_comp_params &params)
         {
            m_perceptual = (params.m_flags & cCRNCompFlagPerceptual) != 0;
            m_num_helper_threads = params.m_num_helper_threads;
            m_use_both_block_types = (params.m_flags & cCRNCompFlagUseBothBlockTypes) != 0;
            m_use_transparent_indices_for_black = (params.m_flags & cCRNCompFlagUseTransparentIndicesForBlack) != 0;
            m_dxt1a_alpha_threshold = params.m_dxt1a_alpha_threshold;
            m_quality = params.m_dxt_quality;
            m_endpoint_caching = (params.m_flags & cCRNCompFlagDisableEndpointCaching) == 0;
            m_grayscale_sampling = (params.m_flags & cCRNCompFlagGrayscaleSampling) != 0;
            m_compressor = params.m_dxt_compressor_type;
         }
         
         uint                    m_dxt1a_alpha_threshold;
         
         uint                    m_num_helper_threads;
         
         crn_dxt_quality         m_quality;
         
         crn_dxt_compressor_type m_compressor;
                           
         bool                    m_perceptual;
         bool                    m_dithering;
         bool                    m_grayscale_sampling;
         bool                    m_use_both_block_types;
         bool                    m_endpoint_caching;
         bool                    m_use_transparent_indices_for_black;
                                    
         typedef bool (*progress_callback_func)(uint percentage_complete, void* pUser_data_ptr);
         progress_callback_func  m_pProgress_callback;
         void*                   m_pProgress_callback_user_data_ptr;
         
         uint                    m_progress_start;
         uint                    m_progress_range;

         task_pool               *m_pTask_pool;

         int                     m_color_weights[3];
      };
      
      bool init(dxt_format fmt, const image_u8& img, const pack_params& p = dxt_image::pack_params());
      
      bool unpack(image_u8& img) const;
      
      void endian_swap();
      
      uint get_total_elements() const { return m_elements.size(); }
                  
      const element_vec& get_element_vec() const   { return m_elements; }
            element_vec& get_element_vec()         { return m_elements; }
                  
      const element& get_element(uint block_x, uint block_y, uint element_index) const;
            element& get_element(uint block_x, uint block_y, uint element_index);      
            
      const element* get_element_ptr() const { return m_pElements; } 
            element* get_element_ptr()       { return m_pElements; } 
      
      uint get_size_in_bytes() const { return m_elements.size() * sizeof(element); }
      uint get_row_pitch_in_bytes() const { return m_blocks_x * m_bytes_per_block; }
            
      color_quad_u8 get_pixel(uint x, uint y) const;
      uint get_pixel_alpha(uint x, uint y, uint element_index) const;
      
      void set_pixel(uint x, uint y, const color_quad_u8& c, bool perceptual = true);
      
      // get_block_pixels() only sets those components stored in the image!
      bool get_block_pixels(uint block_x, uint block_y, color_quad_u8* pPixels) const;

      struct set_block_pixels_context
      {
         dxt1_endpoint_optimizer m_dxt1_optimizer;
         dxt5_endpoint_optimizer m_dxt5_optimizer;
         pack_etc1_block_context m_etc1_optimizer;
#if CRNLIB_SUPPORT_ETC_A1
         etc_a1::pack_etc1_block_context m_etc1_a1_optimizer;
#endif
      };
      
      void set_block_pixels(uint block_x, uint block_y, const color_quad_u8* pPixels, const pack_params& p, set_block_pixels_context& context);
      void set_block_pixels(uint block_x, uint block_y, const color_quad_u8* pPixels, const pack_params& p);
      
      void get_block_endpoints(uint block_x, uint block_y, uint element_index, uint& packed_low_endpoint, uint& packed_high_endpoint) const;
      
      // Returns a value representing the component(s) that where actually set, where -1 = RGB.
      // This method does not always set every component!
      int get_block_endpoints(uint block_x, uint block_y, uint element_index, color_quad_u8& low_endpoint, color_quad_u8& high_endpoint, bool scaled = true) const;
      
      // pColors should point to a 16 entry array, to handle DXT3.
      // Returns the number of block colors: 3, 4, 6, 8, or 16.
      uint get_block_colors(uint block_x, uint block_y, uint element_index, color_quad_u8* pColors, uint subblock_index = 0);
            
      uint get_subblock_index(uint x, uint y, uint element_index) const;
      uint get_total_subblocks(uint element_index) const;
      
      uint get_selector(uint x, uint y, uint element_index) const;
      
      void change_dxt1_to_dxt1a();

      bool can_flip(uint axis_index);

      // Returns true if the texture can actually be flipped.
      bool flip_x();
      bool flip_y();
         
   private:
      element_vec       m_elements;
      element*          m_pElements;
      
      uint              m_width;
      uint              m_height;
      
      uint              m_blocks_x;
      uint              m_blocks_y;
      uint              m_total_blocks;
      uint              m_total_elements;
      
      uint              m_num_elements_per_block;   // 1 or 2
      uint              m_bytes_per_block;          // 8 or 16
      
      int8              m_element_component_index[2];            
      element_type      m_element_type[2];
         
      dxt_format        m_format;             // DXT1, 1A, 3, 5, N/3DC, or 5A
      
      bool init_internal(dxt_format fmt, uint width, uint height);
      void init_task(uint64 data, void* pData_ptr);

#if CRNLIB_SUPPORT_ATI_COMPRESS   
      bool init_ati_compress(dxt_format fmt, const image_u8& img, const pack_params& p);
#endif      

      void flip_col(uint x);
      void flip_row(uint y);
   };

} // namespace crnlib
