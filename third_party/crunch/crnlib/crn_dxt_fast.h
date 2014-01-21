// File: crn_dxt_fast.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_color.h"
#include "crn_dxt.h"

namespace crnlib
{
   namespace dxt_fast
   {
      void compress_color_block(uint n, const color_quad_u8* block, uint& low16, uint& high16, uint8* pSelectors, bool refine = false);
      void compress_color_block(dxt1_block* pDXT1_block, const color_quad_u8* pBlock, bool refine = false);
      
      void compress_alpha_block(uint n, const color_quad_u8* block, uint& low8, uint& high8, uint8* pSelectors, uint comp_index);
      void compress_alpha_block(dxt5_block* pDXT5_block, const color_quad_u8* pBlock, uint comp_index);
      
      void find_representative_colors(uint n, const color_quad_u8* pBlock, color_quad_u8& lo, color_quad_u8& hi);
      
   } // namespace dxt_fast

} // namespace crnlib
