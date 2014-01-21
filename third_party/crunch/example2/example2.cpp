// File: example2.cpp - This example uses the crn_decomp.h stand-alone header file library 
// to transcode .CRN files directly to .DDS, with no intermediate recompression step to DXTn.
// This tool does NOT depend on the crnlib library at all. It only needs the low-level 
// decompression/transcoding functionality defined in inc/crn_decomp.h.
// This is the basic functionality a game engine would need to employ at runtime to utilize 
// .CRN textures (excluding writing the output DDS file - instead you would provide the DXTn
// bits directly to OpenGL/D3D).
// See Copyright Notice and license at the end of inc/crnlib.h
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>

// CRN transcoder library.
#include "crn_decomp.h"
// .DDS file format definitions.
#include "dds_defs.h"

// A simple high-precision, platform independent timer class.
#include "timer.h"

using namespace crnlib;

static int print_usage()
{
   printf("Description: Transcodes .CRN to .DDS files using crn_decomp.h.\n");
   printf("Copyright (c) 2010-2011 Tenacious Software LLC\n");
   printf("Usage: example2 [source_file] [options]\n");
   printf("\nOptions:\n");
   printf("-out filename - Force output filename.\n");
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

// Loads an entire file into an allocated memory block.
static crn_uint8 *read_file_into_buffer(const char *pFilename, crn_uint32 &size)
{
   size = 0;

   FILE* pFile = NULL;
   fopen_s(&pFile, pFilename, "rb");
   if (!pFile)
      return NULL;
   
   fseek(pFile, 0, SEEK_END);
   size = ftell(pFile);
   fseek(pFile, 0, SEEK_SET);
   
   crn_uint8 *pSrc_file_data = static_cast<crn_uint8*>(malloc(std::max(1U, size)));
   if ((!pSrc_file_data) || (fread(pSrc_file_data, size, 1, pFile) != 1))
   {
      fclose(pFile);
      free(pSrc_file_data);
      size = 0;
      return NULL;
   }

   fclose(pFile);
   return pSrc_file_data;
}

int main(int argc, char *argv[])
{
   printf("example2 - Version v%u.%02u Built " __DATE__ ", " __TIME__ "\n", CRNLIB_VERSION / 100, CRNLIB_VERSION % 100);

   if (argc < 2)
      return print_usage();

   // Parse command line options
   const char *pSrc_filename = argv[1];
   char out_filename[FILENAME_MAX] = { '\0' };
   
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
      else
         return error("Invalid option: %s\n", argv[i]);
   }

   // Split the source filename into its various components.
   char drive_buf[_MAX_DRIVE], dir_buf[_MAX_DIR], fname_buf[_MAX_FNAME], ext_buf[_MAX_EXT];
   if (_splitpath_s(pSrc_filename, drive_buf, _MAX_DRIVE, dir_buf, _MAX_DIR, fname_buf, _MAX_FNAME, ext_buf, _MAX_EXT))
      return error("Invalid source filename!\n");
   
   // Load the source file into memory.
   printf("Loading source file: %s\n", pSrc_filename);
   crn_uint32 src_file_size;
   crn_uint8 *pSrc_file_data = read_file_into_buffer(pSrc_filename, src_file_size);
   if (!pSrc_file_data)
      return error("Unable to read source file\n");
   
   // Decompress/transcode CRN to DDS. 
   // DDS files are organized in face-major order, like this:
   //  Face0: Mip0, Mip1, Mip2, etc.
   //  Face1: Mip0, Mip1, Mip2, etc.
   //  etc.
   // While CRN files are organized in mip-major order, like this:
   //  Mip0: Face0, Face1, Face2, Face3, Face4, Face5
   //  Mip1: Face0, Face1, Face2, Face3, Face4, Face5
   //  etc.
   printf("Transcoding CRN to DDS\n");

   crnd::crn_texture_info tex_info;
   if (!crnd::crnd_get_texture_info(pSrc_file_data, src_file_size, &tex_info))
   {
      free(pSrc_file_data);
      return error("crnd_get_texture_info() failed!\n");
   }
   
   timer tm;
   
   tm.start();
   crnd::crnd_unpack_context pContext = crnd::crnd_unpack_begin(pSrc_file_data, src_file_size);
   double total_unpack_begin_time = tm.get_elapsed_ms();

   if (!pContext)
   {
      free(pSrc_file_data);
      return error("crnd_unpack_begin() failed!\n");
   }

   // Now create the DDS file.
   char dst_filename[FILENAME_MAX];
   sprintf_s(dst_filename, sizeof(dst_filename), "%s%s%s.dds", drive_buf, dir_buf, fname_buf);
   if (out_filename[0]) strcpy(dst_filename, out_filename);

   printf("Writing DDS file: %s\n", dst_filename);

   FILE *pDDS_file = fopen(dst_filename, "wb");
   if (!pDDS_file)
   {
      crnd::crnd_unpack_end(pContext);
      free(pSrc_file_data);
      return error("Failed creating destination file!\n");
   }

   // Write the 4-byte DDS signature (not endian safe, but whatever this is a sample).
   fwrite(&crnlib::cDDSFileSignature, sizeof(crnlib::cDDSFileSignature), 1, pDDS_file);

   // Prepare the DDS header.
   crnlib::DDSURFACEDESC2 dds_desc;
   memset(&dds_desc, 0, sizeof(dds_desc));
   dds_desc.dwSize = sizeof(dds_desc);
   dds_desc.dwFlags = DDSD_CAPS | DDSD_HEIGHT | DDSD_WIDTH | DDSD_PIXELFORMAT | ((tex_info.m_levels > 1) ? DDSD_MIPMAPCOUNT : 0);
   dds_desc.dwWidth = tex_info.m_width;
   dds_desc.dwHeight = tex_info.m_height;
   dds_desc.dwMipMapCount = (tex_info.m_levels > 1) ? tex_info.m_levels : 0;

   dds_desc.ddpfPixelFormat.dwSize = sizeof(crnlib::DDPIXELFORMAT);
   dds_desc.ddpfPixelFormat.dwFlags = DDPF_FOURCC;
   crn_format fundamental_fmt = crnd::crnd_get_fundamental_dxt_format(tex_info.m_format);
   dds_desc.ddpfPixelFormat.dwFourCC = crnd::crnd_crn_format_to_fourcc(fundamental_fmt);
   if (fundamental_fmt != tex_info.m_format)
   {
      // It's a funky swizzled DXTn format - write its FOURCC to dwRGBBitCount.
      dds_desc.ddpfPixelFormat.dwRGBBitCount = crnd::crnd_crn_format_to_fourcc(tex_info.m_format);
   }

   dds_desc.ddsCaps.dwCaps = DDSCAPS_TEXTURE;
   if (tex_info.m_levels > 1)
   {     
      dds_desc.ddsCaps.dwCaps |= (DDSCAPS_COMPLEX | DDSCAPS_MIPMAP);
   }

   if (tex_info.m_faces == 6)
   {
      dds_desc.ddsCaps.dwCaps2 = DDSCAPS2_CUBEMAP | 
         DDSCAPS2_CUBEMAP_POSITIVEX | DDSCAPS2_CUBEMAP_NEGATIVEX | DDSCAPS2_CUBEMAP_POSITIVEY | 
         DDSCAPS2_CUBEMAP_NEGATIVEY | DDSCAPS2_CUBEMAP_POSITIVEZ | DDSCAPS2_CUBEMAP_NEGATIVEZ;
   }

   // Set pitch/linearsize field (some DDS readers require this field to be non-zero).
   int bits_per_pixel = crnd::crnd_get_crn_format_bits_per_texel(tex_info.m_format);
   dds_desc.lPitch = (((dds_desc.dwWidth + 3) & ~3) * ((dds_desc.dwHeight + 3) & ~3) * bits_per_pixel) >> 3;
   dds_desc.dwFlags |= DDSD_LINEARSIZE;

   // Write the DDS header to the output file.
   fwrite(&dds_desc, sizeof(dds_desc), 1, pDDS_file);
   
   // Now transcode all face and mipmap levels into memory, one mip level at a time.
   void *pImages[cCRNMaxFaces][cCRNMaxLevels];
   crn_uint32 image_size_in_bytes[cCRNMaxLevels];
   memset(pImages, 0, sizeof(pImages));
   memset(image_size_in_bytes, 0, sizeof(image_size_in_bytes));

   crn_uint32 total_unpacked_texels = 0;

   double total_unpack_time = 0.0f;
   for (crn_uint32 level_index = 0; level_index < tex_info.m_levels; level_index++)
   {
      // Compute the face's width, height, number of DXT blocks per row/col, etc.
      const crn_uint32 width = std::max(1U, tex_info.m_width >> level_index);
      const crn_uint32 height = std::max(1U, tex_info.m_height >> level_index);
      const crn_uint32 blocks_x = std::max(1U, (width + 3) >> 2);
      const crn_uint32 blocks_y = std::max(1U, (height + 3) >> 2);
      const crn_uint32 row_pitch = blocks_x * crnd::crnd_get_bytes_per_dxt_block(tex_info.m_format);
      const crn_uint32 total_face_size = row_pitch * blocks_y;
      
      image_size_in_bytes[level_index] = total_face_size;

      for (crn_uint32 face_index = 0; face_index < tex_info.m_faces; face_index++)
      {
         void *p = malloc(total_face_size);
         if (!p)
         {
            for (crn_uint32 f = 0; f < cCRNMaxFaces; f++) 
               for (crn_uint32 l = 0; l < cCRNMaxLevels; l++) 
                  free(pImages[f][l]);
            crnd::crnd_unpack_end(pContext);
            free(pSrc_file_data);
            return error("Out of memory!");
         }
         
         pImages[face_index][level_index] = p;
      }
      
      // Prepare the face pointer array needed by crnd_unpack_level().
      void *pDecomp_images[cCRNMaxFaces];
      for (crn_uint32 face_index = 0; face_index < tex_info.m_faces; face_index++)
         pDecomp_images[face_index] = pImages[face_index][level_index];
      
      // Now transcode the level to raw DXTn
      tm.start();
      if (!crnd::crnd_unpack_level(pContext, pDecomp_images, total_face_size, row_pitch, level_index))
      {
         for (crn_uint32 f = 0; f < cCRNMaxFaces; f++) 
            for (crn_uint32 l = 0; l < cCRNMaxLevels; l++) 
               free(pImages[f][l]);

         crnd::crnd_unpack_end(pContext);
         free(pSrc_file_data);
         
         return error("Failed transcoding texture!");
      }

      total_unpack_time += tm.get_elapsed_ms();
      total_unpacked_texels += (blocks_x * blocks_y * 16);
   }

   printf("crnd_unpack_begin time: %3.3fms\n", total_unpack_begin_time);
   printf("Total crnd_unpack_level time: %3.3fms\n", total_unpack_time);
   double total_time = total_unpack_begin_time + total_unpack_time;
   printf("Total transcode time: %3.3fms\n", total_time);
   printf("Total texels transcoded: %u\n", total_unpacked_texels);
   printf("Overall transcode throughput: %3.3f million texels/sec\n", (total_unpacked_texels / (total_time / 1000.0f)) / 1000000.0f);

   // Now write the DXTn data to the DDS file in face-major order.
   for (crn_uint32 face_index = 0; face_index < tex_info.m_faces; face_index++)
      for (crn_uint32 level_index = 0; level_index < tex_info.m_levels; level_index++)
         fwrite(pImages[face_index][level_index], image_size_in_bytes[level_index], 1, pDDS_file);
   
   for (crn_uint32 f = 0; f < cCRNMaxFaces; f++) 
      for (crn_uint32 l = 0; l < cCRNMaxLevels; l++) 
         free(pImages[f][l]);

   crnd::crnd_unpack_end(pContext);
   free(pSrc_file_data);
   
   if (fclose(pDDS_file) == EOF)
   {
      return error("Failed writing to DDS file!\n");
   }
   
   return EXIT_SUCCESS;
}
