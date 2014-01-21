// File: crnlib.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "../inc/crnlib.h"
#include "crn_comp.h"
#include "crn_dds_comp.h"
#include "crn_dynamic_stream.h"
#include "crn_buffer_stream.h"
#include "crn_ryg_dxt.hpp"
#include "crn_etc.h"

#define CRND_HEADER_FILE_ONLY
#include "../inc/crn_decomp.h"

#include "crn_rg_etc1.h"

namespace crnlib
{
   static void* realloc_func(void* p, size_t size, size_t* pActual_size, bool movable, void* pUser_data)
   {
      pUser_data;
      return crnlib_realloc(p, size, pActual_size, movable);
   }

   static size_t msize_func(void* p, void* pUser_data)
   {
      pUser_data;
      return crnlib_msize(p);
   }

   class crnlib_global_initializer
   {
   public:
      crnlib_global_initializer()
      {
         crn_threading_init();
         
         crnlib_enable_fail_exceptions(true);

         // Redirect crn_decomp.h's memory allocations into crnlib, which may be further redirected by the outside caller.
         crnd::crnd_set_memory_callbacks(realloc_func, msize_func, NULL);

         ryg_dxt::sInitDXT();

         pack_etc1_block_init();

         rg_etc1::pack_etc1_block_init();
      }
   };

   crnlib_global_initializer g_crnlib_initializer;
} // namespace crnlib

using namespace crnlib;

const char* crn_get_format_string(crn_format fmt)
{
   return pixel_format_helpers::get_crn_format_string(fmt);
}

crn_uint32 crn_get_format_fourcc(crn_format fmt)
{
   return crnd::crnd_crn_format_to_fourcc(fmt);
}

crn_uint32 crn_get_format_bits_per_texel(crn_format fmt)
{
   return crnd::crnd_get_crn_format_bits_per_texel(fmt);
}

crn_uint32 crn_get_bytes_per_dxt_block(crn_format fmt)
{
   return crnd::crnd_get_bytes_per_dxt_block(fmt);
}

crn_format crn_get_fundamental_dxt_format(crn_format fmt)
{
   return crnd::crnd_get_fundamental_dxt_format(fmt);
}

const char* crn_get_file_type_ext(crn_file_type file_type)
{
   switch (file_type)
   {
      case cCRNFileTypeDDS: return "dds";
      case cCRNFileTypeCRN: return "crn";
      default: break;
   }
   return "?";
}

const char* crn_get_mip_mode_desc(crn_mip_mode m)
{
   switch (m)
   {
      case cCRNMipModeUseSourceOrGenerateMips:  return "Use source/generate if none";
      case cCRNMipModeUseSourceMips:            return "Only use source MIP maps (if any)";
      case cCRNMipModeGenerateMips:             return "Always generate new MIP maps";
      case cCRNMipModeNoMips:                   return "No MIP maps";
      default: break;
   }
   return "?";
}

const char* crn_get_mip_mode_name(crn_mip_mode m)
{
   switch (m)
   {
      case cCRNMipModeUseSourceOrGenerateMips:  return "UseSourceOrGenerate";
      case cCRNMipModeUseSourceMips:            return "UseSource";
      case cCRNMipModeGenerateMips:             return "Generate";
      case cCRNMipModeNoMips:                   return "None";
      default: break;
   }
   return "?";
}

const char* crn_get_mip_filter_name(crn_mip_filter f)
{
   switch (f)
   {
      case cCRNMipFilterBox:        return "box";
      case cCRNMipFilterTent:       return "tent";
      case cCRNMipFilterLanczos4:   return "lanczos4";
      case cCRNMipFilterMitchell:   return "mitchell";
      case cCRNMipFilterKaiser:     return "kaiser";
      default: break;
   }
   return "?";
}

const char* crn_get_scale_mode_desc(crn_scale_mode sm)
{
   switch (sm)
   {
      case cCRNSMDisabled:       return "disabled";
      case cCRNSMAbsolute:       return "absolute";
      case cCRNSMRelative:       return "relative";
      case cCRNSMLowerPow2:      return "lowerpow2";
      case cCRNSMNearestPow2:    return "nearestpow2";
      case cCRNSMNextPow2:       return "nextpow2";
      default: break;
   }
   return "?";
}

const char* crn_get_dxt_quality_string(crn_dxt_quality q)
{
   switch (q)
   {
      case cCRNDXTQualitySuperFast: return "SuperFast";
      case cCRNDXTQualityFast:      return "Fast";
      case cCRNDXTQualityNormal:    return "Normal";
      case cCRNDXTQualityBetter:    return "Better";
      case cCRNDXTQualityUber:      return "Uber";
      default: break;
   }
   CRNLIB_ASSERT(false);
   return "?";
}


void crn_free_block(void *pBlock)
{
   crnlib_free(pBlock);
}

void *crn_compress(const crn_comp_params &comp_params, crn_uint32 &compressed_size, crn_uint32 *pActual_quality_level, float *pActual_bitrate)
{
   compressed_size = 0;
   if (pActual_quality_level) *pActual_quality_level = 0;
   if (pActual_bitrate) *pActual_bitrate = 0.0f;

   if (!comp_params.check())
      return false;

   crnlib::vector<uint8> crn_file_data;
   if (!create_compressed_texture(comp_params, crn_file_data, pActual_quality_level, pActual_bitrate))
      return NULL;

   compressed_size = crn_file_data.size();
   return crn_file_data.assume_ownership();
}

void *crn_compress(const crn_comp_params &comp_params, const crn_mipmap_params &mip_params, crn_uint32 &compressed_size, crn_uint32 *pActual_quality_level, float *pActual_bitrate)
{
   compressed_size = 0;
   if (pActual_quality_level) *pActual_quality_level = 0;
   if (pActual_bitrate) *pActual_bitrate = 0.0f;

   if ((!comp_params.check()) || (!mip_params.check()))
      return false;

   crnlib::vector<uint8> crn_file_data;
   if (!create_compressed_texture(comp_params, mip_params, crn_file_data, pActual_quality_level, pActual_bitrate))
      return NULL;

   compressed_size = crn_file_data.size();
   return crn_file_data.assume_ownership();
}

void *crn_decompress_crn_to_dds(const void *pCRN_file_data, crn_uint32 &file_size)
{
   mipmapped_texture tex;
   if (!tex.read_crn_from_memory(pCRN_file_data, file_size, "from_memory.crn"))
   {
      file_size = 0;
      return NULL;
   }

   file_size = 0;

   dynamic_stream dds_file_data;
   dds_file_data.reserve(128*1024);
   data_stream_serializer serializer(dds_file_data);
   if (!tex.write_dds(serializer))
      return NULL;
   dds_file_data.reserve(0);

   file_size = static_cast<crn_uint32>(dds_file_data.get_size());
   return dds_file_data.get_buf().assume_ownership();
}

bool crn_decompress_dds_to_images(const void *pDDS_file_data, crn_uint32 dds_file_size, crn_uint32 **ppImages, crn_texture_desc &tex_desc)
{
   memset(&tex_desc, 0, sizeof(tex_desc));

   mipmapped_texture tex;
   buffer_stream in_stream(pDDS_file_data, dds_file_size);
   data_stream_serializer in_serializer(in_stream);
   if (!tex.read_dds(in_serializer))
      return false;

   if (tex.is_packed())
   {
      // TODO: Allow the user to disable uncooking of swizzled DXT5 formats?
      bool uncook = true;

      if (!tex.unpack_from_dxt(uncook))
         return false;
   }

   tex_desc.m_faces = tex.get_num_faces();
   tex_desc.m_width = tex.get_width();
   tex_desc.m_height = tex.get_height();
   tex_desc.m_levels = tex.get_num_levels();
   tex_desc.m_fmt_fourcc = (crn_uint32)tex.get_format();

   for (uint32 f = 0; f < tex.get_num_faces(); f++)
   {
      for (uint32 l = 0; l < tex.get_num_levels(); l++)
      {
         mip_level *pLevel = tex.get_level(f, l);
         image_u8 *pImg = pLevel->get_image();
         ppImages[l + tex.get_num_levels() * f] = static_cast<crn_uint32*>(pImg->get_pixel_buf().assume_ownership());
      }
   }

   return true;
}

void crn_free_all_images(crn_uint32 **ppImages, const crn_texture_desc &desc)
{
   for (uint32 f = 0; f < desc.m_faces; f++)
      for (uint32 l = 0; l < desc.m_levels; l++)
         crn_free_block(ppImages[l + desc.m_levels * f]);
}

// Simple low-level DXTn 4x4 block compressor API.
// Basically just a basic wrapper over the crnlib::dxt_image class.

namespace crnlib
{
   class crn_block_compressor
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(crn_block_compressor);

   public:
      crn_block_compressor()
      {
      }

      bool init(const crn_comp_params &params)
      {
         m_comp_params = params;

         m_pack_params.init(params);

         crn_format basic_crn_fmt = crnd::crnd_get_fundamental_dxt_format(params.m_format);
         pixel_format basic_pixel_fmt = pixel_format_helpers::convert_crn_format_to_pixel_format(basic_crn_fmt);

         if ((params.get_flag(cCRNCompFlagDXT1AForTransparency)) && (basic_pixel_fmt == PIXEL_FMT_DXT1))
            basic_pixel_fmt = PIXEL_FMT_DXT1A;

         if (!m_image.init(pixel_format_helpers::get_dxt_format(basic_pixel_fmt), cDXTBlockSize, cDXTBlockSize, false))
            return false;

         return true;
      }

      void compress_block(const crn_uint32 *pPixels, void *pDst_block)
      {
         if (m_image.is_valid())
         {
            m_image.set_block_pixels(0, 0, reinterpret_cast<const color_quad_u8 *>(pPixels), m_pack_params, m_set_block_pixels_context);
            memcpy(pDst_block, &m_image.get_element(0, 0, 0), m_image.get_bytes_per_block());
         }
      }

   private:
      dxt_image m_image;
      crn_comp_params m_comp_params;
      dxt_image::pack_params m_pack_params;
      dxt_image::set_block_pixels_context m_set_block_pixels_context;
   };
}

crn_block_compressor_context_t crn_create_block_compressor(const crn_comp_params &params)
{
   crn_block_compressor *pComp = crnlib_new<crn_block_compressor>();
   if (!pComp->init(params))
   {
      crnlib_delete(pComp);
      return NULL;
   }
   return pComp;
}

void crn_compress_block(crn_block_compressor_context_t pContext, const crn_uint32 *pPixels, void *pDst_block)
{
   crn_block_compressor *pComp = static_cast<crn_block_compressor *>(pContext);
   pComp->compress_block(pPixels, pDst_block);
}

void crn_free_block_compressor(crn_block_compressor_context_t pContext)
{
   crnlib_delete(static_cast<crn_block_compressor *>(pContext));
}

bool crn_decompress_block(const void *pSrc_block, crn_uint32 *pDst_pixels_u32, crn_format crn_fmt)
{
   color_quad_u8* pDst_pixels = reinterpret_cast<color_quad_u8*>(pDst_pixels_u32);

   switch (crn_get_fundamental_dxt_format(crn_fmt))
   {
      case cCRNFmtETC1:
      {
         const etc1_block& block = *reinterpret_cast<const etc1_block*>(pSrc_block);
         unpack_etc1(block, pDst_pixels, false);
         break;
      }
      case cCRNFmtDXT1:
      {
         const dxt1_block* pDXT1_block = reinterpret_cast<const dxt1_block*>(pSrc_block);

         color_quad_u8 colors[cDXT1SelectorValues];
         pDXT1_block->get_block_colors(colors, static_cast<uint16>(pDXT1_block->get_low_color()), static_cast<uint16>(pDXT1_block->get_high_color()));

         for (uint i = 0; i < cDXTBlockSize * cDXTBlockSize; i++)
         {
            const uint s = pDXT1_block->get_selector(i & 3, i >> 2);

            pDst_pixels[i] = colors[s];
         }

         break;
      }

      case cCRNFmtDXT3:
      {
         const dxt3_block* pDXT3_block = reinterpret_cast<const dxt3_block*>(pSrc_block);
         
         const dxt1_block* pDXT1_block = reinterpret_cast<const dxt1_block*>(pSrc_block) + 1;
         color_quad_u8 colors[cDXT1SelectorValues];
         pDXT1_block->get_block_colors(colors, static_cast<uint16>(pDXT1_block->get_low_color()), static_cast<uint16>(pDXT1_block->get_high_color()));
                  
         for (uint i = 0; i < cDXTBlockSize * cDXTBlockSize; i++)
         {
            const uint s = pDXT1_block->get_selector(i & 3, i >> 2);
            const uint a = pDXT3_block->get_alpha(i & 3, i >> 2, true);

            pDst_pixels[i] = colors[s];
            pDst_pixels[i].a = static_cast<uint8>(a);
         }

         break;
      }

      case cCRNFmtDXT5:
      {
         const dxt5_block* pDXT5_block = reinterpret_cast<const dxt5_block*>(pSrc_block);

         const dxt1_block* pDXT1_block = reinterpret_cast<const dxt1_block*>(pSrc_block) + 1;
         color_quad_u8 colors[cDXT1SelectorValues];
         pDXT1_block->get_block_colors(colors, static_cast<uint16>(pDXT1_block->get_low_color()), static_cast<uint16>(pDXT1_block->get_high_color()));

         uint values[cDXT5SelectorValues];
         dxt5_block::get_block_values(values, pDXT5_block->get_low_alpha(), pDXT5_block->get_high_alpha());
         
         for (uint i = 0; i < cDXTBlockSize * cDXTBlockSize; i++)
         {
            const uint s = pDXT1_block->get_selector(i & 3, i >> 2);
            const uint a = pDXT5_block->get_selector(i & 3, i >> 2);

            pDst_pixels[i] = colors[s];
            pDst_pixels[i].a = static_cast<uint8>(values[a]);
         }
      }

      case cCRNFmtDXN_XY:
      case cCRNFmtDXN_YX:
      {
         const dxt5_block* pDXT5_block0 = reinterpret_cast<const dxt5_block*>(pSrc_block);
         const dxt5_block* pDXT5_block1 = reinterpret_cast<const dxt5_block*>(pSrc_block) + 1;

         uint values0[cDXT5SelectorValues];
         dxt5_block::get_block_values(values0, pDXT5_block0->get_low_alpha(), pDXT5_block0->get_high_alpha());

         uint values1[cDXT5SelectorValues];
         dxt5_block::get_block_values(values1, pDXT5_block1->get_low_alpha(), pDXT5_block1->get_high_alpha());

         for (uint i = 0; i < cDXTBlockSize * cDXTBlockSize; i++)
         {
            const uint s0 = pDXT5_block0->get_selector(i & 3, i >> 2);
            const uint s1 = pDXT5_block1->get_selector(i & 3, i >> 2);

            if (crn_fmt == cCRNFmtDXN_XY)
               pDst_pixels[i].set_noclamp_rgba(values0[s0], values1[s1], 255, 255);
            else
               pDst_pixels[i].set_noclamp_rgba(values1[s1], values0[s0], 255, 255);
         }

         break;
      }
      
      case cCRNFmtDXT5A:
      {
         const dxt5_block* pDXT5_block = reinterpret_cast<const dxt5_block*>(pSrc_block);

         uint values[cDXT5SelectorValues];
         dxt5_block::get_block_values(values, pDXT5_block->get_low_alpha(), pDXT5_block->get_high_alpha());

         for (uint i = 0; i < cDXTBlockSize * cDXTBlockSize; i++)
         {
            const uint s = pDXT5_block->get_selector(i & 3, i >> 2);

            pDst_pixels[i].set_noclamp_rgba(255, 255, 255, values[s]);
         }

         break;
      }
      default:
      {
         return false;
      }
   }

   return true;
}