// File: crn_dxt_endpoint_refiner.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_dxt_endpoint_refiner.h"
#include "crn_dxt1.h"

namespace crnlib
{
   dxt_endpoint_refiner::dxt_endpoint_refiner() :
      m_pParams(NULL),
      m_pResults(NULL)
   {
   }

   bool dxt_endpoint_refiner::refine(const params& p, results& r)
   {
      if (!p.m_num_pixels)
         return false;

      m_pParams = &p;
      m_pResults = &r;

      r.m_error = cUINT64_MAX;
      r.m_low_color = 0;
      r.m_high_color = 0;

      double alpha2_sum = 0.0f;
      double beta2_sum = 0.0f;
      double alphabeta_sum = 0.0f;

      vec<3, double> alphax_sum( 0.0f );
      vec<3, double> betax_sum( 0.0f );

      vec<3, double> first_color( 0.0f );

      // This linear solver is from Squish.
      for( uint i = 0; i < p.m_num_pixels; ++i )
      {
         uint8 c = p.m_pSelectors[i];

         double k;
         if (p.m_dxt1_selectors)
            k = g_dxt1_to_linear[c] * 1.0f/3.0f;
         else
            k = g_dxt5_to_linear[c] * 1.0f/7.0f;

         double alpha = 1.0f - k;
         double beta = k;

         vec<3, double> x;

         if (p.m_dxt1_selectors)
            x.set( p.m_pPixels[i][0] * 1.0f/255.0f, p.m_pPixels[i][1] * 1.0f/255.0f, p.m_pPixels[i][2] * 1.0f/255.0f );
         else
            x.set( p.m_pPixels[i][p.m_alpha_comp_index]/255.0f );

         if (!i)
            first_color = x;

         alpha2_sum += alpha*alpha;
         beta2_sum += beta*beta;
         alphabeta_sum += alpha*beta;
         alphax_sum += alpha*x;
         betax_sum += beta*x;
      }

      // zero where non-determinate
      vec<3, double> a, b;
      if( beta2_sum == 0.0f )
      {
         a = alphax_sum / alpha2_sum;
         b.clear();
      }
      else if( alpha2_sum == 0.0f )
      {
         a.clear();
         b = betax_sum / beta2_sum;
      }
      else
      {
         double factor = alpha2_sum*beta2_sum - alphabeta_sum*alphabeta_sum;
         if (factor != 0.0f)
         {
            a = ( alphax_sum*beta2_sum - betax_sum*alphabeta_sum ) / factor;
            b = ( betax_sum*alpha2_sum - alphax_sum*alphabeta_sum ) / factor;
         }
         else
         {
            a = first_color;
            b = first_color;
         }
      }

      vec3F l(0.0f), h(0.0f);
      l = a;
      h = b;

      l.clamp(0.0f, 1.0f);
      h.clamp(0.0f, 1.0f);

      if (p.m_dxt1_selectors)
         optimize_dxt1(l, h);
      else
         optimize_dxt5(l, h);

      //if (r.m_low_color < r.m_high_color)
      //   utils::swap(r.m_low_color, r.m_high_color);

      return r.m_error < p.m_error_to_beat;
   }

   void dxt_endpoint_refiner::optimize_dxt5(vec3F low_color, vec3F high_color)
   {
      float nl = low_color[0];
      float nh = high_color[0];

#if CRNLIB_DXT_ALT_ROUNDING
      nl = math::clamp(nl, 0.0f, .999f);
      nh = math::clamp(nh, 0.0f, .999f);
      uint il = (int)floor(nl * 256.0f);
      uint ih = (int)floor(nh * 256.0f);
#else
      uint il = (int)floor(.5f + math::clamp(nl, 0.0f, 1.0f) * 255.0f);
      uint ih = (int)floor(.5f + math::clamp(nh, 0.0f, 1.0f) * 255.0f);
#endif

      crnlib::vector<uint> trial_solutions;
      trial_solutions.reserve(256);
      trial_solutions.push_back(il | (ih << 8));

      sparse_bit_array flags;
      flags.resize(256 * 256);

      flags.set_bit((il * 256) + ih);

      const int cProbeAmount = 11;

      for (int l_delta = -cProbeAmount; l_delta <= cProbeAmount; l_delta++)
      {
         const int l = il + l_delta;
         if (l < 0)
            continue;
         else if (l > 255)
            break;

         const uint bit_index = l * 256;

         for (int h_delta = -cProbeAmount; h_delta <= cProbeAmount; h_delta++)
         {
            const int h = ih + h_delta;
            if (h < 0)
               continue;
            else if (h > 255)
               break;

            if ((flags.get_bit(bit_index + h)) || (flags.get_bit(h * 256 + l)))
               continue;

            flags.set_bit(bit_index + h);

            trial_solutions.push_back(l | (h << 8));
         }
      }

      for (uint trial = 0; trial < trial_solutions.size(); trial++)
      {
         uint l = trial_solutions[trial] & 0xFF;
         uint h = trial_solutions[trial] >> 8;

         if (l == h)
         {
            if (h)
               h--;
            else
               l++;
         }
         else if (l < h)
         {
            utils::swap(l, h);
         }

         CRNLIB_ASSERT(l > h);

         uint values[cDXT5SelectorValues];
         dxt5_block::get_block_values8(values, l, h);

         uint total_error = 0;

         for (uint j = 0; j < m_pParams->m_num_pixels; j++)
         {
            int p = m_pParams->m_pPixels[j][m_pParams->m_alpha_comp_index];
            int c = values[m_pParams->m_pSelectors[j]];

            int error = p - c;
            error *= error;

            total_error += error;

            if (total_error > m_pResults->m_error)
               break;
         }

         if (total_error < m_pResults->m_error)
         {
            m_pResults->m_error = total_error;
            m_pResults->m_low_color = static_cast<uint16>(l);
            m_pResults->m_high_color = static_cast<uint16>(h);

            if (m_pResults->m_error == 0)
               return;
         }
      }
   }

   void dxt_endpoint_refiner::optimize_dxt1(vec3F low_color, vec3F high_color)
   {
      uint selector_hist[4];
      utils::zero_object(selector_hist);
      for (uint i = 0; i < m_pParams->m_num_pixels; i++)
         selector_hist[m_pParams->m_pSelectors[i]]++;

      dxt1_solution_coordinates c(low_color, high_color);

      for (uint pass = 0; pass < 8; pass++)
      {
         const uint64 initial_error = m_pResults->m_error;

         dxt1_solution_coordinates_vec coords_to_try;

         coords_to_try.resize(0);

         color_quad_u8 lc(dxt1_block::unpack_color(c.m_low_color, false));
         color_quad_u8 hc(dxt1_block::unpack_color(c.m_high_color, false));

         for (int i = 0; i < 27; i++)
         {
            if (13 == i) continue;

            const int ir = (i % 3) - 1;
            const int ig = ((i / 3) % 3) - 1;
            const int ib = ((i / 9) % 3) - 1;

            int r = lc.r + ir;
            int g = lc.g + ig;
            int b = lc.b + ib;
            if ((r < 0) || (r > 31)|| (g < 0) || (g > 63) || (b < 0) || (b > 31)) continue;

            coords_to_try.push_back(
               dxt1_solution_coordinates(dxt1_block::pack_color(r, g, b, false), c.m_high_color)
               );
         }

         for (int i = 0; i < 27; i++)
         {
            if (13 == i) continue;

            const int ir = (i % 3) - 1;
            const int ig = ((i / 3) % 3) - 1;
            const int ib = ((i / 9) % 3) - 1;

            int r = hc.r + ir;
            int g = hc.g + ig;
            int b = hc.b + ib;
            if ((r < 0) || (r > 31)|| (g < 0) || (g > 63) || (b < 0) || (b > 31)) continue;

            coords_to_try.push_back(dxt1_solution_coordinates(c.m_low_color, dxt1_block::pack_color(r, g, b, false)));
         }

         std::sort(coords_to_try.begin(), coords_to_try.end());

         dxt1_solution_coordinates_vec::const_iterator p_last = std::unique(coords_to_try.begin(), coords_to_try.end());
         uint num_coords_to_try = (uint)(p_last - coords_to_try.begin());

         for (uint i = 0; i < num_coords_to_try; i++)
         {
            color_quad_u8 block_colors[4];
            uint16 l = coords_to_try[i].m_low_color;
            uint16 h = coords_to_try[i].m_high_color;
            if (l < h)
               utils::swap(l, h);
            else if (l == h)
            {
               color_quad_u8 lc(dxt1_block::unpack_color(l, false));
               color_quad_u8 hc(dxt1_block::unpack_color(h, false));

               bool retry = false;
               if ((selector_hist[0] + selector_hist[2]) > (selector_hist[1] + selector_hist[3]))
               {
                  // l affects the output more than h, so muck with h
                  if (hc[2] != 0)
                     hc[2]--;
                  else if (hc[0] != 0)
                     hc[0]--;
                  else if (hc[1] != 0)
                     hc[1]--;
                  else
                     retry = true;
               }
               else
               {
                  // h affects the output more than l, so muck with l
                  if (lc[2] != 31)
                     lc[2]++;
                  else if (lc[0] != 31)
                     lc[0]++;
                  else if (lc[1] != 63)
                     lc[1]++;
                  else
                     retry = true;
               }

               if (retry)
               {
                  if (l == 0)
                     l++;
                  else
                     h--;
               }
               else
               {
                  l = dxt1_block::pack_color(lc, false);
                  h = dxt1_block::pack_color(hc, false);
               }

               CRNLIB_ASSERT(l > h);
            }

            dxt1_block::get_block_colors4(block_colors, l, h);

            uint total_error = 0;

            for (uint j = 0; j < m_pParams->m_num_pixels; j++)
            {
               const color_quad_u8& c = block_colors[m_pParams->m_pSelectors[j]];
               total_error += color::color_distance(m_pParams->m_perceptual, c, m_pParams->m_pPixels[j], false);

               if (total_error > m_pResults->m_error)
                  break;
            }

            if (total_error < m_pResults->m_error)
            {
               m_pResults->m_error = total_error;
               m_pResults->m_low_color = l;
               m_pResults->m_high_color = h;
               CRNLIB_ASSERT(l > h);
               if (m_pResults->m_error == 0)
                  return;
            }
         }

         if (m_pResults->m_error == initial_error)
            break;

         c.m_low_color = m_pResults->m_low_color;
         c.m_high_color = m_pResults->m_high_color;
      }

   }

} // namespace crnlib

