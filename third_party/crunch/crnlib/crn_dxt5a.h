// File: crn_dxt5a.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dxt.h"

namespace crnlib
{
   class dxt5_endpoint_optimizer
   {
   public:
      dxt5_endpoint_optimizer();

      struct params
      {
         params() :
            m_block_index(0),
            m_pPixels(NULL),
            m_num_pixels(0),
            m_comp_index(3),
            m_quality(cCRNDXTQualityUber),
            m_use_both_block_types(true)
         {
         }

         uint                 m_block_index;

         const color_quad_u8* m_pPixels;
         uint                 m_num_pixels;
         uint                 m_comp_index;

         crn_dxt_quality      m_quality;

         bool                 m_use_both_block_types;
      };

      struct results
      {
         uint8*   m_pSelectors;

         uint64   m_error;

         uint8    m_first_endpoint;
         uint8    m_second_endpoint;

         uint8    m_block_type; // 1 if 6-alpha, otherwise 8-alpha
      };

      bool compute(const params& p, results& r);

   private:
      const params*  m_pParams;
      results*       m_pResults;

      crnlib::vector<uint8> m_unique_values;
      crnlib::vector<uint> m_unique_value_weights;

      crnlib::vector<uint8> m_trial_selectors;
      crnlib::vector<uint8> m_best_selectors;
      int m_unique_value_map[256];

      sparse_bit_array m_flags;

      void evaluate_solution(uint low_endpoint, uint high_endpoint);
   };

} // namespace crnlib
