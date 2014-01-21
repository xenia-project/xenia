// File: crn_freeimage_image_utils.h
// See Copyright Notice and license at the end of inc/crnlib.h
// Note: This header file requires FreeImage/FreeImagePlus.

#include "crn_image_utils.h"

#include "freeImagePlus.h"

namespace crnlib
{
   namespace freeimage_image_utils
   {
      inline bool load_from_file(image_u8& dest, const wchar_t* pFilename, int fi_flag)
      {
         fipImage src_image;

         if (!src_image.loadU(pFilename, fi_flag))
            return false;
         
         const uint orig_bits_per_pixel = src_image.getBitsPerPixel();

         const FREE_IMAGE_COLOR_TYPE orig_color_type = src_image.getColorType();
         
         if (!src_image.convertTo32Bits())
            return false;

         if (src_image.getBitsPerPixel() != 32)
            return false;

         uint width = src_image.getWidth();
         uint height = src_image.getHeight();

         dest.resize(src_image.getWidth(), src_image.getHeight(), src_image.getWidth());

         color_quad_u8* pDst = dest.get_ptr();

         bool grayscale = true;
         bool has_alpha = false;
         for (uint y = 0; y < height; y++)
         {
            const BYTE* pSrc = src_image.getScanLine((WORD)(height - 1 - y));
            color_quad_u8* pD = pDst;

            for (uint x = width; x; x--)
            {
               color_quad_u8 c;
               c.r = pSrc[FI_RGBA_RED];
               c.g = pSrc[FI_RGBA_GREEN];
               c.b = pSrc[FI_RGBA_BLUE];
               c.a = pSrc[FI_RGBA_ALPHA];
               
               if (!c.is_grayscale())
                  grayscale = false;
               has_alpha |= (c.a < 255);

               pSrc += 4;
               *pD++ = c;
            }

            pDst += width;
         }

         dest.reset_comp_flags();
         
         if (grayscale)
            dest.set_grayscale(true);
         
         dest.set_component_valid(3, has_alpha || (orig_color_type == FIC_RGBALPHA) || (orig_bits_per_pixel == 32));

         return true;
      }
            
      const int cSaveLuma = -1;
      
      inline bool save_to_grayscale_file(const wchar_t* pFilename, const image_u8& src, int component, int fi_flag)
      {
         fipImage dst_image(FIT_BITMAP, (WORD)src.get_width(), (WORD)src.get_height(), 8);

         RGBQUAD* p = dst_image.getPalette();
         for (uint i = 0; i < dst_image.getPaletteSize(); i++)
         {
            p[i].rgbRed = (BYTE)i;
            p[i].rgbGreen = (BYTE)i;
            p[i].rgbBlue = (BYTE)i;
            p[i].rgbReserved = 255;
         }

         for (uint y = 0; y < src.get_height(); y++)
         {
            const color_quad_u8* pSrc = src.get_scanline(y);

            for (uint x = 0; x < src.get_width(); x++)
            {
               BYTE v;
               if (component == cSaveLuma)
                  v = (BYTE)(*pSrc).get_luma();
               else
                  v = (*pSrc)[component];
               dst_image.setPixelIndex(x, src.get_height() - 1 - y, &v);

               pSrc++;
            } 
         }

         if (!dst_image.saveU(pFilename, fi_flag))
            return false; 

         return true;
      }

      inline bool save_to_file(const wchar_t* pFilename, const image_u8& src, int fi_flag, bool ignore_alpha = false)
      {
         const bool save_alpha = src.is_component_valid(3);
         uint bpp = (save_alpha && !ignore_alpha) ? 32 : 24;
         
         if (bpp == 32)
         {
            dynamic_wstring ext(pFilename);
            get_extension(ext);

            if ((ext == L"jpg") || (ext == L"jpeg") || (ext == L"gif") || (ext == L"jp2"))
               bpp = 24;
         }
         
         if ((bpp == 24) && (src.is_grayscale()))
            return save_to_grayscale_file(pFilename, src, cSaveLuma, fi_flag);
                  
         fipImage dst_image(FIT_BITMAP, (WORD)src.get_width(), (WORD)src.get_height(), (WORD)bpp);

         for (uint y = 0; y < src.get_height(); y++)
         {
            for (uint x = 0; x < src.get_width(); x++)
            {
               color_quad_u8 c(src(x, y));

               RGBQUAD quad;
               quad.rgbRed = c.r;
               quad.rgbGreen = c.g;
               quad.rgbBlue = c.b;
               if (bpp == 32)
                  quad.rgbReserved = c.a;
               else
                  quad.rgbReserved = 255;

               dst_image.setPixelColor(x, src.get_height() - 1 - y, &quad);
            } 
         }

         if (!dst_image.saveU(pFilename, fi_flag))
            return false; 

         return true;
      }
   
   } // namespace freeimage_image_utils
   
} // namespace crnlib

