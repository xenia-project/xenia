// File: crn_dxt5a.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_dxt5a.h"
#include "crn_ryg_dxt.hpp"
#include "crn_dxt_fast.h"
#include "crn_intersect.h"

namespace crnlib
{
   dxt5_endpoint_optimizer::dxt5_endpoint_optimizer() :  
      m_pParams(NULL),
      m_pResults(NULL)
   {
      m_unique_values.reserve(16);
      m_unique_value_weights.reserve(16);
   }

   bool dxt5_endpoint_optimizer::compute(const params& p, results& r)
   {
      m_pParams = &p;
      m_pResults = &r;

      if ((!p.m_num_pixels) || (!p.m_pPixels))
         return false;

      m_unique_values.resize(0);
      m_unique_value_weights.resize(0);

      for (uint i = 0; i < 256; i++)
         m_unique_value_map[i] = -1;

      for (uint i = 0; i < p.m_num_pixels; i++)
      {
         uint alpha = p.m_pPixels[i][p.m_comp_index];

         int index = m_unique_value_map[alpha];

         if (index == -1)
         {
            index = m_unique_values.size();

            m_unique_value_map[alpha] = index;

            m_unique_values.push_back(static_cast<uint8>(alpha));
            m_unique_value_weights.push_back(0);
         }

         m_unique_value_weights[index]++;
      }         

      if (m_unique_values.size() == 1)
      {
         r.m_block_type = 0;
         r.m_error = 0;
         r.m_first_endpoint = m_unique_values[0];
         r.m_second_endpoint = m_unique_values[0];
         memset(r.m_pSelectors, 0, p.m_num_pixels);
         return true;
      }

      m_trial_selectors.resize(m_unique_values.size());
      m_best_selectors.resize(m_unique_values.size());

      r.m_error = cUINT64_MAX;

      for (uint i = 0; i < m_unique_values.size() - 1; i++)
      {
         const uint low_endpoint = m_unique_values[i];

         for (uint j = i + 1; j < m_unique_values.size(); j++)
         {
            const uint high_endpoint = m_unique_values[j];

            evaluate_solution(low_endpoint, high_endpoint);
         }
      }

      if ((m_pParams->m_quality >= cCRNDXTQualityBetter) && (m_pResults->m_error))
      {
         m_flags.resize(256 * 256);
         m_flags.clear_all_bits();

         const int cProbeAmount = (m_pParams->m_quality == cCRNDXTQualityUber) ? 16 : 8;

         for (int l_delta = -cProbeAmount; l_delta <= cProbeAmount; l_delta++)
         {
            const int l = m_pResults->m_first_endpoint + l_delta;
            if (l < 0)
               continue;
            else if (l > 255)
               break;

            const uint bit_index = l * 256;

            for (int h_delta = -cProbeAmount; h_delta <= cProbeAmount; h_delta++)
            {
               const int h = m_pResults->m_second_endpoint + h_delta;
               if (h < 0)
                  continue;
               else if (h > 255)
                  break;

               //if (m_flags.get_bit(bit_index + h))
               //   continue;
               if ((m_flags.get_bit(bit_index + h)) || (m_flags.get_bit(h * 256 + l)))
                  continue;
               m_flags.set_bit(bit_index + h);

               evaluate_solution(static_cast<uint>(l), static_cast<uint>(h));
            }
         }
      }

      if (m_pResults->m_first_endpoint == m_pResults->m_second_endpoint)
      {
         for (uint i = 0; i < m_best_selectors.size(); i++)
            m_best_selectors[i] = 0;
      }      
      else if (m_pResults->m_block_type)
      {
         //if (l > h) 
         //   eight alpha
         // else 
         //   six alpha

         if (m_pResults->m_first_endpoint > m_pResults->m_second_endpoint)
         {
            utils::swap(m_pResults->m_first_endpoint, m_pResults->m_second_endpoint);
            for (uint i = 0; i < m_best_selectors.size(); i++)
               m_best_selectors[i] = g_six_alpha_invert_table[m_best_selectors[i]];
         }
      }
      else if (!(m_pResults->m_first_endpoint > m_pResults->m_second_endpoint))
      {
         utils::swap(m_pResults->m_first_endpoint, m_pResults->m_second_endpoint);
         for (uint i = 0; i < m_best_selectors.size(); i++)
            m_best_selectors[i] = g_eight_alpha_invert_table[m_best_selectors[i]];
      }

      for (uint i = 0; i < m_pParams->m_num_pixels; i++)
      {
         uint alpha = m_pParams->m_pPixels[i][m_pParams->m_comp_index];

         int index = m_unique_value_map[alpha];

         m_pResults->m_pSelectors[i] = m_best_selectors[index];
      }

      return true;
   }

   void dxt5_endpoint_optimizer::evaluate_solution(uint low_endpoint, uint high_endpoint)
   {
      for (uint block_type = 0; block_type < (m_pParams->m_use_both_block_types ? 2U : 1U); block_type++)
      {
         uint selector_values[8];      

         if (!block_type)
            dxt5_block::get_block_values8(selector_values, low_endpoint, high_endpoint);
         else
            dxt5_block::get_block_values6(selector_values, low_endpoint, high_endpoint);

         uint64 trial_error = 0;

         for (uint i = 0; i < m_unique_values.size(); i++)
         {
            const uint val = m_unique_values[i];
            const uint weight = m_unique_value_weights[i];

            uint best_selector_error = UINT_MAX;
            uint best_selector = 0;

            for (uint j = 0; j < 8; j++)
            {
               int selector_error = val - selector_values[j];
               selector_error = selector_error * selector_error * (int)weight;

               if (static_cast<uint>(selector_error) < best_selector_error)
               {
                  best_selector_error = selector_error;
                  best_selector = j;
                  if (!best_selector_error)
                     break;
               }
            }

            m_trial_selectors[i] = static_cast<uint8>(best_selector);
            trial_error += best_selector_error;

            if (trial_error > m_pResults->m_error)
               break;
         }

         if (trial_error < m_pResults->m_error)
         {
            m_pResults->m_error = trial_error;
            m_pResults->m_first_endpoint = static_cast<uint8>(low_endpoint);
            m_pResults->m_second_endpoint = static_cast<uint8>(high_endpoint);
            m_pResults->m_block_type = static_cast<uint8>(block_type);
            m_best_selectors.swap(m_trial_selectors);

            if (!trial_error)
               break;
         }
      }
   }

} // namespace crnlib
