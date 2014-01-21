// File: crn_texture_conversion.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt_image.h"
#include "crn_mipmapped_texture.h"
#include "crn_rect.h"
#include "crn_lzma_codec.h"

namespace crnlib
{
   namespace texture_conversion
   {
      class convert_stats
      {
      public:
         convert_stats();

         bool init(
            const char* pSrc_filename,
            const char* pDst_filename,
            mipmapped_texture& src_tex,
            texture_file_types::format dst_file_type,
            bool lzma_stats);

         bool print(bool psnr_metrics, bool mip_stats, bool grayscale_sampling, const char *pCSVStatsFile = NULL) const;

         void clear();

         dynamic_string             m_src_filename;
         dynamic_string             m_dst_filename;
         texture_file_types::format m_dst_file_type;

         mipmapped_texture*               m_pInput_tex;
         mipmapped_texture                m_output_tex;

         uint64                     m_input_file_size;
         uint                       m_total_input_pixels;

         uint64                     m_output_file_size;
         uint                       m_total_output_pixels;

         uint64                     m_output_comp_file_size;
      };

      class convert_params
      {
      public:
         convert_params() :
            m_pInput_texture(NULL),
            m_texture_type(cTextureTypeUnknown),
            m_dst_file_type(texture_file_types::cFormatInvalid),
            m_dst_format(PIXEL_FMT_INVALID),
            m_pProgress_func(NULL),
            m_pProgress_user_data(NULL),
            m_pIntermediate_texture(NULL),
            m_y_flip(false),
            m_unflip(false),
            m_always_use_source_pixel_format(false),
            m_write_mipmaps_to_multiple_files(false),
            m_quick(false),
            m_debugging(false),
            m_param_debugging(false),
            m_no_stats(false),
            m_lzma_stats(false),
            m_status(false),
            m_canceled(false)
         {
         }

         ~convert_params()
         {
            crnlib_delete(m_pIntermediate_texture);
         }

         void print();

         // Input parameters
         mipmapped_texture*                  m_pInput_texture;

         texture_type                  m_texture_type;

         dynamic_string                m_dst_filename;
         texture_file_types::format    m_dst_file_type;
         pixel_format                  m_dst_format;

         crn_comp_params               m_comp_params;
         crn_mipmap_params             m_mipmap_params;

         typedef bool (*progress_callback_func_ptr)(uint percentage_complete, void* pUser_data_ptr);
         progress_callback_func_ptr    m_pProgress_func;
         void*                         m_pProgress_user_data;

         // Return parameters
         mipmapped_texture*                  m_pIntermediate_texture;
         mutable dynamic_string        m_error_message;

         bool                          m_y_flip;
         bool                          m_unflip;
         bool                          m_always_use_source_pixel_format;
         bool                          m_write_mipmaps_to_multiple_files;
         bool                          m_quick;
         bool                          m_debugging;
         bool                          m_param_debugging;
         bool                          m_no_stats;

         bool                          m_lzma_stats;
         mutable bool                  m_status;
         mutable bool                  m_canceled;
      };

      bool process(convert_params& params, convert_stats& stats);

   } // namespace texture_conversion

} // namespace crnlib
