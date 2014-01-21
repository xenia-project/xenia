// File: crn_dxt_endpoint_refiner.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt.h"

namespace crnlib
{
   // TODO: Experimental/Not fully implemented
   class dxt_endpoint_refiner
   {
   public:
      dxt_endpoint_refiner();

      struct params
      {
         params() :
            m_block_index(0),
            m_pPixels(NULL),
            m_num_pixels(0),
            m_pSelectors(NULL),
            m_alpha_comp_index(0),
            m_error_to_beat(cUINT64_MAX),
            m_dxt1_selectors(true),
            m_perceptual(true),
            m_highest_quality(true)
         {
         }

         uint                 m_block_index;

         const color_quad_u8* m_pPixels;
         uint                 m_num_pixels;

         const uint8*         m_pSelectors;

         uint                 m_alpha_comp_index;

         uint64               m_error_to_beat;

         bool                 m_dxt1_selectors;
         bool                 m_perceptual;
         bool                 m_highest_quality;
      };

      struct results
      {
         uint16   m_low_color;
         uint16   m_high_color;
         uint64   m_error;
      };

      bool refine(const params& p, results& r);

   private:
      const params* m_pParams;
      results* m_pResults;

      void optimize_dxt1(vec3F low_color, vec3F high_color);
      void optimize_dxt5(vec3F low_color, vec3F high_color);
   };

} // namespace crnlib
