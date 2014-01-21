// File: crn_texture_comp.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_texture_comp.h"
#include "crn_dds_comp.h"
#include "crn_console.h"
#include "crn_rect.h"

namespace crnlib
{
   static itexture_comp *create_texture_comp(crn_file_type file_type)
   {
      if (file_type == cCRNFileTypeCRN)
         return crnlib_new<crn_comp>();
      else if (file_type == cCRNFileTypeDDS)
         return crnlib_new<dds_comp>();
      else
         return NULL;
   }

   bool create_compressed_texture(const crn_comp_params &params, crnlib::vector<uint8> &comp_data, uint32 *pActual_quality_level, float *pActual_bitrate)
   {
      crn_comp_params local_params(params);

      if (pixel_format_helpers::is_crn_format_non_srgb(local_params.m_format))
      {
         if (local_params.get_flag(cCRNCompFlagPerceptual))
         {
            console::info("Output pixel format is swizzled or not RGB, disabling perceptual color metrics");

            // Destination compressed pixel format is swizzled or not RGB at all, so be sure perceptual colorspace metrics are disabled.
            local_params.set_flag(cCRNCompFlagPerceptual, false);
         }
      }

      if (pActual_quality_level) *pActual_quality_level = 0;
      if (pActual_bitrate) *pActual_bitrate = 0.0f;

      comp_data.resize(0);

      itexture_comp *pTexture_comp = create_texture_comp(local_params.m_file_type);
      if (!pTexture_comp)
         return false;

      if (!pTexture_comp->compress_init(local_params))
      {
         crnlib_delete(pTexture_comp);
         return false;
      }

      if ( (local_params.m_target_bitrate <= 0.0f) ||
           (local_params.m_format == cCRNFmtDXT3) ||
           ((local_params.m_file_type == cCRNFileTypeCRN) && ((local_params.m_flags & cCRNCompFlagManualPaletteSizes) != 0))
          )
      {
         if ( (local_params.m_file_type == cCRNFileTypeCRN) ||
              ((local_params.m_file_type == cCRNFileTypeDDS) && (local_params.m_quality_level < cCRNMaxQualityLevel)) )
         {
            console::info("Compressing using quality level %i", local_params.m_quality_level);
         }
         if (local_params.m_format == cCRNFmtDXT3)
         {
            if (local_params.m_file_type == cCRNFileTypeCRN)
               console::warning("CRN format doesn't support DXT3");
            else if ((local_params.m_file_type == cCRNFileTypeDDS) && (local_params.m_quality_level < cCRNMaxQualityLevel))
               console::warning("Clustered DDS compressor doesn't support DXT3");
         }
         if (!pTexture_comp->compress_pass(local_params, pActual_bitrate))
         {
            crnlib_delete(pTexture_comp);
            return false;
         }

         comp_data.swap(pTexture_comp->get_comp_data());

         if ((pActual_quality_level) && (local_params.m_target_bitrate <= 0.0))
            *pActual_quality_level = local_params.m_quality_level;

         crnlib_delete(pTexture_comp);
         return true;
      }

      // Interpolative search to find closest quality level to target bitrate.
      const int cLowestQuality = 0;
      const int cHighestQuality = cCRNMaxQualityLevel;
      const int cNumQualityLevels = cHighestQuality - cLowestQuality + 1;

      float best_bitrate = 1e+10f;
      int best_quality_level = -1;
      const uint cMaxIterations = 8;

      for ( ; ; )
      {
         int low_quality = cLowestQuality;
         int high_quality = cHighestQuality;

         float cached_bitrates[cNumQualityLevels];
         for (int i = 0; i < cNumQualityLevels; i++)
            cached_bitrates[i] = -1.0f;

         float highest_bitrate = 0.0f;

         uint iter_count = 0;
         bool force_binary_search = false;

         while (low_quality <= high_quality)
         {
            if (params.m_flags & cCRNCompFlagDebugging)
            {
               console::debug("Quality level bracket: [%u, %u]", low_quality, high_quality);
            }

            int trial_quality = (low_quality + high_quality) / 2;

            if ((iter_count) && (!force_binary_search))
            {
               int bracket_low = trial_quality;
               while ((cached_bitrates[bracket_low] < 0) && (bracket_low > cLowestQuality))
                  bracket_low--;

               if (cached_bitrates[bracket_low] < 0)
                  trial_quality = static_cast<int>(math::lerp<float>((float)low_quality, (float)high_quality, .33f));
               else
               {
                  int bracket_high = trial_quality + 1;
                  if (bracket_high <= cHighestQuality)
                  {
                     while ((cached_bitrates[bracket_high] < 0) && (bracket_high < cHighestQuality))
                        bracket_high++;

                     if (cached_bitrates[bracket_high] >= 0)
                     {
                        float bracket_low_bitrate = cached_bitrates[bracket_low];
                        float bracket_high_bitrate = cached_bitrates[bracket_high];

                        if ((bracket_low_bitrate < bracket_high_bitrate) &&
                           (bracket_low_bitrate < local_params.m_target_bitrate) &&
                           (bracket_high_bitrate >= local_params.m_target_bitrate))
                        {
                           int quality = low_quality + static_cast<int>( ((local_params.m_target_bitrate - bracket_low_bitrate) * (high_quality - low_quality)) / (bracket_high_bitrate - bracket_low_bitrate) );

                           if ((quality >= low_quality) && (quality <= high_quality))
                           {
                              trial_quality = quality;
                           }
                        }
                     }
                  }
               }
            }

            console::info("Compressing to quality level %u", trial_quality);

            float bitrate = 0.0f;

            local_params.m_quality_level = trial_quality;

            if (!pTexture_comp->compress_pass(local_params, &bitrate))
            {
               crnlib_delete(pTexture_comp);
               return false;
            }

            cached_bitrates[trial_quality] = bitrate;

            highest_bitrate = math::maximum(highest_bitrate, bitrate);

            console::info("\nTried quality level %u, bpp: %3.3f", trial_quality, bitrate);

            if ( (best_quality_level < 0) ||
                 ((bitrate <= local_params.m_target_bitrate) && (best_bitrate > local_params.m_target_bitrate)) ||
                 (((bitrate <= local_params.m_target_bitrate) || (best_bitrate > local_params.m_target_bitrate)) && (fabs(bitrate - local_params.m_target_bitrate) < fabs(best_bitrate - local_params.m_target_bitrate)))
                )
            {
               best_bitrate = bitrate;
               comp_data.swap(pTexture_comp->get_comp_data());
               best_quality_level = trial_quality;
               if (params.m_flags & cCRNCompFlagDebugging)
               {
                  console::debug("Choose new best quality level");
               }

               if ((best_bitrate <= local_params.m_target_bitrate) && (fabs(best_bitrate - local_params.m_target_bitrate) < .005f))
                  break;
            }

            if (bitrate > local_params.m_target_bitrate)
               high_quality = trial_quality - 1;
            else
               low_quality = trial_quality + 1;

            iter_count++;
            if (iter_count > cMaxIterations)
            {
               force_binary_search = true;
            }
         }

         if (((local_params.m_flags & cCRNCompFlagHierarchical) != 0) &&
            (highest_bitrate < local_params.m_target_bitrate) &&
            (fabs(best_bitrate - local_params.m_target_bitrate) >= .005f))
         {
            console::info("Unable to achieve desired bitrate - disabling adaptive block sizes and retrying search.");

            local_params.m_flags &= ~cCRNCompFlagHierarchical;

            crnlib_delete(pTexture_comp);
            pTexture_comp = create_texture_comp(local_params.m_file_type);

            if (!pTexture_comp->compress_init(local_params))
            {
               crnlib_delete(pTexture_comp);
               return false;
            }
         }
         else
            break;
      }

      crnlib_delete(pTexture_comp);
      pTexture_comp = NULL;

      if (best_quality_level < 0)
         return false;

      if (pActual_quality_level) *pActual_quality_level = best_quality_level;
      if (pActual_bitrate) *pActual_bitrate = best_bitrate;

      console::printf("Selected quality level %u bpp: %f", best_quality_level, best_bitrate);

      return true;
   }

   static bool create_dds_tex(const crn_comp_params &params, mipmapped_texture &dds_tex)
   {
      image_u8 images[cCRNMaxFaces][cCRNMaxLevels];

      bool has_alpha = false;
      for (uint face_index = 0; face_index < params.m_faces; face_index++)
      {
         for (uint level_index = 0; level_index < params.m_levels; level_index++)
         {
            const uint width = math::maximum(1U, params.m_width >> level_index);
            const uint height = math::maximum(1U, params.m_height >> level_index);

            if (!params.m_pImages[face_index][level_index])
               return false;

            images[face_index][level_index].alias((color_quad_u8*)params.m_pImages[face_index][level_index], width, height);
            if (!has_alpha)
               has_alpha = image_utils::has_alpha(images[face_index][level_index]);
         }
      }

      for (uint face_index = 0; face_index < params.m_faces; face_index++)
         for (uint level_index = 0; level_index < params.m_levels; level_index++)
            images[face_index][level_index].set_component_valid(3, has_alpha);

      face_vec faces(params.m_faces);

      for (uint face_index = 0; face_index < params.m_faces; face_index++)
      {
         for (uint level_index = 0; level_index < params.m_levels; level_index++)
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

   bool create_texture_mipmaps(mipmapped_texture &work_tex, const crn_comp_params &params, const crn_mipmap_params &mipmap_params, bool generate_mipmaps)
   {
      crn_comp_params new_params(params);

      bool generate_new_mips = false;

      switch (mipmap_params.m_mode)
      {
         case cCRNMipModeUseSourceOrGenerateMips:
         {
            if (work_tex.get_num_levels() == 1)
               generate_new_mips = true;
            break;
         }
         case cCRNMipModeUseSourceMips:
         {
            break;
         }
         case cCRNMipModeGenerateMips:
         {
            generate_new_mips = true;
            break;
         }
         case cCRNMipModeNoMips:
         {
            work_tex.discard_mipmaps();
            break;
         }
         default:
         {
            CRNLIB_ASSERT(0);
            break;
         }
      }

      rect window_rect(mipmap_params.m_window_left, mipmap_params.m_window_top, mipmap_params.m_window_right, mipmap_params.m_window_bottom);

      if (!window_rect.is_empty())
      {
         if (work_tex.get_num_faces() > 1)
         {
            console::warning("Can't crop cubemap textures");
         }
         else
         {
            console::info("Cropping input texture from window (%ux%u)-(%ux%u)", window_rect.get_left(), window_rect.get_top(), window_rect.get_right(), window_rect.get_bottom());

            if (!work_tex.crop(window_rect.get_left(), window_rect.get_top(), window_rect.get_width(), window_rect.get_height()))
               console::warning("Failed cropping window rect");
         }
      }

      int new_width = work_tex.get_width();
      int new_height = work_tex.get_height();

      if ((mipmap_params.m_clamp_width) && (mipmap_params.m_clamp_height))
      {
         if ((new_width > (int)mipmap_params.m_clamp_width) || (new_height > (int)mipmap_params.m_clamp_height))
         {
            if (!mipmap_params.m_clamp_scale)
            {
               if (work_tex.get_num_faces() > 1)
               {
                  console::warning("Can't crop cubemap textures");
               }
               else
               {
                  new_width = math::minimum<uint>(mipmap_params.m_clamp_width, new_width);
                  new_height = math::minimum<uint>(mipmap_params.m_clamp_height, new_height);
                  console::info("Clamping input texture to %ux%u", new_width, new_height);
                  work_tex.crop(0, 0, new_width, new_height);
               }
            }
         }
      }

      if (mipmap_params.m_scale_mode != cCRNSMDisabled)
      {
         bool is_pow2 = math::is_power_of_2((uint32)new_width) && math::is_power_of_2((uint32)new_height);

         switch (mipmap_params.m_scale_mode)
         {
            case cCRNSMAbsolute:
            {
               new_width = (uint)mipmap_params.m_scale_x;
               new_height = (uint)mipmap_params.m_scale_y;
               break;
            }
            case cCRNSMRelative:
            {
               new_width = (uint)(mipmap_params.m_scale_x * new_width + .5f);
               new_height = (uint)(mipmap_params.m_scale_y * new_height + .5f);
               break;
            }
            case cCRNSMLowerPow2:
            {
               if (!is_pow2)
                  math::compute_lower_pow2_dim(new_width, new_height);
               break;
            }
            case cCRNSMNearestPow2:
            {
               if (!is_pow2)
               {
                  int lwidth = new_width;
                  int lheight = new_height;
                  math::compute_lower_pow2_dim(lwidth, lheight);

                  int uwidth = new_width;
                  int uheight = new_height;
                  math::compute_upper_pow2_dim(uwidth, uheight);

                  if (labs(new_width - lwidth) < labs(new_width - uwidth))
                     new_width = lwidth;
                  else
                     new_width = uwidth;

                  if (labs(new_height - lheight) < labs(new_height - uheight))
                     new_height = lheight;
                  else
                     new_height = uheight;
               }
               break;
            }
            case cCRNSMNextPow2:
            {
               if (!is_pow2)
                  math::compute_upper_pow2_dim(new_width, new_height);
               break;
            }
            default: break;
         }
      }

      if ((mipmap_params.m_clamp_width) && (mipmap_params.m_clamp_height))
      {
         if ((new_width > (int)mipmap_params.m_clamp_width) || (new_height > (int)mipmap_params.m_clamp_height))
         {
            if (mipmap_params.m_clamp_scale)
            {
               new_width = math::minimum<uint>(mipmap_params.m_clamp_width, new_width);
               new_height = math::minimum<uint>(mipmap_params.m_clamp_height, new_height);
            }
         }
      }

      new_width = math::clamp<int>(new_width, 1, cCRNMaxLevelResolution);
      new_height = math::clamp<int>(new_height, 1, cCRNMaxLevelResolution);

      if ((new_width != (int)work_tex.get_width()) || (new_height != (int)work_tex.get_height()))
      {
         console::info("Resampling input texture to %ux%u", new_width, new_height);

         const char* pFilter = crn_get_mip_filter_name(mipmap_params.m_filter);

         bool srgb = mipmap_params.m_gamma_filtering != 0;

         mipmapped_texture::resample_params res_params;
         res_params.m_pFilter = pFilter;
         res_params.m_wrapping = mipmap_params.m_tiled != 0;
         if (work_tex.get_num_faces())
            res_params.m_wrapping = false;
         res_params.m_renormalize = mipmap_params.m_renormalize != 0;
         res_params.m_filter_scale = 1.0f;
         res_params.m_gamma = mipmap_params.m_gamma;
         res_params.m_srgb = srgb;
         res_params.m_multithreaded = (params.m_num_helper_threads > 0);

         if (!work_tex.resize(new_width, new_height, res_params))
         {
            console::error("Failed resizing texture!");
            return false;
         }
      }

      if ((generate_new_mips) && (generate_mipmaps))
      {
         bool srgb = mipmap_params.m_gamma_filtering != 0;

         const char* pFilter = crn_get_mip_filter_name(mipmap_params.m_filter);

         mipmapped_texture::generate_mipmap_params gen_params;
         gen_params.m_pFilter = pFilter;
         gen_params.m_wrapping = mipmap_params.m_tiled != 0;
         gen_params.m_renormalize = mipmap_params.m_renormalize != 0;
         gen_params.m_filter_scale = mipmap_params.m_blurriness;
         gen_params.m_gamma = mipmap_params.m_gamma;
         gen_params.m_srgb = srgb;
         gen_params.m_multithreaded = params.m_num_helper_threads > 0;
         gen_params.m_max_mips = mipmap_params.m_max_levels;
         gen_params.m_min_mip_size = mipmap_params.m_min_mip_size;

         console::info("Generating mipmaps using filter \"%s\"", pFilter);

         timer tm;
         tm.start();
         if (!work_tex.generate_mipmaps(gen_params, true))
         {
            console::error("Failed generating mipmaps!");
            return false;
         }
         double t = tm.get_elapsed_secs();

         console::info("Generated %u mipmap levels in %3.3fs", work_tex.get_num_levels() - 1, t);
      }

      return true;
   }

   bool create_compressed_texture(const crn_comp_params &params, const crn_mipmap_params &mipmap_params, crnlib::vector<uint8> &comp_data, uint32 *pActual_quality_level, float *pActual_bitrate)
   {
      comp_data.resize(0);
      if (pActual_bitrate) *pActual_bitrate = 0.0f;
      if (pActual_quality_level) *pActual_quality_level = 0;

      mipmapped_texture work_tex;
      if (!create_dds_tex(params, work_tex))
      {
         console::error("Failed creating DDS texture from crn_comp_params!");
         return false;
      }

      if (!create_texture_mipmaps(work_tex, params, mipmap_params, true))
         return false;

      crn_comp_params new_params(params);
      new_params.m_levels = work_tex.get_num_levels();
      memset(new_params.m_pImages, 0, sizeof(new_params.m_pImages));

      for (uint f = 0; f < work_tex.get_num_faces(); f++)
         for (uint l = 0; l < work_tex.get_num_levels(); l++)
            new_params.m_pImages[f][l] = (uint32*)work_tex.get_level(f, l)->get_image()->get_ptr();

      return create_compressed_texture(new_params, comp_data, pActual_quality_level, pActual_bitrate);
   }

} // namespace crnlib

