// File: crn_texture_file_types.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_texture_file_types.h"
#include "crn_file_utils.h"

namespace crnlib
{
   const char* texture_file_types::get_extension(format fmt)
   {
      CRNLIB_ASSERT(fmt < cNumFileFormats);
      if (fmt >= cNumFileFormats)
         return NULL;

      static const char* extensions[cNumFileFormats] =
      {
         "dds",
         "crn",
         "ktx",

         "tga",
         "png",
         "jpg",
         "jpeg",
         "bmp",
         "gif",
         "tif",
         "tiff",
         "ppm",
         "pgm",
         "psd",
         "jp2",
         
         "<clipboard>",
         "<dragdrop>"
      };
      return extensions[fmt];
   }

   texture_file_types::format texture_file_types::determine_file_format(const char* pFilename)
   {
      dynamic_string ext;
      if (!file_utils::split_path(pFilename, NULL, NULL, NULL, &ext))
         return cFormatInvalid;

      if (ext.is_empty())
         return cFormatInvalid;

      if (ext[0] == '.')
         ext.right(1);

      for (uint i = 0; i < cNumFileFormats; i++)
         if (ext == get_extension(static_cast<format>(i)))
            return static_cast<format>(i);

      return cFormatInvalid;
   }

   bool texture_file_types::supports_mipmaps(format fmt)
   {
      switch (fmt)
      {
         case cFormatCRN:
         case cFormatDDS:
         case cFormatKTX:
            return true;
         default: break;
      }

      return false;
   }

   bool texture_file_types::supports_alpha(format fmt)
   {
      switch (fmt)
      {
         case cFormatJPG:
         case cFormatJPEG:
         case cFormatGIF:
         case cFormatJP2:
            return false;
         default: break;
      }

      return true;
   }

   const char* get_texture_type_desc(texture_type t)
   {
      switch (t)
      {
         case cTextureTypeUnknown:                 return "Unknown";
         case cTextureTypeRegularMap:              return "2D map";
         case cTextureTypeNormalMap:               return "Normal map";
         case cTextureTypeVerticalCrossCubemap:    return "Vertical Cross Cubemap";
         case cTextureTypeCubemap:                 return "Cubemap";
         default: break;
      }

      CRNLIB_ASSERT(false);

      return "?";
   }

} // namespace crnlib
