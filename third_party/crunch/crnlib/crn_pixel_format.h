// File: crn_pixel_format.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt.h"
#include "../inc/crnlib.h"
#include "../inc/dds_defs.h"

namespace crnlib
{
   namespace pixel_format_helpers
   {
      uint get_num_formats();
      pixel_format get_pixel_format_by_index(uint index);

      const char* get_pixel_format_string(pixel_format fmt);

      const char* get_crn_format_string(crn_format fmt);

      inline bool is_grayscale(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_L8:
            case PIXEL_FMT_A8L8:
               return true;
            default: break;
         }
         return false;
      }

      inline bool is_dxt1(pixel_format fmt)
      {
         return (fmt == PIXEL_FMT_DXT1) || (fmt == PIXEL_FMT_DXT1A);
      }

      // has_alpha() should probably be called "has_opacity()" - it indicates if the format encodes opacity
      // because some swizzled DXT5 formats do not encode opacity.
      inline bool has_alpha(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_DXT1A:
            case PIXEL_FMT_DXT2:
            case PIXEL_FMT_DXT3:
            case PIXEL_FMT_DXT4:
            case PIXEL_FMT_DXT5:
            case PIXEL_FMT_DXT5A:
            case PIXEL_FMT_A8R8G8B8:
            case PIXEL_FMT_A8:
            case PIXEL_FMT_A8L8:
            case PIXEL_FMT_DXT5_AGBR:
               return true;
            default: break;
         }
         return false;
      }

      inline bool is_alpha_only(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_A8:
            case PIXEL_FMT_DXT5A:
               return true;
            default: break;
         }
         return false;
      }

      inline bool is_normal_map(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_3DC:
            case PIXEL_FMT_DXN:
            case PIXEL_FMT_DXT5_xGBR:
            case PIXEL_FMT_DXT5_xGxR:
            case PIXEL_FMT_DXT5_AGBR:
               return true;
            default: break;
         }
         return false;
      }

      inline int is_dxt(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_DXT1:
            case PIXEL_FMT_DXT1A:
            case PIXEL_FMT_DXT2:
            case PIXEL_FMT_DXT3:
            case PIXEL_FMT_DXT4:
            case PIXEL_FMT_DXT5:
            case PIXEL_FMT_3DC:
            case PIXEL_FMT_DXT5A:
            case PIXEL_FMT_DXN:
            case PIXEL_FMT_DXT5_CCxY:
            case PIXEL_FMT_DXT5_xGxR:
            case PIXEL_FMT_DXT5_xGBR:
            case PIXEL_FMT_DXT5_AGBR:
            case PIXEL_FMT_ETC1:
               return true;
            default: break;
         }
         return false;
      }

      inline int get_fundamental_format(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_DXT1A:
               return PIXEL_FMT_DXT1;
            case PIXEL_FMT_DXT5_CCxY:
            case PIXEL_FMT_DXT5_xGxR:
            case PIXEL_FMT_DXT5_xGBR:
            case PIXEL_FMT_DXT5_AGBR:
               return PIXEL_FMT_DXT5;
            default: break;
         }
         return fmt;
      }

      inline dxt_format get_dxt_format(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_DXT1:         return cDXT1;
            case PIXEL_FMT_DXT1A:        return cDXT1A;
            case PIXEL_FMT_DXT2:         return cDXT3;
            case PIXEL_FMT_DXT3:         return cDXT3;
            case PIXEL_FMT_DXT4:         return cDXT5;
            case PIXEL_FMT_DXT5:         return cDXT5;
            case PIXEL_FMT_3DC:          return cDXN_YX;
            case PIXEL_FMT_DXT5A:        return cDXT5A;
            case PIXEL_FMT_DXN:          return cDXN_XY;
            case PIXEL_FMT_DXT5_CCxY:    return cDXT5;
            case PIXEL_FMT_DXT5_xGxR:    return cDXT5;
            case PIXEL_FMT_DXT5_xGBR:    return cDXT5;
            case PIXEL_FMT_DXT5_AGBR:    return cDXT5;
            case PIXEL_FMT_ETC1:         return cETC1;
            default: break;
         }
         return cDXTInvalid;
      }

      inline pixel_format from_dxt_format(dxt_format dxt_fmt)
      {
         switch (dxt_fmt)
         {
            case cDXT1:
               return PIXEL_FMT_DXT1;
            case cDXT1A:
               return PIXEL_FMT_DXT1A;
            case cDXT3:
               return PIXEL_FMT_DXT3;
            case cDXT5:
               return PIXEL_FMT_DXT5;
            case cDXN_XY:
               return PIXEL_FMT_DXN;
            case cDXN_YX:
               return PIXEL_FMT_3DC;
            case cDXT5A:
               return PIXEL_FMT_DXT5A;
            case cETC1:
               return PIXEL_FMT_ETC1;
            default: break;
         }
         CRNLIB_ASSERT(false);
         return PIXEL_FMT_INVALID;
      }

      inline bool is_pixel_format_non_srgb(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_3DC:
            case PIXEL_FMT_DXN:
            case PIXEL_FMT_DXT5A:
            case PIXEL_FMT_DXT5_CCxY:
            case PIXEL_FMT_DXT5_xGxR:
            case PIXEL_FMT_DXT5_xGBR:
            case PIXEL_FMT_DXT5_AGBR:
               return true;
            default: break;
         }
         return false;
      }

      inline bool is_crn_format_non_srgb(crn_format fmt)
      {
         switch (fmt)
         {
            case cCRNFmtDXN_XY:
            case cCRNFmtDXN_YX:
            case cCRNFmtDXT5A:
            case cCRNFmtDXT5_CCxY:
            case cCRNFmtDXT5_xGxR:
            case cCRNFmtDXT5_xGBR:
            case cCRNFmtDXT5_AGBR:
               return true;
            default: break;
         }
         return false;
      }

      inline uint get_bpp(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_DXT1:      return 4;
            case PIXEL_FMT_DXT1A:     return 4;
            case PIXEL_FMT_ETC1:      return 4;
            case PIXEL_FMT_DXT2:      return 8;
            case PIXEL_FMT_DXT3:      return 8;
            case PIXEL_FMT_DXT4:      return 8;
            case PIXEL_FMT_DXT5:      return 8;
            case PIXEL_FMT_3DC:       return 8;
            case PIXEL_FMT_DXT5A:     return 4;
            case PIXEL_FMT_R8G8B8:    return 24;
            case PIXEL_FMT_A8R8G8B8:  return 32;
            case PIXEL_FMT_A8:        return 8;
            case PIXEL_FMT_L8:        return 8;
            case PIXEL_FMT_A8L8:      return 16;
            case PIXEL_FMT_DXN:       return 8;
            case PIXEL_FMT_DXT5_CCxY: return 8;
            case PIXEL_FMT_DXT5_xGxR: return 8;
            case PIXEL_FMT_DXT5_xGBR: return 8;
            case PIXEL_FMT_DXT5_AGBR: return 8;
            default: break;
         }
         CRNLIB_ASSERT(false);
         return 0;
      };

      inline uint get_dxt_bytes_per_block(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_DXT1:      return 8;
            case PIXEL_FMT_DXT1A:     return 8;
            case PIXEL_FMT_DXT5A:     return 8;
            case PIXEL_FMT_ETC1:      return 8;
            case PIXEL_FMT_DXT2:      return 16;
            case PIXEL_FMT_DXT3:      return 16;
            case PIXEL_FMT_DXT4:      return 16;
            case PIXEL_FMT_DXT5:      return 16;
            case PIXEL_FMT_3DC:       return 16;
            case PIXEL_FMT_DXN:       return 16;
            case PIXEL_FMT_DXT5_CCxY: return 16;
            case PIXEL_FMT_DXT5_xGxR: return 16;
            case PIXEL_FMT_DXT5_xGBR: return 16;
            case PIXEL_FMT_DXT5_AGBR: return 16;
            default: break;
         }
         CRNLIB_ASSERT(false);
         return 0;
      }

      enum component_flags
      {
         cCompFlagRValid            = 1,
         cCompFlagGValid            = 2,
         cCompFlagBValid            = 4,
         cCompFlagAValid            = 8,

         cCompFlagGrayscale         = 16,
         cCompFlagNormalMap         = 32,
         cCompFlagLumaChroma        = 64,

         cDefaultCompFlags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid
      };

      component_flags get_component_flags(pixel_format fmt);

      crn_format convert_pixel_format_to_best_crn_format(pixel_format crn_fmt);

      pixel_format convert_crn_format_to_pixel_format(crn_format fmt);

   } // namespace pixel_format_helpers

} // namespace crnlib

