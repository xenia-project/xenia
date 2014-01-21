// File: crn_pixel_format.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_pixel_format.h"
#include "crn_image.h"

namespace crnlib
{
   namespace pixel_format_helpers
   {
      const pixel_format g_all_pixel_formats[] =
      {
         PIXEL_FMT_DXT1,
         PIXEL_FMT_DXT2,
         PIXEL_FMT_DXT3,
         PIXEL_FMT_DXT4,
         PIXEL_FMT_DXT5,
         PIXEL_FMT_3DC,
         PIXEL_FMT_DXN,
         PIXEL_FMT_DXT5A,
         PIXEL_FMT_DXT5_CCxY,
         PIXEL_FMT_DXT5_xGxR,
         PIXEL_FMT_DXT5_xGBR,
         PIXEL_FMT_DXT5_AGBR,
         PIXEL_FMT_DXT1A,
         PIXEL_FMT_ETC1,
         PIXEL_FMT_R8G8B8,
         PIXEL_FMT_L8,
         PIXEL_FMT_A8,
         PIXEL_FMT_A8L8,
         PIXEL_FMT_A8R8G8B8
      };

      uint get_num_formats()
      {
         return sizeof(g_all_pixel_formats) / sizeof(g_all_pixel_formats[0]);
      }

      pixel_format get_pixel_format_by_index(uint index)
      {
         CRNLIB_ASSERT(index < get_num_formats());
         return g_all_pixel_formats[index];
      }

      const char* get_pixel_format_string(pixel_format fmt)
      {
         switch (fmt)
         {
            case PIXEL_FMT_INVALID:   return "INVALID";
            case PIXEL_FMT_DXT1:      return "DXT1";
            case PIXEL_FMT_DXT1A:     return "DXT1A";
            case PIXEL_FMT_DXT2:      return "DXT2";
            case PIXEL_FMT_DXT3:      return "DXT3";
            case PIXEL_FMT_DXT4:      return "DXT4";
            case PIXEL_FMT_DXT5:      return "DXT5";
            case PIXEL_FMT_3DC:       return "3DC";
            case PIXEL_FMT_DXN:       return "DXN";
            case PIXEL_FMT_DXT5A:     return "DXT5A";
            case PIXEL_FMT_DXT5_CCxY: return "DXT5_CCxY";
            case PIXEL_FMT_DXT5_xGxR: return "DXT5_xGxR";
            case PIXEL_FMT_DXT5_xGBR: return "DXT5_xGBR";
            case PIXEL_FMT_DXT5_AGBR: return "DXT5_AGBR";
            case PIXEL_FMT_ETC1:      return "ETC1";
            case PIXEL_FMT_R8G8B8:    return "R8G8B8";
            case PIXEL_FMT_A8R8G8B8:  return "A8R8G8B8";
            case PIXEL_FMT_A8:        return "A8";
            case PIXEL_FMT_L8:        return "L8";
            case PIXEL_FMT_A8L8:      return "A8L8";
            default: break;
         }
         CRNLIB_ASSERT(false);
         return "?";
      }

      const char* get_crn_format_string(crn_format fmt)
      {
         switch (fmt)
         {
            case cCRNFmtDXT1:        return "DXT1";
            case cCRNFmtDXT3:        return "DXT3";
            case cCRNFmtDXT5:        return "DXT5";
            case cCRNFmtDXT5_CCxY:   return "DXT5_CCxY";
            case cCRNFmtDXT5_xGBR:   return "DXT5_xGBR";
            case cCRNFmtDXT5_AGBR:   return "DXT5_AGBR";
            case cCRNFmtDXT5_xGxR:   return "DXT5_xGxR";
            case cCRNFmtDXN_XY:      return "DXN_XY";
            case cCRNFmtDXN_YX:      return "DXN_YX";
            case cCRNFmtDXT5A:       return "DXT5A";
            case cCRNFmtETC1:        return "ETC1";
            default: break;
         }
         CRNLIB_ASSERT(false);
         return "?";
      }

      component_flags get_component_flags(pixel_format fmt)
      {
         // These flags are for *uncooked* pixels, i.e. after after adding Z to DXN maps, or converting YCC maps to RGB, etc.

         uint flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid | cCompFlagGrayscale;
         switch (fmt)
         {
            case PIXEL_FMT_DXT1:
            case PIXEL_FMT_ETC1:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid;
               break;
            }
            case PIXEL_FMT_DXT1A:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid;
               break;
            }
            case PIXEL_FMT_DXT2:
            case PIXEL_FMT_DXT3:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid;
               break;
            }
            case PIXEL_FMT_DXT4:
            case PIXEL_FMT_DXT5:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid;
               break;
            }
            case PIXEL_FMT_DXT5A:
            {
               flags = cCompFlagAValid;
               break;
            }
            case PIXEL_FMT_DXT5_CCxY:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagLumaChroma;
               break;
            }
            case PIXEL_FMT_DXT5_xGBR:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagNormalMap;
               break;
            }
            case PIXEL_FMT_DXT5_AGBR:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid | cCompFlagNormalMap;
               break;
            }
            case PIXEL_FMT_DXT5_xGxR:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagNormalMap;
               break;
            }
            case PIXEL_FMT_3DC:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagNormalMap;
               break;
            }
            case PIXEL_FMT_DXN:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagNormalMap;
               break;
            }
            case PIXEL_FMT_R8G8B8:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid;
               break;
            }
            case PIXEL_FMT_A8R8G8B8:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid;
               break;
            }
            case PIXEL_FMT_A8:
            {
               flags = cCompFlagAValid;
               break;
            }
            case PIXEL_FMT_L8:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagGrayscale;
               break;
            }
            case PIXEL_FMT_A8L8:
            {
               flags = cCompFlagRValid | cCompFlagGValid | cCompFlagBValid | cCompFlagAValid | cCompFlagGrayscale;
               break;
            }
            default:
            {
               CRNLIB_ASSERT(0);
               break;
            }
         }
         return static_cast<component_flags>(flags);
      }

      crn_format convert_pixel_format_to_best_crn_format(pixel_format crn_fmt)
      {
         crn_format fmt = cCRNFmtDXT1;
         switch (crn_fmt)
         {
            case PIXEL_FMT_DXT1:
            case PIXEL_FMT_DXT1A:
               fmt = cCRNFmtDXT1;
               break;
            case PIXEL_FMT_DXT2:
            case PIXEL_FMT_DXT3:
            case PIXEL_FMT_DXT4:
            case PIXEL_FMT_DXT5:
               fmt = cCRNFmtDXT5;
               break;
            case PIXEL_FMT_3DC:
               fmt = cCRNFmtDXN_YX;
               break;
            case PIXEL_FMT_DXN:
               fmt = cCRNFmtDXN_XY;
               break;
            case PIXEL_FMT_DXT5A:
               fmt = cCRNFmtDXT5A;
               break;
            case PIXEL_FMT_R8G8B8:
            case PIXEL_FMT_L8:
               fmt = cCRNFmtDXT1;
               break;
            case PIXEL_FMT_A8R8G8B8:
            case PIXEL_FMT_A8:
            case PIXEL_FMT_A8L8:
               fmt = cCRNFmtDXT5;
               break;
            case PIXEL_FMT_DXT5_CCxY:
               fmt = cCRNFmtDXT5_CCxY;
               break;
            case PIXEL_FMT_DXT5_xGBR:
               fmt = cCRNFmtDXT5_xGBR;
               break;
            case PIXEL_FMT_DXT5_AGBR:
               fmt = cCRNFmtDXT5_AGBR;
               break;
            case PIXEL_FMT_DXT5_xGxR:
               fmt = cCRNFmtDXT5_xGxR;
               break;
            case PIXEL_FMT_ETC1:
               fmt = cCRNFmtETC1;
               break;
            default:
            {
               CRNLIB_ASSERT(false);
               break;
            }
         }
         return fmt;
      }

      pixel_format convert_crn_format_to_pixel_format(crn_format fmt)
      {
         switch (fmt)
         {
            case cCRNFmtDXT1:       return PIXEL_FMT_DXT1;
            case cCRNFmtDXT3:       return PIXEL_FMT_DXT3;
            case cCRNFmtDXT5:       return PIXEL_FMT_DXT5;
            case cCRNFmtDXT5_CCxY:  return PIXEL_FMT_DXT5_CCxY;
            case cCRNFmtDXT5_xGxR:  return PIXEL_FMT_DXT5_xGxR;
            case cCRNFmtDXT5_xGBR:  return PIXEL_FMT_DXT5_xGBR;
            case cCRNFmtDXT5_AGBR:  return PIXEL_FMT_DXT5_AGBR;
            case cCRNFmtDXN_XY:     return PIXEL_FMT_DXN;
            case cCRNFmtDXN_YX:     return PIXEL_FMT_3DC;
            case cCRNFmtDXT5A:      return PIXEL_FMT_DXT5A;
            case cCRNFmtETC1:       return PIXEL_FMT_ETC1;
            default: 
            {
               CRNLIB_ASSERT(false); 
               break;
            }
         }

         return PIXEL_FMT_INVALID;
      }

   } // namespace pixel_format

} // namespace crnlib

