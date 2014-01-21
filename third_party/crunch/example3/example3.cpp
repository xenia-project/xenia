// File: example3.cpp - Demonstrates how to use crnlib's simple block compression 
// API's to manually pack images to DXTn compressed .DDS files. This example isn't multithreaded
// so it's not going to be fast. 
// Also note that this sample only demonstrates traditional/vanilla 4x4 DXTn block compression (not CRN).

// See Copyright Notice and license at the end of inc/crnlib.h
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <minmax.h>

// CRN transcoder library.
#include "crnlib.h"
// .DDS file format definitions.
#include "dds_defs.h"

// stb_image, for loading/saving image files.
#ifdef _MSC_VER
#pragma warning (disable: 4244) // conversion from 'int' to 'uint8', possible loss of data
#pragma warning (disable: 4100) // unreferenced formal parameter
#pragma warning (disable: 4127) // conditional expression is constant
#endif
#include "stb_image.h"

using namespace crnlib;

const uint cDXTBlockSize = 4;

static int print_usage()
{
   printf("Description: Simple .DDS DXTn block compression using crnlib.\n");
   printf("Copyright (c) 2010-2011 Tenacious Software LLC\n");
   printf("Usage: example3 [source_file] [options]\n");
   printf("\n");
   printf("Note: This simple example is not multithreaded, so it's not going to be\n");
   printf("particularly fast.\n");
   printf("\n");
   printf("Supported source image formats:\n");
   printf("Baseline JPEG, PNG, BMP, TGA, PSD, and HDR\n");
   printf("\nOptions:\n");
   printf("-out filename - Force output filename (always use .DDS extension).\n");
   printf("-nonsrgb - Input is not sRGB: disables gamma filtering, perceptual metrics.\n");
   printf("-pixelformat X - Output DXTn format. Supported formats:\n");
   printf("DXT1, DXT3, DXT5, DXN_XY (ATI 3DC), DXN_YX (ATI 3DC), DXT5A (ATN1N)\n");
   printf("If no output pixel format is specified, this example uses either DXT1 or DXT5.\n");
   printf("-dxtquality X - DXTn quality: superfast, fast, normal, better, uber (default)\n");
   printf("-setalphatoluma - Set alpha channel to luma before compression.\n");
   printf("-converttoluma - Set RGB to luma before compression.\n");
   return EXIT_FAILURE;
}

static int error(const char* pMsg, ...)
{
   va_list args;
   va_start(args, pMsg);
   char buf[512];
   vsprintf_s(buf, sizeof(buf), pMsg, args);
   va_end(args);
   printf("%s", buf);
   return EXIT_FAILURE;
}

int main(int argc, char *argv[])
{
   printf("example3 - Version v%u.%02u Built " __DATE__ ", " __TIME__ "\n", CRNLIB_VERSION / 100, CRNLIB_VERSION % 100);

   if (argc < 2)
      return print_usage();

   // Parse command line options
   const char *pSrc_filename = argv[1];
   char out_filename[FILENAME_MAX] = { '\0' };
   crn_format fmt = cCRNFmtInvalid;
   bool srgb_colorspace = true;
   crn_dxt_quality dxt_quality = cCRNDXTQualityUber; // best quality, but slowest
   bool set_alpha_to_luma = false;
   bool convert_to_luma = false;
      
   for (int i = 2; i < argc; i++)
   {
      if (argv[i][0] == '/') 
         argv[i][0] = '-';

      if (!_stricmp(argv[i], "-out"))
      {
         if (++i >= argc)
            return error("Expected output filename!");

         strcpy_s(out_filename, sizeof(out_filename), argv[i]);
      }
      else if (!_stricmp(argv[i], "-nonsrgb"))
         srgb_colorspace = false;
      else if (!_stricmp(argv[i], "-pixelformat"))
      {
         if (++i >= argc)
            return error("Expected pixel format!");

         uint f;
         for (f = 0; f < cCRNFmtTotal; f++)
         {
            crn_format actual_fmt = crn_get_fundamental_dxt_format(static_cast<crn_format>(f));
            if (!_stricmp(argv[i], crn_get_format_string(actual_fmt)))
            {
               fmt = actual_fmt;
               break;
            }
         }
         if (f == cCRNFmtTotal)
            return error("Unrecognized pixel format: %s\n", argv[i]);
      }
      else if (!_stricmp(argv[i], "-dxtquality"))
      {
         if (++i >= argc)
            return error("Expected DXTn quality!\n");

         uint q;
         for (q = 0; q < cCRNDXTQualityTotal; q++)
         {
            if (!_stricmp(argv[i], crn_get_dxt_quality_string(static_cast<crn_dxt_quality>(q))))
            {
               dxt_quality = static_cast<crn_dxt_quality>(q);
               break;
            }
         }
         if (q == cCRNDXTQualityTotal)
            return error("Unrecognized DXTn quality: %s\n", argv[i]);
      }
      else if (!_stricmp(argv[i], "-setalphatoluma"))
         set_alpha_to_luma = true;
      else if (!_stricmp(argv[i], "-converttoluma"))
         convert_to_luma = true;
      else
         return error("Invalid option: %s\n", argv[i]);
   }

   // Split the source filename into its various components.
   char drive_buf[_MAX_DRIVE], dir_buf[_MAX_DIR], fname_buf[_MAX_FNAME], ext_buf[_MAX_EXT];
   if (_splitpath_s(pSrc_filename, drive_buf, _MAX_DRIVE, dir_buf, _MAX_DIR, fname_buf, _MAX_FNAME, ext_buf, _MAX_EXT))
      return error("Invalid source filename!\n");
   
   // Load the source image into memory.
   printf("Loading source file: %s\n", pSrc_filename);
   int width, height, actual_comps;
   crn_uint32 *pSrc_image = (crn_uint32*)stbi_load(pSrc_filename, &width, &height, &actual_comps, 4);
   if (!pSrc_image)
      return error("Unable to read source file\n");

   if (fmt == cCRNFmtInvalid)
   {
      // Format not specified - automatically choose the DXTn format.
      fmt = (actual_comps > 3) ? cCRNFmtDXT5 : cCRNFmtDXT1;
   }

   if ((fmt == cCRNFmtDXT5A) && (actual_comps <= 3))
      set_alpha_to_luma = true;

   if ((set_alpha_to_luma) || (convert_to_luma))
   {
      for (int i = 0; i < width * height; i++)
      {
         crn_uint32 r = pSrc_image[i] & 0xFF, g = (pSrc_image[i] >> 8) & 0xFF, b = (pSrc_image[i] >> 16) & 0xFF;
         // Compute CCIR 601 luma.
         crn_uint32 y = (19595U * r + 38470U * g + 7471U * b + 32768) >> 16U;
         crn_uint32 a = (pSrc_image[i] >> 24) & 0xFF;
         if (set_alpha_to_luma) a = y;
         if (convert_to_luma) { r = y; g = y; b = y; }
         pSrc_image[i] = r | (g << 8) | (b << 16) | (a << 24);
      }
   }

   printf("Source Dimensions: %ux%u, Actual Components: %u\n", width, height, actual_comps);

   const uint num_blocks_x = (width + cDXTBlockSize - 1) / cDXTBlockSize;
   const uint num_blocks_y = (height + cDXTBlockSize - 1) / cDXTBlockSize;
   const uint bytes_per_block = crn_get_bytes_per_dxt_block(fmt);
   const uint total_compressed_size = num_blocks_x * num_blocks_y * bytes_per_block;

   printf("Block Dimensions: %ux%u, BytesPerBlock: %u, Total Compressed Size: %u\n", num_blocks_x, num_blocks_y, bytes_per_block, total_compressed_size);

   void *pCompressed_data = malloc(total_compressed_size);
   if (!pCompressed_data)
   {
      stbi_image_free(pSrc_image);
      return error("Out of memory!");
   }
   
   crn_comp_params comp_params;
   comp_params.m_format = fmt;
   comp_params.m_dxt_quality = dxt_quality;
   comp_params.set_flag(cCRNCompFlagPerceptual, srgb_colorspace);
   comp_params.set_flag(cCRNCompFlagDXT1AForTransparency, actual_comps > 3);

   crn_block_compressor_context_t pContext = crn_create_block_compressor(comp_params);

   printf("Compressing to %s:     ", crn_get_format_string(fmt));

   int prev_percentage_complete = -1;
   for (crn_uint32 block_y = 0; block_y  < num_blocks_y; block_y++)
   {
      for (crn_uint32 block_x = 0; block_x < num_blocks_x; block_x++)
      {
         crn_uint32 pixels[cDXTBlockSize * cDXTBlockSize];
                  
         // Exact block from image, clamping at the sides of non-divisible by 4 images to avoid artifacts.
         crn_uint32 *pDst_pixels = pixels;
         for (int y = 0; y < cDXTBlockSize; y++)
         {
            const uint actual_y = min(height - 1U, (block_y * cDXTBlockSize) + y);
            for (int x = 0; x < cDXTBlockSize; x++)
            {
               const uint actual_x = min(width - 1U, (block_x * cDXTBlockSize) + x);
               *pDst_pixels++ = pSrc_image[actual_x + actual_y * width];
            }
         }
         
         // Compress the DXTn block.
         crn_compress_block(pContext, pixels, static_cast<crn_uint8 *>(pCompressed_data) + (block_x + block_y * num_blocks_x) * bytes_per_block);
      }
      
      int percentage_complete = ((block_y + 1) * 100 + (num_blocks_y / 2)) / num_blocks_y;
      if (percentage_complete != prev_percentage_complete)
      {
         printf("\b\b\b\b%3u%%", percentage_complete);
         prev_percentage_complete = percentage_complete;
      }
   }
   printf("\n");

   // Free the block compressor.
   crn_free_block_compressor(pContext);
   pContext = NULL;
   
   // Now create the DDS file.
   char dst_filename[FILENAME_MAX];
   sprintf_s(dst_filename, sizeof(dst_filename), "%s%s%s.dds", drive_buf, dir_buf, fname_buf);
   if (out_filename[0]) strcpy(dst_filename, out_filename);

   printf("Writing DDS file: %s\n", dst_filename);

   FILE *pDDS_file = fopen(dst_filename, "wb");
   if (!pDDS_file)
   {
      free(pCompressed_data);
      return error("Failed creating destination file!\n");
   }

   // Write the 4-byte DDS signature (not endian safe, but whatever this is a sample).
   fwrite(&crnlib::cDDSFileSignature, sizeof(crnlib::cDDSFileSignature), 1, pDDS_file);

   // Prepare the DDS header.
   crnlib::DDSURFACEDESC2 dds_desc;
   memset(&dds_desc, 0, sizeof(dds_desc));
   dds_desc.dwSize = sizeof(dds_desc);
   dds_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT;
   dds_desc.dwWidth = width;
   dds_desc.dwHeight = height;
   
   dds_desc.ddpfPixelFormat.dwSize = sizeof(crnlib::DDPIXELFORMAT);
   dds_desc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
   dds_desc.ddpfPixelFormat.dwFourCC = crn_get_format_fourcc(fmt);
   dds_desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;

   // Set pitch/linearsize field (some DDS readers require this field to be non-zero).
   uint bits_per_pixel = crn_get_format_bits_per_texel(fmt);
   dds_desc.lPitch = (((dds_desc.dwWidth + 3) & ~3) * ((dds_desc.dwHeight + 3) & ~3) * bits_per_pixel) >> 3;
   dds_desc.dwFlags |= DDSD_LINEARSIZE;

   // Write the DDS header to the output file.
   fwrite(&dds_desc, sizeof(dds_desc), 1, pDDS_file);
   
   // Write the image's compressed data to the output file.
   fwrite(pCompressed_data, total_compressed_size, 1, pDDS_file);
   free(pCompressed_data);

   stbi_image_free(pSrc_image);

   if (fclose(pDDS_file) == EOF)
   {
      return error("Failed writing to DDS file!\n");
   }
      
   return EXIT_SUCCESS;
}
