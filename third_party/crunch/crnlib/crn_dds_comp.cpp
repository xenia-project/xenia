// File: crn_dds_comp.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_dds_comp.h"
#include "crn_dynamic_stream.h"
#include "crn_lzma_codec.h"

namespace crnlib
{
   dds_comp::dds_comp() :
      m_pParams(NULL),
      m_pixel_fmt(PIXEL_FMT_INVALID),
      m_pQDXT_state(NULL)
   {
   }

   dds_comp::~dds_comp()
   {
      crnlib_delete(m_pQDXT_state);
   }

   void dds_comp::clear()
   {
      m_src_tex.clear();
      m_packed_tex.clear();
      m_comp_data.clear();
      m_pParams = NULL;
      m_pixel_fmt = PIXEL_FMT_INVALID;
      m_task_pool.deinit();
      if (m_pQDXT_state)
      {
         crnlib_delete(m_pQDXT_state);
         m_pQDXT_state = NULL;
      }
   }
   
   bool dds_comp::create_dds_tex(mipmapped_texture &dds_tex)
   {
      image_u8 images[cCRNMaxFaces][cCRNMaxLevels];

      bool has_alpha = false;
      for (uint face_index = 0; face_index < m_pParams->m_faces; face_index++)
      {
         for (uint level_index = 0; level_index < m_pParams->m_levels; level_index++)
         {
            const uint width = math::maximum(1U, m_pParams->m_width >> level_index);
            const uint height = math::maximum(1U, m_pParams->m_height >> level_index);

            if (!m_pParams->m_pImages[face_index][level_index])
               return false;

            images[face_index][level_index].alias((color_quad_u8*)m_pParams->m_pImages[face_index][level_index], width, height);
            if (!has_alpha)
               has_alpha = image_utils::has_alpha(images[face_index][level_index]);
         }
      }
      
      for (uint face_index = 0; face_index < m_pParams->m_faces; face_index++)
         for (uint level_index = 0; level_index < m_pParams->m_levels; level_index++)
            images[face_index][level_index].set_component_valid(3, has_alpha);

      image_utils::conversion_type conv_type = image_utils::get_image_conversion_type_from_crn_format((crn_format)m_pParams->m_format);
      if (conv_type != image_utils::cConversion_Invalid)
      {
         for (uint face_index = 0; face_index < m_pParams->m_faces; face_index++)
         {
            for (uint level_index = 0; level_index < m_pParams->m_levels; level_index++)
            {
               image_u8 cooked_image(images[face_index][level_index]);

               image_utils::convert_image(cooked_image, conv_type);

               images[face_index][level_index].swap(cooked_image);
            }
         }
      }

      face_vec faces(m_pParams->m_faces);

      for (uint face_index = 0; face_index < m_pParams->m_faces; face_index++)
      {
         for (uint level_index = 0; level_index < m_pParams->m_levels; level_index++)
         {
            mip_level *pMip = crnlib_new<mip_level>();

            image_u8 *pImage = crnlib_new<image_u8>();
            pImage->swap(images[face_index][level_index]);
            pMip->assign(pImage);

            faces[face_index].push_back(pMip);
         }
      }

      dds_tex.assign(faces);
#ifdef CRNLIB_BUILD_DEBUG
      CRNLIB_ASSERT(dds_tex.check());
#endif

      return true;
   } 

   static bool progress_callback_func(uint percentage_complete, void* pUser_data_ptr)
   {
      const crn_comp_params& params = *(const crn_comp_params*)pUser_data_ptr;
      return params.m_pProgress_func(0, 1, percentage_complete, 100, params.m_pProgress_func_data) != 0;
   }

   static bool progress_callback_func_phase_0(uint percentage_complete, void* pUser_data_ptr)
   {
      const crn_comp_params& params = *(const crn_comp_params*)pUser_data_ptr;
      return params.m_pProgress_func(0, 2, percentage_complete, 100, params.m_pProgress_func_data) != 0;
   }

   static bool progress_callback_func_phase_1(uint percentage_complete, void* pUser_data_ptr)
   {
      const crn_comp_params& params = *(const crn_comp_params*)pUser_data_ptr;
      return params.m_pProgress_func(1, 2, percentage_complete, 100, params.m_pProgress_func_data) != 0;
   }

   bool dds_comp::convert_to_dxt(const crn_comp_params& params)
   {
      if ((params.m_quality_level == cCRNMaxQualityLevel) || (params.m_format == cCRNFmtDXT3))
      {
         m_packed_tex = m_src_tex;
         if (!m_packed_tex.convert(m_pixel_fmt, false, m_pack_params))
            return false;
      }
      else
      {
         const bool hierarchical = (params.m_flags & cCRNCompFlagHierarchical) != 0;
         
         m_q1_params.m_quality_level = params.m_quality_level;
         m_q1_params.m_hierarchical = hierarchical;

         m_q5_params.m_quality_level = params.m_quality_level;
         m_q5_params.m_hierarchical = hierarchical;

         if (!m_pQDXT_state)
         {
            m_pQDXT_state = crnlib_new<mipmapped_texture::qdxt_state>(m_task_pool);
                        
            if (params.m_pProgress_func)
            {
               m_q1_params.m_pProgress_func = progress_callback_func_phase_0;
               m_q1_params.m_pProgress_data = (void*)&params;
               m_q5_params.m_pProgress_func = progress_callback_func_phase_0;
               m_q5_params.m_pProgress_data = (void*)&params;
            }

            if (!m_src_tex.qdxt_pack_init(*m_pQDXT_state, m_packed_tex, m_q1_params, m_q5_params, m_pixel_fmt, false))
               return false;
            
            if (params.m_pProgress_func)
            {
               m_q1_params.m_pProgress_func = progress_callback_func_phase_1;
               m_q5_params.m_pProgress_func = progress_callback_func_phase_1;
            }
         }
         else
         {
            if (params.m_pProgress_func)
            {
               m_q1_params.m_pProgress_func = progress_callback_func;
               m_q1_params.m_pProgress_data = (void*)&params;
               m_q5_params.m_pProgress_func = progress_callback_func;
               m_q5_params.m_pProgress_data = (void*)&params;
            }
         }

         if (!m_src_tex.qdxt_pack(*m_pQDXT_state, m_packed_tex, m_q1_params, m_q5_params))
            return false;
      }
      
      return true;
   }
         
   bool dds_comp::compress_init(const crn_comp_params& params)
   {
      clear();

      m_pParams = &params;

      if ((math::minimum(m_pParams->m_width, m_pParams->m_height) < 1) || (math::maximum(m_pParams->m_width, m_pParams->m_height) > cCRNMaxLevelResolution))
         return false;

      if (math::minimum(m_pParams->m_faces, m_pParams->m_levels) < 1)
         return false;
      
      if (!create_dds_tex(m_src_tex))
         return false;

      m_pack_params.init(*m_pParams);
      if (params.m_pProgress_func)
      {
         m_pack_params.m_pProgress_callback = progress_callback_func;
         m_pack_params.m_pProgress_callback_user_data_ptr = (void*)&params;
      }
      
      m_pixel_fmt = pixel_format_helpers::convert_crn_format_to_pixel_format(static_cast<crn_format>(m_pParams->m_format));
      if (m_pixel_fmt == PIXEL_FMT_INVALID)
         return false;
      if ((m_pixel_fmt == PIXEL_FMT_DXT1) && (m_src_tex.has_alpha()) && (m_pack_params.m_use_both_block_types) && (m_pParams->m_flags & cCRNCompFlagDXT1AForTransparency))
         m_pixel_fmt = PIXEL_FMT_DXT1A;
      
      if (!m_task_pool.init(m_pParams->m_num_helper_threads))
         return false;
      m_pack_params.m_pTask_pool = &m_task_pool;

      const bool hierarchical = (params.m_flags & cCRNCompFlagHierarchical) != 0;
      m_q1_params.init(m_pack_params, params.m_quality_level, hierarchical);
      m_q5_params.init(m_pack_params, params.m_quality_level, hierarchical);
      
      return true;
   }

   bool dds_comp::compress_pass(const crn_comp_params& params, float *pEffective_bitrate)
   {
      if (pEffective_bitrate) *pEffective_bitrate = 0.0f;
      
      if (!m_pParams)
         return false;

      if (!convert_to_dxt(params))
         return false;

      dynamic_stream out_stream;
      out_stream.reserve(512*1024);
      data_stream_serializer serializer(out_stream);

      if (!m_packed_tex.write_dds(serializer))
         return false;
      out_stream.reserve(0);

      m_comp_data.swap(out_stream.get_buf());

      if (pEffective_bitrate)
      {
         lzma_codec lossless_codec;

         crnlib::vector<uint8> cmp_tex_bytes;
         if (lossless_codec.pack(m_comp_data.get_ptr(), m_comp_data.size(), cmp_tex_bytes))
         {
            uint comp_size = cmp_tex_bytes.size();
            if (comp_size)
            {
               *pEffective_bitrate = (comp_size * 8.0f) / m_src_tex.get_total_pixels_in_all_faces_and_mips();
            }
         }
      }

      return true;
   }

   void dds_comp::compress_deinit()
   {
      clear();
   }

} // namespace crnlib
