// File: dds_defs.h
// DX9 .DDS file header definitions.
#ifndef CRNLIB_DDS_DEFS_H
#define CRNLIB_DDS_DEFS_H

#include "crnlib.h"

#define CRNLIB_PIXEL_FMT_FOURCC(a, b, c, d) ((a) | ((b) << 8U) | ((c) << 16U) | ((d) << 24U))

namespace crnlib
{
   enum pixel_format
   {
      PIXEL_FMT_INVALID               = 0,

      PIXEL_FMT_DXT1                  = CRNLIB_PIXEL_FMT_FOURCC('D', 'X', 'T', '1'),
      PIXEL_FMT_DXT2                  = CRNLIB_PIXEL_FMT_FOURCC('D', 'X', 'T', '2'),
      PIXEL_FMT_DXT3                  = CRNLIB_PIXEL_FMT_FOURCC('D', 'X', 'T', '3'),
      PIXEL_FMT_DXT4                  = CRNLIB_PIXEL_FMT_FOURCC('D', 'X', 'T', '4'),
      PIXEL_FMT_DXT5                  = CRNLIB_PIXEL_FMT_FOURCC('D', 'X', 'T', '5'),
      PIXEL_FMT_3DC                   = CRNLIB_PIXEL_FMT_FOURCC('A', 'T', 'I', '2'), // DXN_YX
      PIXEL_FMT_DXN                   = CRNLIB_PIXEL_FMT_FOURCC('A', '2', 'X', 'Y'), // DXN_XY
      PIXEL_FMT_DXT5A                 = CRNLIB_PIXEL_FMT_FOURCC('A', 'T', 'I', '1'), // ATI1N, http://developer.amd.com/media/gpu_assets/Radeon_X1x00_Programming_Guide.pdf

      // Non-standard, crnlib-specific pixel formats (some of these are supported by ATI's Compressonator)
      PIXEL_FMT_DXT5_CCxY             = CRNLIB_PIXEL_FMT_FOURCC('C', 'C', 'x', 'Y'),
      PIXEL_FMT_DXT5_xGxR             = CRNLIB_PIXEL_FMT_FOURCC('x', 'G', 'x', 'R'),
      PIXEL_FMT_DXT5_xGBR             = CRNLIB_PIXEL_FMT_FOURCC('x', 'G', 'B', 'R'),
      PIXEL_FMT_DXT5_AGBR             = CRNLIB_PIXEL_FMT_FOURCC('A', 'G', 'B', 'R'),

      PIXEL_FMT_DXT1A                 = CRNLIB_PIXEL_FMT_FOURCC('D', 'X', '1', 'A'),
      PIXEL_FMT_ETC1                  = CRNLIB_PIXEL_FMT_FOURCC('E', 'T', 'C', '1'),

      PIXEL_FMT_R8G8B8                = CRNLIB_PIXEL_FMT_FOURCC('R', 'G', 'B', 'x'),
      PIXEL_FMT_L8                    = CRNLIB_PIXEL_FMT_FOURCC('L', 'x', 'x', 'x'),
      PIXEL_FMT_A8                    = CRNLIB_PIXEL_FMT_FOURCC('x', 'x', 'x', 'A'),
      PIXEL_FMT_A8L8                  = CRNLIB_PIXEL_FMT_FOURCC('L', 'x', 'x', 'A'),
      PIXEL_FMT_A8R8G8B8              = CRNLIB_PIXEL_FMT_FOURCC('R', 'G', 'B', 'A')
   };

   const crn_uint32 cDDSMaxImageDimensions = 8192U;

   // Total size of header is sizeof(uint32)+cDDSSizeofDDSurfaceDesc2;
   const crn_uint32 cDDSSizeofDDSurfaceDesc2 = 124;

   // "DDS "
   const crn_uint32 cDDSFileSignature = 0x20534444;

   struct DDCOLORKEY
   {
      crn_uint32 dwUnused0;
      crn_uint32 dwUnused1;
   };

   struct DDPIXELFORMAT
   {
      crn_uint32 dwSize;
      crn_uint32 dwFlags;
      crn_uint32 dwFourCC;
      crn_uint32 dwRGBBitCount;     // ATI compressonator and crnlib will place a FOURCC code here for swizzled/cooked DXTn formats
      crn_uint32 dwRBitMask;
      crn_uint32 dwGBitMask;
      crn_uint32 dwBBitMask;
      crn_uint32 dwRGBAlphaBitMask;
   };

   struct DDSCAPS2
   {
      crn_uint32 dwCaps;
      crn_uint32 dwCaps2;
      crn_uint32 dwCaps3;
      crn_uint32 dwCaps4;
   };

   struct DDSURFACEDESC2
   {
      crn_uint32 dwSize;
      crn_uint32 dwFlags;
      crn_uint32 dwHeight;
      crn_uint32 dwWidth;
      union
      {
         crn_int32 lPitch;
         crn_uint32 dwLinearSize;
      };
      crn_uint32 dwBackBufferCount;
      crn_uint32 dwMipMapCount;
      crn_uint32 dwAlphaBitDepth;
      crn_uint32 dwUnused0;
      crn_uint32 lpSurface;
      DDCOLORKEY unused0;
      DDCOLORKEY unused1;
      DDCOLORKEY unused2;
      DDCOLORKEY unused3;
      DDPIXELFORMAT ddpfPixelFormat;
      DDSCAPS2 ddsCaps;
      crn_uint32 dwUnused1;
   };
   
   const crn_uint32 DDSD_CAPS                   = 0x00000001;
   const crn_uint32 DDSD_HEIGHT                 = 0x00000002;
   const crn_uint32 DDSD_WIDTH                  = 0x00000004;
   const crn_uint32 DDSD_PITCH                  = 0x00000008;

   const crn_uint32 DDSD_BACKBUFFERCOUNT        = 0x00000020;
   const crn_uint32 DDSD_ZBUFFERBITDEPTH        = 0x00000040;
   const crn_uint32 DDSD_ALPHABITDEPTH          = 0x00000080;

   const crn_uint32 DDSD_LPSURFACE              = 0x00000800;
                                                            
   const crn_uint32 DDSD_PIXELFORMAT            = 0x00001000;
   const crn_uint32 DDSD_CKDESTOVERLAY          = 0x00002000;
   const crn_uint32 DDSD_CKDESTBLT              = 0x00004000;
   const crn_uint32 DDSD_CKSRCOVERLAY           = 0x00008000;

   const crn_uint32 DDSD_CKSRCBLT               = 0x00010000;
   const crn_uint32 DDSD_MIPMAPCOUNT            = 0x00020000;
   const crn_uint32 DDSD_REFRESHRATE            = 0x00040000;
   const crn_uint32 DDSD_LINEARSIZE             = 0x00080000;

   const crn_uint32 DDSD_TEXTURESTAGE           = 0x00100000;
   const crn_uint32 DDSD_FVF                    = 0x00200000;
   const crn_uint32 DDSD_SRCVBHANDLE            = 0x00400000;
   const crn_uint32 DDSD_DEPTH                  = 0x00800000;
   
   const crn_uint32 DDSD_ALL                    = 0x00fff9ee;

   const crn_uint32 DDPF_ALPHAPIXELS            = 0x00000001;
   const crn_uint32 DDPF_ALPHA                  = 0x00000002;
   const crn_uint32 DDPF_FOURCC                 = 0x00000004;
   const crn_uint32 DDPF_PALETTEINDEXED8        = 0x00000020;
   const crn_uint32 DDPF_RGB                    = 0x00000040;
   const crn_uint32 DDPF_LUMINANCE              = 0x00020000;

   const crn_uint32 DDSCAPS_COMPLEX             = 0x00000008;
   const crn_uint32 DDSCAPS_TEXTURE             = 0x00001000;
   const crn_uint32 DDSCAPS_MIPMAP              = 0x00400000;

   const crn_uint32 DDSCAPS2_CUBEMAP            = 0x00000200;
   const crn_uint32 DDSCAPS2_CUBEMAP_POSITIVEX  = 0x00000400;
   const crn_uint32 DDSCAPS2_CUBEMAP_NEGATIVEX  = 0x00000800;

   const crn_uint32 DDSCAPS2_CUBEMAP_POSITIVEY  = 0x00001000;
   const crn_uint32 DDSCAPS2_CUBEMAP_NEGATIVEY  = 0x00002000;
   const crn_uint32 DDSCAPS2_CUBEMAP_POSITIVEZ  = 0x00004000;
   const crn_uint32 DDSCAPS2_CUBEMAP_NEGATIVEZ  = 0x00008000;

   const crn_uint32 DDSCAPS2_VOLUME             = 0x00200000;

} // namespace crnlib

#endif // CRNLIB_DDS_DEFS_H
