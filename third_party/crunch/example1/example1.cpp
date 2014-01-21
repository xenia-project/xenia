// File: example1.cpp - Simple command line tool that uses the crnlib lib and the crn_decomp.h header file library 
// to compress, transcode/unpack, and inspect CRN/DDS textures.
// See Copyright Notice and license at the end of inc/crnlib.h
#include <stdlib.h>
#include <stdio.h>
#include <math.h>
#include <algorithm>

// Public crnlib header.
#include "crnlib.h"

// CRN transcoder library.
#include "crn_decomp.h"
// .DDS file format definitions.
#include "dds_defs.h"

// stb_image, for loading/saving image files.
#ifdef _MSC_VER
#pragma warning (disable: 4244) // conversion from 'int' to 'uint8', possible loss of data
#pragma warning (disable: 4100) // unreferenced formal parameter
#endif
#include "stb_image.h"

// windows.h is only needed here for GetSystemInfo().
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "windows.h"

using namespace crnlib;

const int cDefaultCRNQualityLevel = 128;

static int print_usage()
{
   printf("Description: Simple crnlib API example program.\n");
   printf("Copyright (c) 2010-2011 Tenacious Software LLC\n");
   printf("Usage: example1 [mode: i/c/d] [source_file] [options]\n");
   printf("\nModes:\n");
   printf("c: Compress to .DDS or .CRN using the crn_compress() func. in crnlib.h\n");
   printf("   The default output format is .DDS\n");
   printf("   Supported source image formats:\n");
   printf("   Baseline JPEG, PNG, BMP, TGA, PSD, and HDR\n");
   printf("d: Transcodes a .CRN file to .DDS using the crn_decompress_crn_to_dds() func.,\n");
   printf("or unpacks each face and mipmap level in a .DDS file to multiple .TGA files.\n");
   printf("i: Display info about source_file.\n");
   printf("\nOptions:\n");
   printf("-out filename - Force output filename.\n");
   printf("\nCompression mode options:\n");
   printf("-crn - Generate a .CRN file instead of .DDS\n");
   printf("-bitrate # - Specify desired CRN/DDS bits/texel, from [.1-8]\n");
   printf(" When writing .DDS: -bitrate or -quality enable clustered DXTn compression.\n");
   printf("-quality # - Specify CRN/DDS quality level factor, from [0-255]\n");
   printf("-noAdaptiveBlocks - Always use 4x4 blocks instead of up to 8x8 macroblocks\n");
   printf("-nonsrgb - Input is not sRGB: disables gamma filtering, perceptual metrics.\n");
   printf("-nomips - Don't generate mipmaps\n");
   printf("-setalphatoluma - Set alpha channel to luma before compression.\n");
   printf("-converttoluma - Set RGB to luma before compression.\n");
   printf("-pixelformat fmt - Output file's crn_format: DXT1, DXT1A, DXT3, DXT5_CCxY,\n");
   printf(" DXT5_xGxR, DXT5_xGBR, DXT5_AGBR, DXN_XY (ATI 3DC), DXN_YX (ATI 3DC),\n");
   printf(" DXT5A (ATN1N)\n");
   printf(" If no output format is specified, this example uses either DXT1 or DXT5.\n");
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

// Cracks a CRN's file header using the helper functions in crn_decomp.h.
static bool print_crn_info(const crn_uint8 *pData, crn_uint32 data_size)
{
   crnd::crn_file_info file_info;
   if (!crnd::crnd_validate_file(pData, data_size, &file_info))
      return false;

   printf("crnd_validate_file:\n");
   printf("File size: %u\nActualDataSize: %u\nHeaderSize: %u\nTotalPaletteSize: %u\nTablesSize: %u\nLevels: %u\n", data_size,
      file_info.m_actual_data_size, file_info.m_header_size, file_info.m_total_palette_size, file_info.m_tables_size, file_info.m_levels);
   
   printf("LevelCompressedSize: ");
   for (crn_uint32 i = 0; i < cCRNMaxLevels; i++)
      printf("%u ", file_info.m_level_compressed_size[i]);
   printf("\n");

   printf("ColorEndpointPaletteSize: %u\n", file_info.m_color_endpoint_palette_entries);
   printf("ColorSelectorPaletteSize: %u\n", file_info.m_color_selector_palette_entries);
   printf("AlphaEndpointPaletteSize: %u\n", file_info.m_alpha_endpoint_palette_entries);
   printf("AlphaSelectorPaletteSize: %u\n", file_info.m_alpha_selector_palette_entries);
   
   printf("crnd_get_texture_info:\n");
   crnd::crn_texture_info tex_info;
   if (!crnd::crnd_get_texture_info(pData, data_size, &tex_info))
      return false;

   printf("Dimensions: %ux%u\nLevels: %u\nFaces: %u\nBytesPerBlock: %u\nUserData0: %u\nUserData1: %u\nCrnFormat: %S\n",
      tex_info.m_width, tex_info.m_height, tex_info.m_levels, tex_info.m_faces, tex_info.m_bytes_per_block, tex_info.m_userdata0, tex_info.m_userdata1, crn_get_format_string(tex_info.m_format));

   return true;
}

// Cracks the DDS header and dump its contents.
static bool print_dds_info(const void *pData, crn_uint32 data_size)
{
   if ((data_size < 128) || (*reinterpret_cast<const crn_uint32*>(pData) != crnlib::cDDSFileSignature))
      return false;

   const crnlib::DDSURFACEDESC2 &desc = *reinterpret_cast<const crnlib::DDSURFACEDESC2*>((reinterpret_cast<const crn_uint8*>(pData) + sizeof(crn_uint32)));
   if (desc.dwSize != sizeof(crnlib::DDSURFACEDESC2))
      return false;

   printf("DDS file information:\n");
   printf("File size: %u\nDimensions: %ux%u\nPitch/LinearSize: %u\n", data_size, desc.dwWidth, desc.dwHeight, desc.dwLinearSize);
   printf("MipMapCount: %u\nAlphaBitDepth: %u\n", desc.dwMipMapCount, desc.dwAlphaBitDepth);

   const char *pDDSDFlagNames[] = 
   {
      "DDSD_CAPS", "DDSD_HEIGHT", "DDSD_WIDTH", "DDSD_PITCH",
      NULL, "DDSD_BACKBUFFERCOUNT", "DDSD_ZBUFFERBITDEPTH", "DDSD_ALPHABITDEPTH",
      NULL, NULL, NULL, "DDSD_LPSURFACE",
      "DDSD_PIXELFORMAT", "DDSD_CKDESTOVERLAY", "DDSD_CKDESTBLT", "DDSD_CKSRCOVERLAY",
      "DDSD_CKSRCBLT", "DDSD_MIPMAPCOUNT", "DDSD_REFRESHRATE", "DDSD_LINEARSIZE",
      "DDSD_TEXTURESTAGE", "DDSD_FVF", "DDSD_SRCVBHANDLE", "DDSD_DEPTH" 
   };

   printf("DDSD Flags: 0x%08X ", desc.dwFlags);
   for (int i = 0; i < sizeof(pDDSDFlagNames)/sizeof(pDDSDFlagNames[0]); i++)
      if ((pDDSDFlagNames[i]) && (desc.dwFlags & (1 << i)))
         printf("%s ", pDDSDFlagNames[i]);
   printf("\n\n");

   printf("ddpfPixelFormat.dwFlags: 0x%08X ", desc.ddpfPixelFormat.dwFlags);
   if (desc.ddpfPixelFormat.dwFlags & DDPF_ALPHAPIXELS) printf("DDPF_ALPHAPIXELS ");
   if (desc.ddpfPixelFormat.dwFlags & DDPF_ALPHA) printf("DDPF_ALPHA ");
   if (desc.ddpfPixelFormat.dwFlags & DDPF_FOURCC) printf("DDPF_FOURCC ");
   if (desc.ddpfPixelFormat.dwFlags & DDPF_PALETTEINDEXED8) printf("DDPF_PALETTEINDEXED8 ");
   if (desc.ddpfPixelFormat.dwFlags & DDPF_RGB) printf("DDPF_RGB ");
   if (desc.ddpfPixelFormat.dwFlags & DDPF_LUMINANCE) printf("DDPF_LUMINANCE ");
   printf("\n");

   printf("ddpfPixelFormat.dwFourCC: 0x%08X '%c' '%c' '%c' '%c'\n",
      desc.ddpfPixelFormat.dwFourCC, 
      std::max(32U, desc.ddpfPixelFormat.dwFourCC & 0xFF), 
      std::max(32U, (desc.ddpfPixelFormat.dwFourCC >> 8) & 0xFF), 
      std::max(32U, (desc.ddpfPixelFormat.dwFourCC >> 16) & 0xFF), 
      std::max(32U, (desc.ddpfPixelFormat.dwFourCC >> 24) & 0xFF));

   printf("dwRGBBitCount: %u 0x%08X\n",
      desc.ddpfPixelFormat.dwRGBBitCount, desc.ddpfPixelFormat.dwRGBBitCount);

   printf("dwRGBBitCount as FOURCC: '%c' '%c' '%c' '%c'\n", 
      std::max(32U, desc.ddpfPixelFormat.dwRGBBitCount & 0xFF), 
      std::max(32U, (desc.ddpfPixelFormat.dwRGBBitCount >> 8) & 0xFF), 
      std::max(32U, (desc.ddpfPixelFormat.dwRGBBitCount >> 16) & 0xFF), 
      std::max(32U, (desc.ddpfPixelFormat.dwRGBBitCount >> 24) & 0xFF));

   printf("dwRBitMask: 0x%08X\ndwGBitMask: 0x%08X\ndwBBitMask: 0x%08X\ndwRGBAlphaBitMask: 0x%08X\n",
      desc.ddpfPixelFormat.dwRBitMask, desc.ddpfPixelFormat.dwGBitMask, desc.ddpfPixelFormat.dwBBitMask, desc.ddpfPixelFormat.dwRGBAlphaBitMask);

   printf("\n");
   printf("ddsCaps.dwCaps: 0x%08X ", desc.ddsCaps.dwCaps);
   if (desc.ddsCaps.dwCaps & DDSCAPS_COMPLEX) printf("DDSCAPS_COMPLEX ");
   if (desc.ddsCaps.dwCaps & DDSCAPS_TEXTURE) printf("DDSCAPS_TEXTURE ");
   if (desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP) printf("DDSCAPS_MIPMAP");
   printf("\n");

   printf("ddsCaps.dwCaps2: 0x%08X ", desc.ddsCaps.dwCaps2);
   const char *pDDCAPS2FlagNames[] = 
   {
      NULL, NULL, NULL, NULL, NULL, NULL, NULL, NULL, 
      NULL, "DDSCAPS2_CUBEMAP", "DDSCAPS2_CUBEMAP_POSITIVEX", "DDSCAPS2_CUBEMAP_NEGATIVEX", 
      "DDSCAPS2_CUBEMAP_POSITIVEY", "DDSCAPS2_CUBEMAP_NEGATIVEY", "DDSCAPS2_CUBEMAP_POSITIVEZ", "DDSCAPS2_CUBEMAP_NEGATIVEZ", 
      NULL, NULL, NULL, NULL, 
      NULL, "DDSCAPS2_VOLUME"
   };
   for (int i = 0; i < sizeof(pDDCAPS2FlagNames)/sizeof(pDDCAPS2FlagNames[0]); i++)
      if ((pDDCAPS2FlagNames[i]) && (desc.ddsCaps.dwCaps2 & (1 << i)))
         printf("%s ", pDDCAPS2FlagNames[i]);
   printf("\n");

   printf("ddsCaps.dwCaps3: 0x%08X\nddsCaps.dwCaps4: 0x%08X\n", 
      desc.ddsCaps.dwCaps3, desc.ddsCaps.dwCaps4);

   return true;
}

// CRN/DDS compression callback function.
static crn_bool progress_callback_func(crn_uint32 phase_index, crn_uint32 total_phases, crn_uint32 subphase_index, crn_uint32 total_subphases, void* pUser_data_ptr)
{
   int percentage_complete = (int)(.5f + (phase_index + float(subphase_index) / total_subphases) * 100.0f) / total_phases;
   printf("\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\b\bProcessing: %u%%", std::min(100, std::max(0, percentage_complete)));
   return true;
}

int main(int argc, char *argv[])
{
   printf("example1 - Version v%u.%02u Built " __DATE__ ", " __TIME__ "\n", CRNLIB_VERSION / 100, CRNLIB_VERSION % 100);

   if (argc < 3)
      return print_usage();

   // Parse command line options
   int mode = argv[1][0];
   if ((mode != 'c') && (mode != 'd') && (mode != 'i'))
      return error("Invalid mode!\n");
   
   const char *pSrc_filename = argv[2];
   char out_filename[FILENAME_MAX] = { '\0' };
   
   float bitrate = 0.0f;
   int quality_level = -1;
   bool srgb_colorspace = true;
   bool create_mipmaps = true;
   bool output_crn = false;
   crn_format fmt = cCRNFmtInvalid;
   bool use_adaptive_block_sizes = true;
   bool set_alpha_to_luma = false;
   bool convert_to_luma = false;
   bool enable_dxt1a = false;

   for (int i = 3; i < argc; i++)
   {
      if (argv[i][0] == '/') 
         argv[i][0] = '-';

      if (!_stricmp(argv[i], "-crn"))
      {
         output_crn = true;
      }
      else if (!_stricmp(argv[i], "-pixelformat"))
      {
         if (++i >= argc)
            return error("Expected pixel format!");

         if (!_stricmp(argv[i], "dxt1a"))
         {
            enable_dxt1a = true;
            fmt = cCRNFmtDXT1;
         }
         else
         {
            uint f;
            for (f = 0; f < cCRNFmtTotal; f++)
            {
               if (!_stricmp(argv[i], crn_get_format_string(static_cast<crn_format>(f))))
               {
                  fmt = static_cast<crn_format>(f);
                  break;
               }
            }
            if (f == cCRNFmtTotal)
               return error("Unrecognized pixel format: %s\n", argv[i]);
         }
      }
      else if (!_stricmp(argv[i], "-bitrate"))
      {
         if (++i >= argc)
            return error("Invalid bitrate!");

         bitrate = (float)atof(argv[i]);
         if ((bitrate < .1f) || (bitrate > 8.0f))
            return error("Invalid bitrate!");
      }
      else if (!_stricmp(argv[i], "-quality"))
      {
         if (++i >= argc)
            return error("Invalid quality level!");

         quality_level = atoi(argv[i]);
         if ((quality_level < 0) || (quality_level > cCRNMaxQualityLevel))
            return error("Invalid quality level!");
      }
      else if (!_stricmp(argv[i], "-out"))
      {
         if (++i >= argc)
            return error("Expected output filename!");

         strcpy_s(out_filename, sizeof(out_filename), argv[i]);
      }
      else if (!_stricmp(argv[i], "-nonsrgb"))
         srgb_colorspace = false;
      else if (!_stricmp(argv[i], "-nomips"))
         create_mipmaps = false;
      else if (!_stricmp(argv[i], "-noAdaptiveBlocks"))
         use_adaptive_block_sizes = false;
      else if (!_stricmp(argv[i], "-setalphatoluma"))
         set_alpha_to_luma = true;
      else if (!_stricmp(argv[i], "-converttoluma"))
         convert_to_luma = true;
      else
         return error("Invalid option: %s\n", argv[i]);
   }

   char drive_buf[_MAX_DRIVE], dir_buf[_MAX_DIR], fname_buf[_MAX_FNAME], ext_buf[_MAX_EXT];
   if (_splitpath_s(pSrc_filename, drive_buf, _MAX_DRIVE, dir_buf, _MAX_DIR, fname_buf, _MAX_FNAME, ext_buf, _MAX_EXT))
      return error("Invalid source filename!\n");
   
   // Load the source file into memory.
   printf("Loading source file: %s\n", pSrc_filename);
   crn_uint32 src_file_size;
   crn_uint8 *pSrc_file_data = read_file_into_buffer(pSrc_filename, src_file_size);
   if (!pSrc_file_data)
      return error("Unable to read source file\n");
   
   if (mode == 'i')
   {
      // Information
      if (_stricmp(ext_buf, ".crn") == 0)
      {
         if (!print_crn_info(pSrc_file_data, src_file_size))
         {
            free(pSrc_file_data);
            return error("Not a CRN file!\n");
         }
      }
      else if (_stricmp(ext_buf, ".dds") == 0)
      {
         if (!print_dds_info(pSrc_file_data, src_file_size))
         {
            free(pSrc_file_data);
            return error("Not a DDS file!\n");
         }
      }
      else 
      {
         // Try parsing the source file as a regular image.
         int x, y, actual_comps;
         stbi_uc *p = stbi_load_from_memory(pSrc_file_data, src_file_size, &x, &y, &actual_comps, 4);
         if (!p)
         {
            free(pSrc_file_data);
            return error("Failed reading image file!\n");
         }
         stbi_image_free(p);
         
         printf("File size: %u\nDimensions: %ix%i\nActual Components: %i\n", src_file_size, x, y, actual_comps);
      }
   }
   else if (mode == 'c')
   {
      // Compression to DDS or CRN.

      // If the user has explicitly specified an output file, check the output file's extension to ensure we write the expected format.
      if (out_filename[0]) 
      {
         char out_fname_buf[_MAX_FNAME], out_ext_buf[_MAX_EXT];
         _splitpath_s(out_filename, NULL, 0, NULL, 0, out_fname_buf, _MAX_FNAME, out_ext_buf, _MAX_EXT);
         if (!_stricmp(out_ext_buf, ".crn"))
            output_crn = true;
         else if (!_stricmp(out_ext_buf, ".dds"))
            output_crn = false;
      }

      // Load source image
      int width, height, actual_comps;
      crn_uint32 *pSrc_image = (crn_uint32*)stbi_load_from_memory(pSrc_file_data, src_file_size, &width, &height, &actual_comps, 4);
      if (!pSrc_image)
      {
         free(pSrc_file_data);
         return error("Failed reading image file!\n");
      }
            
      printf("Source file size: %u, Dimensions: %ux%u\nActual Components: %u\n", src_file_size, width, height, actual_comps);
      
      // Fill in compression parameters struct.
      bool has_alpha_channel = actual_comps > 3;

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

      crn_comp_params comp_params;
      comp_params.m_width = width;
      comp_params.m_height = height;
      comp_params.set_flag(cCRNCompFlagPerceptual, srgb_colorspace);
      comp_params.set_flag(cCRNCompFlagDXT1AForTransparency, enable_dxt1a && has_alpha_channel);
      comp_params.set_flag(cCRNCompFlagHierarchical, use_adaptive_block_sizes);
      comp_params.m_file_type = output_crn ? cCRNFileTypeCRN : cCRNFileTypeDDS;
      comp_params.m_format = (fmt != cCRNFmtInvalid) ? fmt : (has_alpha_channel ? cCRNFmtDXT5 : cCRNFmtDXT1);

      // Important note: This example only feeds a single source image to the compressor, and it internaly generates mipmaps from that source image.
      // If you want, there's nothing stopping you from generating the mipmaps on your own, then feeding the multiple source images 
      // to the compressor. Just set the crn_mipmap_params::m_mode member (set below) to cCRNMipModeUseSourceMips.
      comp_params.m_pImages[0][0] = pSrc_image;
      
      if (bitrate > 0.0f)
         comp_params.m_target_bitrate = bitrate;
      else if (quality_level >= 0)
         comp_params.m_quality_level = quality_level;
      else if (output_crn)
      {
         // Set a default quality level for CRN, otherwise we'll get the default (highest quality) which leads to huge compressed palettes.
         comp_params.m_quality_level = cDefaultCRNQualityLevel;
      }
      
      // Determine the # of helper threads (in addition to the main thread) to use during compression. NumberOfCPU's-1 is reasonable.
      SYSTEM_INFO g_system_info;
      GetSystemInfo(&g_system_info);  
      int num_helper_threads = std::max<int>(0, (int)g_system_info.dwNumberOfProcessors - 1);
      comp_params.m_num_helper_threads = num_helper_threads;

      comp_params.m_pProgress_func = progress_callback_func;
            
      // Fill in mipmap parameters struct.
      crn_mipmap_params mip_params;
      mip_params.m_gamma_filtering = srgb_colorspace;
      mip_params.m_mode = create_mipmaps ? cCRNMipModeGenerateMips : cCRNMipModeNoMips;

      crn_uint32 actual_quality_level;
      float actual_bitrate;
      crn_uint32 output_file_size;

      printf("Compressing to %s\n", crn_get_format_string(comp_params.m_format));
      
      // Now compress to DDS or CRN.
      void *pOutput_file_data = crn_compress(comp_params, mip_params, output_file_size, &actual_quality_level, &actual_bitrate);
      printf("\n");

      if (!pOutput_file_data)
      {
         stbi_image_free(pSrc_image);
         free(pSrc_file_data);
         return error("Compression failed!");
      }

      printf("Compressed to %u bytes, quality level: %u, effective bitrate: %f\n", output_file_size, actual_quality_level, actual_bitrate);
      
      // Write the output file.
      char dst_filename[FILENAME_MAX];
      sprintf_s(dst_filename, sizeof(dst_filename), "%s%s%s%s", drive_buf, dir_buf, fname_buf, output_crn ? ".crn" : ".dds");
      if (out_filename[0]) strcpy(dst_filename, out_filename);

      printf("Writing %s file: %s\n", output_crn ? "CRN" : "DDS", dst_filename);
      FILE *pFile = fopen(dst_filename, "wb");
      if ((!pFile) || (fwrite(pOutput_file_data, output_file_size, 1, pFile) != 1) || (fclose(pFile) == EOF))
      {
         free(pSrc_file_data);
         crn_free_block(pOutput_file_data);
         stbi_image_free(pSrc_image);
         return error("Failed writing to output file!\n");
      }

      crn_free_block(pOutput_file_data);
      stbi_image_free(pSrc_image);
   }
   else if (_stricmp(ext_buf, ".crn") == 0)
   {
      // Decompress/transcode CRN to DDS.
      printf("Decompressing CRN to DDS\n");

      // Transcode the CRN file to a DDS file in memory.
      crn_uint32 dds_file_size = src_file_size;
      void *pDDS_file_data = crn_decompress_crn_to_dds(pSrc_file_data, dds_file_size);
      if (!pDDS_file_data)
      {
         free(pSrc_file_data);
         return error("Failed decompressing CRN file!\n");
      }

      // Now write the DDS file to disk.
      char dst_filename[FILENAME_MAX];
      sprintf_s(dst_filename, sizeof(dst_filename), "%s%s%s.dds", drive_buf, dir_buf, fname_buf);
      if (out_filename[0]) strcpy(dst_filename, out_filename);
      
      printf("Writing file: %s\n", dst_filename);
      FILE *pFile = fopen(dst_filename, "wb");
      if ((!pFile) || (fwrite(pDDS_file_data, dds_file_size, 1, pFile) != 1) || (fclose(pFile) == EOF))
      {
         crn_free_block(pDDS_file_data);
         free(pSrc_file_data);
         return error("Failed writing to output file!\n");
      }

      printf("\n");

      print_dds_info(pDDS_file_data, dds_file_size);

      crn_free_block(pDDS_file_data);
   }
   else if (_stricmp(ext_buf, ".dds") == 0)
   {
      // Unpack DDS to one or more TGA's.
      if (out_filename[0])
         _splitpath_s(out_filename, drive_buf, _MAX_DRIVE, dir_buf, _MAX_DIR, fname_buf, _MAX_FNAME, ext_buf, _MAX_EXT);

      crn_texture_desc tex_desc;
      crn_uint32 *pImages[cCRNMaxFaces * cCRNMaxLevels];
      if (!crn_decompress_dds_to_images(pSrc_file_data, src_file_size, pImages, tex_desc))
      {
         free(pSrc_file_data);
         return error("Failed unpacking DDS file!\n");
      }
      
      printf("Decompressed texture Dimensions: %ux%u, Faces: %u, Levels: %u, FourCC: 0x%08X '%c' '%c' '%c' '%c'\n",
         tex_desc.m_width, tex_desc.m_height, tex_desc.m_faces, tex_desc.m_levels, tex_desc.m_fmt_fourcc, 
         std::max(32U, tex_desc.m_fmt_fourcc & 0xFF), 
         std::max(32U, (tex_desc.m_fmt_fourcc >> 8) & 0xFF), 
         std::max(32U, (tex_desc.m_fmt_fourcc >> 16) & 0xFF), 
         std::max(32U, (tex_desc.m_fmt_fourcc >> 24) & 0xFF));

      for (crn_uint32 face_index = 0; face_index < tex_desc.m_faces; face_index++)
      {
         for (crn_uint32 level_index = 0; level_index < tex_desc.m_levels; level_index++)
         {
            int width = std::max(1U, tex_desc.m_width >> level_index);
            int height = std::max(1U, tex_desc.m_height >> level_index);

            char dst_filename[FILENAME_MAX];
            sprintf_s(dst_filename, sizeof(dst_filename), "%s%s%s_face%u_mip%u.tga", drive_buf, dir_buf, fname_buf, face_index, level_index);
            
            printf("Writing file: %s\n", dst_filename);
            if (!stbi_write_tga(dst_filename, width, height, 4, pImages[level_index + face_index * tex_desc.m_levels]))
            {
               crn_free_all_images(pImages, tex_desc);
               free(pSrc_file_data);

               return error("Failed writing output file!\n");
            }
         }
      }
      
      crn_free_all_images(pImages, tex_desc);
   }
   else
   {
      free(pSrc_file_data);
      return error("Decompression mode only supports .dds or .crn files!\n");
   }

   free(pSrc_file_data);
   
   return EXIT_SUCCESS;
}
