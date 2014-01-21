// File: crn_image_utils.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_image.h"
#include "crn_data_stream_serializer.h"

namespace crnlib
{
   enum pixel_format;

   namespace image_utils
   {
      enum read_flags_t
      {
         cReadFlagForceSTB = 1,

         cReadFlagsAllFlags = 1
      };

      bool read_from_stream_stb(data_stream_serializer& serializer, image_u8& img);
      bool read_from_stream_jpgd(data_stream_serializer& serializer, image_u8& img);
      bool read_from_stream(image_u8& dest, data_stream_serializer& serializer, uint read_flags = 0);
      bool read_from_file(image_u8& dest, const char* pFilename, uint read_flags = 0);

      // Reads texture from memory, results returned stb_image.c style.
      // *pActual_comps is set to 1, 3, or 4. req_comps must range from 1-4.
      uint8* read_from_memory(const uint8* pImage, int nSize, int* pWidth, int* pHeight, int* pActualComps, int req_comps, const char* pFilename);

      enum
      {
         cWriteFlagIgnoreAlpha            = 0x00000001,
         cWriteFlagGrayscale              = 0x00000002,

         cWriteFlagJPEGH1V1               = 0x00010000,
         cWriteFlagJPEGH2V1               = 0x00020000,
         cWriteFlagJPEGH2V2               = 0x00040000,
         cWriteFlagJPEGTwoPass            = 0x00080000,
         cWriteFlagJPEGNoChromaDiscrim    = 0x00100000,
         cWriteFlagJPEGQualityLevelMask   = 0xFF000000,
         cWriteFlagJPEGQualityLevelShift  = 24,
      };

      const int cLumaComponentIndex = -1;

      inline uint create_jpeg_write_flags(uint base_flags, uint quality_level) { CRNLIB_ASSERT(quality_level <= 100); return base_flags | ((quality_level << cWriteFlagJPEGQualityLevelShift) & cWriteFlagJPEGQualityLevelMask); }

      bool write_to_file(const char* pFilename, const image_u8& img, uint write_flags = 0, int grayscale_comp_index = cLumaComponentIndex);

      bool has_alpha(const image_u8& img);
      bool is_normal_map(const image_u8& img, const char* pFilename = NULL);
      void renorm_normal_map(image_u8& img);

      struct resample_params
      {
         resample_params() :
            m_dst_width(0),
            m_dst_height(0),
            m_pFilter("lanczos4"),
            m_filter_scale(1.0f),
            m_srgb(true),
            m_wrapping(false),
            m_first_comp(0),
            m_num_comps(4),
            m_source_gamma(2.2f), // 1.75f
            m_multithreaded(true)
         {
         }

         uint        m_dst_width;
         uint        m_dst_height;
         const char* m_pFilter;
         float       m_filter_scale;
         bool        m_srgb;
         bool        m_wrapping;
         uint        m_first_comp;
         uint        m_num_comps;
         float       m_source_gamma;
         bool        m_multithreaded;
      };

      bool resample_single_thread(const image_u8& src, image_u8& dst, const resample_params& params);
      bool resample_multithreaded(const image_u8& src, image_u8& dst, const resample_params& params);
      bool resample(const image_u8& src, image_u8& dst, const resample_params& params);

      bool compute_delta(image_u8& dest, image_u8& a, image_u8& b, uint scale = 2);

      class error_metrics
      {
      public:
         error_metrics() { utils::zero_this(this); }

         void print(const char* pName) const;

         // If num_channels==0, luma error is computed.
         // If pHist != NULL, it must point to a 256 entry array.
         bool compute(const image_u8& a, const image_u8& b, uint first_channel, uint num_channels, bool average_component_error = true);

         uint  mMax;
         double mMean;
         double mMeanSquared;
         double mRootMeanSquared;
         double mPeakSNR;

         inline bool operator== (const error_metrics& other) const
         {
            return mPeakSNR == other.mPeakSNR;
         }

         inline bool operator< (const error_metrics& other) const
         {
            return mPeakSNR < other.mPeakSNR;
         }

         inline bool operator> (const error_metrics& other) const
         {
            return mPeakSNR > other.mPeakSNR;
         }
      };

      void print_image_metrics(const image_u8& src_img, const image_u8& dst_img);

      double compute_block_ssim(uint n, const uint8* pX, const uint8* pY);
      double compute_ssim(const image_u8& a, const image_u8& b, int channel_index);
      void print_ssim(const image_u8& src_img, const image_u8& dst_img);

      enum conversion_type
      {
         cConversion_Invalid = -1,

         cConversion_To_CCxY,
         cConversion_From_CCxY,

         cConversion_To_xGxR,
         cConversion_From_xGxR,

         cConversion_To_xGBR,
         cConversion_From_xGBR,

         cConversion_To_AGBR,
         cConversion_From_AGBR,

         cConversion_XY_to_XYZ,

         cConversion_Y_To_A,

         cConversion_A_To_RGBA,
         cConversion_Y_To_RGB,

         cConversion_To_Y,

         cConversionTotal
      };

      void convert_image(image_u8& img, conversion_type conv_type);

      template<typename image_type>
      inline uint8* pack_image(const image_type& img, const pixel_packer& packer, uint& n)
      {
         n = 0;

         if (!packer.is_valid())
            return NULL;

         const uint width = img.get_width(), height = img.get_height();
         uint dst_pixel_stride = packer.get_pixel_stride();
         uint dst_pitch = width * dst_pixel_stride;

         n = dst_pitch * height;

         uint8* pImage = static_cast<uint8*>(crnlib_malloc(n));

         uint8* pDst = pImage;
         for (uint y = 0; y < height; y++)
         {
            const typename image_type::color_t* pSrc = img.get_scanline(y);
            for (uint x = 0; x < width; x++)
               pDst = (uint8*)packer.pack(*pSrc++, pDst);
         }

         return pImage;
      }

      image_utils::conversion_type get_conversion_type(bool cooking, pixel_format fmt);

      image_utils::conversion_type get_image_conversion_type_from_crn_format(crn_format fmt);

      double compute_std_dev(uint n, const color_quad_u8* pPixels, uint first_channel, uint num_channels);

      uint8* read_image_from_memory(const uint8* pImage, int nSize, int* pWidth, int* pHeight, int* pActualComps, int req_comps, const char* pFilename);

   } // namespace image_utils

} // namespace crnlib
