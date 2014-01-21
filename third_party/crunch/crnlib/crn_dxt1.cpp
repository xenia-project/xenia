// File: crn_dxt1.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
//
// Notes:
// This class is not optimized for performance on small blocks, unlike typical DXT1 compressors. It's optimized for scalability and quality:
// - Very high quality in terms of avg. RMSE or Luma RMSE. Goal is to always match or beat every other known offline DXTc compressor: ATI_Compress, squish, NVidia texture tools, nvdxt.exe, etc.
// - Reasonable scalability and stability with hundreds to many thousands of input colors (including inputs with many thousands of equal/nearly equal colors).
// - Any quality optimization which results in even a tiny improvement is worth it -- as long as it's either a constant or linear slowdown.
//   Tiny quality improvements can be extremely valuable in large clusters.
// - Quality should scale well vs. CPU time cost, i.e. the more time you spend the higher the quality.
#include "crn_core.h"
#include "crn_dxt1.h"
#include "crn_ryg_dxt.hpp"
#include "crn_dxt_fast.h"
#include "crn_intersect.h"
#include "crn_vec_interval.h"

namespace crnlib
{
   //-----------------------------------------------------------------------------------------------------------------------------------------

   static const int16 g_fast_probe_table[] = { 0, 1, 2, 3 };
   static const uint cFastProbeTableSize = sizeof(g_fast_probe_table) / sizeof(g_fast_probe_table[0]);

   static const int16 g_normal_probe_table[] = { 0, 1, 3, 5, 7 };
   static const uint cNormalProbeTableSize = sizeof(g_normal_probe_table) / sizeof(g_normal_probe_table[0]);

   static const int16 g_better_probe_table[] = { 0, 1, 2, 3, 5, 9, 15, 19, 27, 43 };
   static const uint cBetterProbeTableSize = sizeof(g_better_probe_table) / sizeof(g_better_probe_table[0]);

   static const int16 g_uber_probe_table[] = { 0, 1, 2, 3, 5, 7, 9, 10, 13, 15, 19, 27, 43, 59, 91 };
   static const uint cUberProbeTableSize = sizeof(g_uber_probe_table) / sizeof(g_uber_probe_table[0]);

   //-----------------------------------------------------------------------------------------------------------------------------------------

   dxt1_endpoint_optimizer::dxt1_endpoint_optimizer() :
      m_pParams(NULL),
      m_pResults(NULL),
      m_pSolutions(NULL),
      m_perceptual(false),
      m_has_color_weighting(false),
      m_all_pixels_grayscale(false)
   {
      m_low_coords.reserve(512);
      m_high_coords.reserve(512);

      m_unique_colors.reserve(512);
      m_temp_unique_colors.reserve(512);
      m_unique_packed_colors.reserve(512);

      m_norm_unique_colors.reserve(512);
      m_norm_unique_colors_weighted.reserve(512);

      m_lo_cells.reserve(128);
      m_hi_cells.reserve(128);
   }

   void dxt1_endpoint_optimizer::clear()
   {
      m_pParams = NULL;
      m_pResults = NULL;
      m_pSolutions = NULL;

      if (m_unique_color_hash_map.get_table_size() > 8192)
         m_unique_color_hash_map.clear();
      else
         m_unique_color_hash_map.reset();

      if (m_solutions_tried.get_table_size() > 8192)
         m_solutions_tried.clear();

      m_unique_colors.resize(0);

      m_has_transparent_pixels = false;
      m_total_unique_color_weight = 0;

      m_norm_unique_colors.resize(0);
      m_mean_norm_color.clear();

      m_norm_unique_colors_weighted.resize(0);
      m_mean_norm_color_weighted.clear();

      m_principle_axis.clear();

      m_total_evals = 0;
      m_all_pixels_grayscale = false;
      m_has_color_weighting = false;
      m_perceptual = false;
   }

   bool dxt1_endpoint_optimizer::handle_all_transparent_block()
   {
      m_pResults->m_low_color = 0;
      m_pResults->m_high_color = 0;
      m_pResults->m_alpha_block = true;

      memset(m_pResults->m_pSelectors, 3, m_pParams->m_num_pixels);

      return true;
   }

   // All selectors are equal. Try compressing as if it was solid, using the block's average color, using ryg's optimal single color compression tables.
   bool dxt1_endpoint_optimizer::try_average_block_as_solid()
   {
      uint64 tot_r = 0;
      uint64 tot_g = 0;
      uint64 tot_b = 0;

      uint total_weight = 0;
      for (uint i = 0; i < m_unique_colors.size(); i++)
      {
         uint weight = m_unique_colors[i].m_weight;
         total_weight += weight;

         tot_r += m_unique_colors[i].m_color.r * weight;
         tot_g += m_unique_colors[i].m_color.g * weight;
         tot_b += m_unique_colors[i].m_color.b * weight;
      }

      const uint half_total_weight = total_weight >> 1;
      uint ave_r = static_cast<uint>((tot_r + half_total_weight) / total_weight);
      uint ave_g = static_cast<uint>((tot_g + half_total_weight) / total_weight);
      uint ave_b = static_cast<uint>((tot_b + half_total_weight) / total_weight);

      uint low_color = (ryg_dxt::OMatch5[ave_r][0]<<11) | (ryg_dxt::OMatch6[ave_g][0]<<5) | ryg_dxt::OMatch5[ave_b][0];
      uint high_color = (ryg_dxt::OMatch5[ave_r][1]<<11) | (ryg_dxt::OMatch6[ave_g][1]<<5) | ryg_dxt::OMatch5[ave_b][1];
      bool improved = evaluate_solution(dxt1_solution_coordinates((uint16)low_color, (uint16)high_color), true, &m_best_solution);

      if ((m_pParams->m_use_alpha_blocks) && (m_best_solution.m_error))
      {
         low_color = (ryg_dxt::OMatch5_3[ave_r][0]<<11) | (ryg_dxt::OMatch6_3[ave_g][0]<<5) | ryg_dxt::OMatch5_3[ave_b][0];
         high_color = (ryg_dxt::OMatch5_3[ave_r][1]<<11) | (ryg_dxt::OMatch6_3[ave_g][1]<<5) | ryg_dxt::OMatch5_3[ave_b][1];
         improved |= evaluate_solution(dxt1_solution_coordinates((uint16)low_color, (uint16)high_color), true, &m_best_solution);
      }

      if (m_pParams->m_quality == cCRNDXTQualityUber)
      {
         // Try compressing as all-solid using the other (non-average) colors in the block in uber.
         for (uint i = 0; i < m_unique_colors.size(); i++)
         {
            uint r = m_unique_colors[i].m_color[0];
            uint g = m_unique_colors[i].m_color[1];
            uint b = m_unique_colors[i].m_color[2];
            if ((r == ave_r) && (g == ave_g) && (b == ave_b))
               continue;

            uint low_color = (ryg_dxt::OMatch5[r][0]<<11) | (ryg_dxt::OMatch6[g][0]<<5) | ryg_dxt::OMatch5[b][0];
            uint high_color = (ryg_dxt::OMatch5[r][1]<<11) | (ryg_dxt::OMatch6[g][1]<<5) | ryg_dxt::OMatch5[b][1];
            improved |= evaluate_solution(dxt1_solution_coordinates((uint16)low_color, (uint16)high_color), true, &m_best_solution);

            if ((m_pParams->m_use_alpha_blocks) && (m_best_solution.m_error))
            {
               low_color = (ryg_dxt::OMatch5_3[r][0]<<11) | (ryg_dxt::OMatch6_3[g][0]<<5) | ryg_dxt::OMatch5_3[b][0];
               high_color = (ryg_dxt::OMatch5_3[r][1]<<11) | (ryg_dxt::OMatch6_3[g][1]<<5) | ryg_dxt::OMatch5_3[b][1];
               improved |= evaluate_solution(dxt1_solution_coordinates((uint16)low_color, (uint16)high_color), true, &m_best_solution);
            }
         }
      }

      return improved;
   }

   // Block is solid, trying using ryg's optimal single color tables.
   bool dxt1_endpoint_optimizer::handle_solid_block()
   {
      int r = m_unique_colors[0].m_color.r;
      int g = m_unique_colors[0].m_color.g;
      int b = m_unique_colors[0].m_color.b;

      //uint packed_color = dxt1_block::pack_color(r, g, b, true);
      //evaluate_solution(dxt1_solution_coordinates((uint16)packed_color, (uint16)packed_color), false, &m_best_solution);

      uint low_color = (ryg_dxt::OMatch5[r][0]<<11) | (ryg_dxt::OMatch6[g][0]<<5) | ryg_dxt::OMatch5[b][0];
      uint high_color = (ryg_dxt::OMatch5[r][1]<<11) | (ryg_dxt::OMatch6[g][1]<<5) | ryg_dxt::OMatch5[b][1];
      evaluate_solution(dxt1_solution_coordinates((uint16)low_color, (uint16)high_color), false, &m_best_solution);

      if ((m_pParams->m_use_alpha_blocks) && (m_best_solution.m_error))
      {
         low_color = (ryg_dxt::OMatch5_3[r][0]<<11) | (ryg_dxt::OMatch6_3[g][0]<<5) | ryg_dxt::OMatch5_3[b][0];
         high_color = (ryg_dxt::OMatch5_3[r][1]<<11) | (ryg_dxt::OMatch6_3[g][1]<<5) | ryg_dxt::OMatch5_3[b][1];
         evaluate_solution(dxt1_solution_coordinates((uint16)low_color, (uint16)high_color), true, &m_best_solution);
      }

      return_solution(*m_pResults, m_best_solution);

      return true;
   }

   void dxt1_endpoint_optimizer::compute_vectors(const vec3F& perceptual_weights)
   {
      m_norm_unique_colors.resize(0);
      m_norm_unique_colors_weighted.resize(0);

      m_mean_norm_color.clear();
      m_mean_norm_color_weighted.clear();

      for (uint i = 0; i < m_unique_colors.size(); i++)
      {
         const color_quad_u8& color = m_unique_colors[i].m_color;
         const uint weight = m_unique_colors[i].m_weight;

         vec3F norm_color(color.r * 1.0f/255.0f, color.g * 1.0f/255.0f, color.b * 1.0f/255.0f);
         vec3F norm_color_weighted(vec3F::mul_components(perceptual_weights, norm_color));

         m_norm_unique_colors.push_back(norm_color);
         m_norm_unique_colors_weighted.push_back(norm_color_weighted);

         m_mean_norm_color += norm_color * (float)weight;
         m_mean_norm_color_weighted += norm_color_weighted * (float)weight;
      }

      if (m_total_unique_color_weight)
      {
         m_mean_norm_color *= (1.0f / m_total_unique_color_weight);
         m_mean_norm_color_weighted *= (1.0f / m_total_unique_color_weight);
      }

      for (uint i = 0; i < m_unique_colors.size(); i++)
      {
         m_norm_unique_colors[i] -= m_mean_norm_color;
         m_norm_unique_colors_weighted[i] -= m_mean_norm_color_weighted;
      }
   }

   // Compute PCA (principle axis, i.e. direction of largest variance) of input vectors.
   void dxt1_endpoint_optimizer::compute_pca(vec3F& axis, const vec3F_array& norm_colors, const vec3F& def)
   {
#if 0
      axis.clear();

      CRNLIB_ASSERT(m_unique_colors.size() == norm_colors.size());

      // Incremental PCA
      bool first = true;
      for (uint i = 0; i < norm_colors.size(); i++)
      {
         const uint weight = m_unique_colors[i].m_weight;

         for (uint j = 0; j < weight; j++)
         {
            vec3F x(norm_colors[i] * norm_colors[i][0]);
            vec3F y(norm_colors[i] * norm_colors[i][1]);
            vec3F z(norm_colors[i] * norm_colors[i][2]);

            vec3F v(first ? norm_colors[0] : axis);
            first = false;

            v.normalize(&def);

            axis[0] += (x * v);
            axis[1] += (y * v);
            axis[2] += (z * v);
         }
      }

      axis.normalize(&def);
#else
      double cov[6] = { 0, 0, 0, 0, 0, 0 };

      //vec3F lo(math::cNearlyInfinite);
      //vec3F hi(-math::cNearlyInfinite);

      for(uint i = 0; i < norm_colors.size(); i++)
      {
         const vec3F& v = norm_colors[i];

         //if (v[0] < lo[0]) lo[0] = v[0];
         //if (v[1] < lo[1]) lo[1] = v[1];
         //if (v[2] < lo[2]) lo[2] = v[2];
         //if (v[0] > hi[0]) hi[0] = v[0];
         //if (v[1] > hi[1]) hi[1] = v[1];
         //if (v[2] > hi[2]) hi[2] = v[2];

         float r = v[0];
         float g = v[1];
         float b = v[2];

         if (m_unique_colors[i].m_weight > 1)
         {
            const double weight = m_unique_colors[i].m_weight;

            cov[0] += r*r*weight;
            cov[1] += r*g*weight;
            cov[2] += r*b*weight;
            cov[3] += g*g*weight;
            cov[4] += g*b*weight;
            cov[5] += b*b*weight;
         }
         else
         {
            cov[0] += r*r;
            cov[1] += r*g;
            cov[2] += r*b;
            cov[3] += g*g;
            cov[4] += g*b;
            cov[5] += b*b;
         }
      }

      double vfr, vfg, vfb;
      //vfr = hi[0] - lo[0];
      //vfg = hi[1] - lo[1];
      //vfb = hi[2] - lo[2];
      // This is more stable.
      vfr = .9f;
      vfg = 1.0f;
      vfb = .7f;

      const uint cNumIters = 8;

      for (uint iter = 0; iter < cNumIters; iter++)
      {
         double r = vfr*cov[0] + vfg*cov[1] + vfb*cov[2];
         double g = vfr*cov[1] + vfg*cov[3] + vfb*cov[4];
         double b = vfr*cov[2] + vfg*cov[4] + vfb*cov[5];

         double m = math::maximum(fabs(r), fabs(g), fabs(b));
         if (m > 1e-10)
         {
            m = 1.0f / m;
            r *= m;
            g *= m;
            b *= m;
         }

         double delta = math::square(vfr-r) + math::square(vfg-g) + math::square(vfb-b);

         vfr = r;
         vfg = g;
         vfb = b;

         if ((iter > 2) && (delta < 1e-8))
            break;
      }

      double len = vfr*vfr + vfg*vfg + vfb*vfb;

      if (len < 1e-10)
      {
         axis = def;
      }
      else
      {
         len = 1.0f / sqrt(len);
         vfr *= len;
         vfg *= len;
         vfb *= len;

         axis.set(static_cast<float>(vfr), static_cast<float>(vfg), static_cast<float>(vfb));
      }
#endif
   }

   static const uint8 g_invTableNull[4] = { 0, 1, 2, 3 };
   static const uint8 g_invTableAlpha[4] = { 1, 0, 2, 3 };
   static const uint8 g_invTableColor[4] = { 1, 0, 3, 2 };

   // Computes a valid (encodable) DXT1 solution (low/high colors, swizzled selectors) from input.
   void dxt1_endpoint_optimizer::return_solution(results& res, const potential_solution& solution)
   {
      bool invert_selectors;

      if (solution.m_alpha_block)
         invert_selectors = (solution.m_coords.m_low_color > solution.m_coords.m_high_color);
      else
      {
         CRNLIB_ASSERT(solution.m_coords.m_low_color != solution.m_coords.m_high_color);

         invert_selectors = (solution.m_coords.m_low_color < solution.m_coords.m_high_color);
      }

      if (invert_selectors)
      {
         res.m_low_color = solution.m_coords.m_high_color;
         res.m_high_color = solution.m_coords.m_low_color;
      }
      else
      {
         res.m_low_color = solution.m_coords.m_low_color;
         res.m_high_color = solution.m_coords.m_high_color;
      }

      const uint8* pInvert_table = g_invTableNull;
      if (invert_selectors)
         pInvert_table = solution.m_alpha_block ? g_invTableAlpha : g_invTableColor;

      const uint alpha_thresh = m_pParams->m_pixels_have_alpha ? (m_pParams->m_dxt1a_alpha_threshold << 24U) : 0;

      const uint32* pSrc_pixels = reinterpret_cast<const uint32*>(m_pParams->m_pPixels);
      uint8* pDst_selectors = res.m_pSelectors;

      if ((m_unique_colors.size() == 1) && (!m_pParams->m_pixels_have_alpha))
      {
         uint32 c = utils::read_le32(pSrc_pixels);

         CRNLIB_ASSERT(c >= alpha_thresh);

         c |= 0xFF000000U;

         unique_color_hash_map::const_iterator it(m_unique_color_hash_map.find(c));
         CRNLIB_ASSERT(it != m_unique_color_hash_map.end());

         uint unique_color_index = it->second;

         uint selector = pInvert_table[solution.m_selectors[unique_color_index]];

         memset(pDst_selectors, selector, m_pParams->m_num_pixels);
      }
      else
      {
         uint8* pDst_selectors_end = pDst_selectors + m_pParams->m_num_pixels;

         uint8 prev_selector = 0;
         uint32 prev_color = 0;

         do
         {
            uint32 c = utils::read_le32(pSrc_pixels);
            pSrc_pixels++;

            uint8 selector = 3;

            if (c >= alpha_thresh)
            {
               c |= 0xFF000000U;

               if (c == prev_color)
                  selector = prev_selector;
               else
               {
                  unique_color_hash_map::const_iterator it(m_unique_color_hash_map.find(c));

                  CRNLIB_ASSERT(it != m_unique_color_hash_map.end());

                  uint unique_color_index = it->second;

                  selector = pInvert_table[solution.m_selectors[unique_color_index]];

                  prev_color = c;
                  prev_selector = selector;
               }
            }

            *pDst_selectors++ = selector;

         } while (pDst_selectors != pDst_selectors_end);
      }

      res.m_alpha_block = solution.m_alpha_block;
      res.m_error = solution.m_error;
   }

   inline vec3F dxt1_endpoint_optimizer::unpack_to_vec3F(uint16 packed_color)
   {
      color_quad_u8 c(dxt1_block::unpack_color(packed_color, false));

      return vec3F(c.r * 1.0f/31.0f, c.g * 1.0f/63.0f, c.b * 1.0f/31.0f);
   }

   inline vec3F dxt1_endpoint_optimizer::unpack_to_vec3F_raw(uint16 packed_color)
   {
      color_quad_u8 c(dxt1_block::unpack_color(packed_color, false));

      return vec3F(c.r, c.g, c.b);
   }

   // Per-component 1D endpoint optimization.
   void dxt1_endpoint_optimizer::optimize_endpoint_comps()
   {
      if ((m_best_solution.m_alpha_block) || (!m_best_solution.m_error))
         return;

      //color_quad_u8 orig_l(dxt1_block::unpack_color(m_best_solution.m_coords.m_low_color, false));
      //color_quad_u8 orig_h(dxt1_block::unpack_color(m_best_solution.m_coords.m_high_color, false));
      //uint orig_error = m_best_solution.m_error;

      color_quad_u8 orig_l_scaled(dxt1_block::unpack_color(m_best_solution.m_coords.m_low_color, true));
      color_quad_u8 orig_h_scaled(dxt1_block::unpack_color(m_best_solution.m_coords.m_high_color, true));

      color_quad_u8 min_color(0xFF, 0xFF, 0xFF, 0xFF);
      color_quad_u8 max_color(0, 0, 0, 0);
      for (uint i = 0; i < m_unique_colors.size(); i++)
      {
         min_color = color_quad_u8::component_min(min_color, m_unique_colors[i].m_color);
         max_color = color_quad_u8::component_max(max_color, m_unique_colors[i].m_color);
      }

      // Try to separately optimize each component. This is a 1D problem so it's easy to compute accurate per-component error bounds.
      for (uint comp_index = 0; comp_index < 3; comp_index++)
      {
         uint ll[4];
         ll[0] = orig_l_scaled[comp_index];
         ll[1] = orig_h_scaled[comp_index];
         ll[2] = (ll[0]*2+ll[1])/3;
         ll[3] = (ll[0]+ll[1]*2)/3;

         uint error_to_beat = 0;
         uint min_color_weight = 0;
         uint max_color_weight = 0;
         for (uint i = 0; i < m_unique_colors.size(); i++)
         {
            uint c = m_unique_colors[i].m_color[comp_index];
            uint w = m_unique_colors[i].m_weight;

            int delta = ll[m_best_solution.m_selectors[i]] - c;
            error_to_beat += (int)w * (delta * delta);

            if (c == min_color[comp_index])
               min_color_weight += w;
            if (c == max_color[comp_index])
               max_color_weight += w;
         }

         if (!error_to_beat)
            continue;

         CRNLIB_ASSERT((min_color_weight > 0) && (max_color_weight > 0));
         const uint error_to_beat_div_min_color_weight = min_color_weight ? ((error_to_beat + min_color_weight - 1) / min_color_weight) : 0;
         const uint error_to_beat_div_max_color_weight = max_color_weight ? ((error_to_beat + max_color_weight - 1) / max_color_weight) : 0;

         const uint m = (comp_index == 1) ? 63 : 31;
         const uint m_shift = (comp_index == 1) ? 3 : 2;

         for (uint o = 0; o <= m; o++)
         {
            uint tl[4];

            tl[0] = (comp_index == 1) ? ((o << 2) | (o >> 4)) : ((o << 3) | (o >> 2));

            for (uint h = 0; h < 8; h++)
            {
               const uint pl = h << m_shift;
               const uint ph = ((h + 1) << m_shift) - 1;

               uint tl_l = (comp_index == 1) ? ((pl << 2) | (pl >> 4)) : ((pl << 3) | (pl >> 2));
               uint tl_h = (comp_index == 1) ? ((ph << 2) | (ph >> 4)) : ((ph << 3) | (ph >> 2));

               tl_l = math::minimum(tl_l, tl[0]);
               tl_h = math::maximum(tl_h, tl[0]);

               uint c_l = min_color[comp_index];
               uint c_h = max_color[comp_index];

               if (c_h < tl_l)
               {
                  uint min_possible_error = math::square<int>(tl_l - c_l);
                  if (min_possible_error > error_to_beat_div_min_color_weight)
                     continue;
               }
               else if (c_l > tl_h)
               {
                  uint min_possible_error = math::square<int>(c_h - tl_h);
                  if (min_possible_error > error_to_beat_div_max_color_weight)
                     continue;
               }

               for (uint p = pl; p <= ph; p++)
               {
                  tl[1] = (comp_index == 1) ? ((p << 2) | (p >> 4)) : ((p << 3) | (p >> 2));

                  tl[2] = (tl[0]*2+tl[1])/3;
                  tl[3] = (tl[0]+tl[1]*2)/3;

                  uint trial_error = 0;
                  for (uint i = 0; i < m_unique_colors.size(); i++)
                  {
                     int delta = tl[m_best_solution.m_selectors[i]] - m_unique_colors[i].m_color[comp_index];
                     trial_error += m_unique_colors[i].m_weight * (delta * delta);
                     if (trial_error >= error_to_beat)
                        break;
                  }

                  //CRNLIB_ASSERT(trial_error >= min_possible_error);

                  if (trial_error < error_to_beat)
                  {
                     color_quad_u8 l(dxt1_block::unpack_color(m_best_solution.m_coords.m_low_color, false));
                     color_quad_u8 h(dxt1_block::unpack_color(m_best_solution.m_coords.m_high_color, false));
                     l[comp_index] = static_cast<uint8>(o);
                     h[comp_index] = static_cast<uint8>(p);

                     bool better = evaluate_solution(
                        dxt1_solution_coordinates(dxt1_block::pack_color(l, false), dxt1_block::pack_color(h, false)),
                        true, &m_best_solution);
                     better;

                     if (better)
                     {
#if 0
                        printf("comp: %u, orig: %u %u, new: %u %u, orig_error: %u, new_error: %u\n", comp_index,
                           orig_l[comp_index], orig_h[comp_index],
                           l[comp_index], h[comp_index],
                           orig_error, m_best_solution.m_error);
#endif
                        if (!m_best_solution.m_error)
                           return;

                        error_to_beat = 0;
                        for (uint i = 0; i < m_unique_colors.size(); i++)
                        {
                           int delta = tl[m_best_solution.m_selectors[i]] - m_unique_colors[i].m_color[comp_index];
                           error_to_beat += m_unique_colors[i].m_weight * (delta * delta);
                        }

                     } // better

                     //goto early_out;
                  } // if (trial_error < error_to_beat)

               } // for (uint p = 0; p <= m; p++)
            }

         } // for (uint o = 0; o <= m; o++)

      } // comp_index
   }

   // Voxel adjacency delta coordinations.
   static const struct adjacent_coords
   {
      int8 x, y, z;
   } g_adjacency[26] = {
      {-1, -1, -1},
      {0, -1, -1},
      {1, -1, -1},
      {-1, 0, -1},
      {0, 0, -1},
      {1, 0, -1},
      {-1, 1, -1},
      {0, 1, -1},

      {1, 1, -1},
      {-1, -1, 0},
      {0, -1, 0},
      {1, -1, 0},
      {-1, 0, 0},
      {1, 0, 0},
      {-1, 1, 0},
      {0, 1, 0},

      {1, 1, 0},
      {-1, -1, 1},
      {0, -1, 1},
      {1, -1, 1},
      {-1, 0, 1},
      {0, 0, 1},
      {1, 0, 1},
      {-1, 1, 1},

      {0, 1, 1},
      {1, 1, 1}
   };

   // Attempt to refine current solution's endpoints given the current selectors using least squares.
   bool dxt1_endpoint_optimizer::refine_solution(int refinement_level)
   {
      CRNLIB_ASSERT(m_best_solution.m_valid);

      static const int w1Tab[4] = { 3,0,2,1 };

      static const int prods_0[4] = { 0x00,0x00,0x02,0x02 };
      static const int prods_1[4] = { 0x00,0x09,0x01,0x04 };
      static const int prods_2[4] = { 0x09,0x00,0x04,0x01 };

      double akku_0 = 0;
      double akku_1 = 0;
      double akku_2 = 0;
      double At1_r, At1_g, At1_b;
      double At2_r, At2_g, At2_b;

      At1_r = At1_g = At1_b = 0;
      At2_r = At2_g = At2_b = 0;
      for(uint i = 0; i < m_unique_colors.size(); i++)
      {
         const color_quad_u8& c = m_unique_colors[i].m_color;
         const double weight = m_unique_colors[i].m_weight;

         double r = c.r*weight;
         double g = c.g*weight;
         double b = c.b*weight;
         int step = m_best_solution.m_selectors[i]^1;

         int w1 = w1Tab[step];

         akku_0  += prods_0[step]*weight;
         akku_1  += prods_1[step]*weight;
         akku_2  += prods_2[step]*weight;
         At1_r   += w1*r;
         At1_g   += w1*g;
         At1_b   += w1*b;
         At2_r   += r;
         At2_g   += g;
         At2_b   += b;
      }

      At2_r = 3*At2_r - At1_r;
      At2_g = 3*At2_g - At1_g;
      At2_b = 3*At2_b - At1_b;

      double xx = akku_2;
      double yy = akku_1;
      double xy = akku_0;

      double t = xx * yy - xy * xy;
      if (!yy || !xx || (fabs(t) < .0000125f))
         return false;

      double frb = (3.0f * 31.0f / 255.0f) / t;
      double fg = frb * (63.0f / 31.0f);

      bool improved = false;

      if (refinement_level == 0)
      {
         uint max16;
         max16 =   math::clamp<int>(static_cast<int>((At1_r*yy - At2_r*xy)*frb+0.5f),0,31) << 11;
         max16 |=  math::clamp<int>(static_cast<int>((At1_g*yy - At2_g*xy)*fg +0.5f),0,63) << 5;
         max16 |=  math::clamp<int>(static_cast<int>((At1_b*yy - At2_b*xy)*frb+0.5f),0,31) << 0;

         uint min16;
         min16 =   math::clamp<int>(static_cast<int>((At2_r*xx - At1_r*xy)*frb+0.5f),0,31) << 11;
         min16 |=  math::clamp<int>(static_cast<int>((At2_g*xx - At1_g*xy)*fg +0.5f),0,63) << 5;
         min16 |=  math::clamp<int>(static_cast<int>((At2_b*xx - At1_b*xy)*frb+0.5f),0,31) << 0;

         dxt1_solution_coordinates nc((uint16)min16, (uint16)max16);
         nc.canonicalize();
         improved |= evaluate_solution(nc, true, &m_best_solution, false);
      }
      else if (refinement_level == 1)
      {
         // Try exploring the local lattice neighbors of the least squares optimized result.
         color_quad_u8 e[2];

         e[0].clear();
         e[0][0] = (uint8)math::clamp<int>(static_cast<int>((At1_r*yy - At2_r*xy)*frb+0.5f),0,31);
         e[0][1] = (uint8)math::clamp<int>(static_cast<int>((At1_g*yy - At2_g*xy)*fg +0.5f),0,63);
         e[0][2] = (uint8)math::clamp<int>(static_cast<int>((At1_b*yy - At2_b*xy)*frb+0.5f),0,31);

         e[1].clear();
         e[1][0] = (uint8)math::clamp<int>(static_cast<int>((At2_r*xx - At1_r*xy)*frb+0.5f),0,31);
         e[1][1] = (uint8)math::clamp<int>(static_cast<int>((At2_g*xx - At1_g*xy)*fg +0.5f),0,63);
         e[1][2] = (uint8)math::clamp<int>(static_cast<int>((At2_b*xx - At1_b*xy)*frb+0.5f),0,31);

         for (uint i = 0; i < 2; i++)
         {
            for (int rr = -1; rr <= 1; rr++)
            {
               for (int gr = -1; gr <= 1; gr++)
               {
                  for (int br = -1; br <= 1; br++)
                  {
                     dxt1_solution_coordinates nc;

                     color_quad_u8 c[2];
                     c[0] = e[0];
                     c[1] = e[1];

                     c[i][0] = (uint8)math::clamp<int>(c[i][0] + rr, 0, 31);
                     c[i][1] = (uint8)math::clamp<int>(c[i][1] + gr, 0, 63);
                     c[i][2] = (uint8)math::clamp<int>(c[i][2] + br, 0, 31);

                     nc.m_low_color = dxt1_block::pack_color(c[0], false);
                     nc.m_high_color = dxt1_block::pack_color(c[1], false);

                     nc.canonicalize();

                     if ((nc.m_low_color != m_best_solution.m_coords.m_low_color) || (nc.m_high_color != m_best_solution.m_coords.m_high_color))
                     {
                        improved |= evaluate_solution(nc, true, &m_best_solution, false);
                     }
                  }
               }
            }
         }
      }
      else
      {
         // Try even harder to explore the local lattice neighbors of the least squares optimized result.
         color_quad_u8 e[2];
         e[0].clear();
         e[0][0] = (uint8)math::clamp<int>(static_cast<int>((At1_r*yy - At2_r*xy)*frb+0.5f),0,31);
         e[0][1] = (uint8)math::clamp<int>(static_cast<int>((At1_g*yy - At2_g*xy)*fg +0.5f),0,63);
         e[0][2] = (uint8)math::clamp<int>(static_cast<int>((At1_b*yy - At2_b*xy)*frb+0.5f),0,31);

         e[1].clear();
         e[1][0] = (uint8)math::clamp<int>(static_cast<int>((At2_r*xx - At1_r*xy)*frb+0.5f),0,31);
         e[1][1] = (uint8)math::clamp<int>(static_cast<int>((At2_g*xx - At1_g*xy)*fg +0.5f),0,63);
         e[1][2] = (uint8)math::clamp<int>(static_cast<int>((At2_b*xx - At1_b*xy)*frb+0.5f),0,31);

         for (int orr = -1; orr <= 1; orr++)
         {
            for (int ogr = -1; ogr <= 1; ogr++)
            {
               for (int obr = -1; obr <= 1; obr++)
               {
                  dxt1_solution_coordinates nc;

                  color_quad_u8 c[2];
                  c[0] = e[0];
                  c[1] = e[1];

                  c[0][0] = (uint8)math::clamp<int>(c[0][0] + orr, 0, 31);
                  c[0][1] = (uint8)math::clamp<int>(c[0][1] + ogr, 0, 63);
                  c[0][2] = (uint8)math::clamp<int>(c[0][2] + obr, 0, 31);

                  for (int rr = -1; rr <= 1; rr++)
                  {
                     for (int gr = -1; gr <= 1; gr++)
                     {
                        for (int br = -1; br <= 1; br++)
                        {
                           c[1][0] = (uint8)math::clamp<int>(c[1][0] + rr, 0, 31);
                           c[1][1] = (uint8)math::clamp<int>(c[1][1] + gr, 0, 63);
                           c[1][2] = (uint8)math::clamp<int>(c[1][2] + br, 0, 31);

                           nc.m_low_color = dxt1_block::pack_color(c[0], false);
                           nc.m_high_color = dxt1_block::pack_color(c[1], false);
                           nc.canonicalize();

                           improved |= evaluate_solution(nc, true, &m_best_solution, false);
                        }
                     }
                  }
               }
            }
         }
      }

      return improved;
   }

   //-----------------------------------------------------------------------------------------------------------------------------------------

   // Primary endpoint optimization entrypoint.
   bool dxt1_endpoint_optimizer::optimize_endpoints(vec3F& low_color, vec3F& high_color)
   {
      vec3F orig_low_color(low_color);
      vec3F orig_high_color(high_color);

      m_trial_solution.clear();

      uint num_passes;
      const int16* pProbe_table = g_uber_probe_table;
      uint probe_range;
      float dist_per_trial = .015625f;

      // How many probes, and the distance between each probe depends on the quality level.
      switch (m_pParams->m_quality)
      {
         case cCRNDXTQualitySuperFast:
            pProbe_table = g_fast_probe_table;
            probe_range = cFastProbeTableSize;
            dist_per_trial = .027063293f;
            num_passes = 1;
            break;
         case cCRNDXTQualityFast:
            pProbe_table = g_fast_probe_table;
            probe_range = cFastProbeTableSize;
            dist_per_trial = .027063293f;
            num_passes = 2;
            break;
         case cCRNDXTQualityNormal:
            pProbe_table = g_normal_probe_table;
            probe_range = cNormalProbeTableSize;
            dist_per_trial = .027063293f;
            num_passes = 2;
            break;
         case cCRNDXTQualityBetter:
            pProbe_table = g_better_probe_table;
            probe_range = cBetterProbeTableSize;
            num_passes = 2;
            break;
         default:
            pProbe_table = g_uber_probe_table;
            probe_range = cUberProbeTableSize;
            num_passes = 4;
            break;
      }

      m_solutions_tried.reset();

      if (m_pParams->m_endpoint_caching)
      {
         // Try the previous X winning endpoints. This may not give us optimal results, but it may increase the probability of early outs while evaluating potential solutions.
         const uint num_prev_results = math::minimum<uint>(cMaxPrevResults, m_num_prev_results);
         for (uint i = 0; i < num_prev_results; i++)
         {
            const dxt1_solution_coordinates& coords = m_prev_results[i];

            solution_hash_map::insert_result solution_res(m_solutions_tried.insert(coords.m_low_color | (coords.m_high_color << 16U)));
            if (!solution_res.second)
               continue;

            evaluate_solution(coords, true, &m_best_solution);
         }

         if (!m_best_solution.m_error)
         {
            // Got lucky - one of the previous endpoints is optimal.
            return_solution(*m_pResults, m_best_solution);
            return true;
         }
      }

      if (m_pParams->m_quality >= cCRNDXTQualityBetter)
      {
         //evaluate_solution(dxt1_solution_coordinates(low_color, high_color), true, &m_best_solution);
         //refine_solution();

         try_median4(orig_low_color, orig_high_color);
      }

      uint probe_low[cUberProbeTableSize * 2 + 1];
      uint probe_high[cUberProbeTableSize * 2 + 1];

      vec3F scaled_principle_axis[2];

      scaled_principle_axis[1] = m_principle_axis * dist_per_trial;
      scaled_principle_axis[1][0] *= 31.0f;
      scaled_principle_axis[1][1] *= 63.0f;
      scaled_principle_axis[1][2] *= 31.0f;

      scaled_principle_axis[0] = -scaled_principle_axis[1];

      //vec3F initial_ofs(scaled_principle_axis * (float)-probe_range);
      //initial_ofs[0] += .5f;
      //initial_ofs[1] += .5f;
      //initial_ofs[2] += .5f;

      low_color[0] = math::clamp(low_color[0] * 31.0f, 0.0f, 31.0f);
      low_color[1] = math::clamp(low_color[1] * 63.0f, 0.0f, 63.0f);
      low_color[2] = math::clamp(low_color[2] * 31.0f, 0.0f, 31.0f);

      high_color[0] = math::clamp(high_color[0] * 31.0f, 0.0f, 31.0f);
      high_color[1] = math::clamp(high_color[1] * 63.0f, 0.0f, 63.0f);
      high_color[2] = math::clamp(high_color[2] * 31.0f, 0.0f, 31.0f);

      for (uint pass = 0; pass < num_passes; pass++)
      {
         // Now separately sweep or probe the low and high colors along the principle axis, both positively and negatively.
         // This results in two arrays of candidate low/high endpoints. Every unique combination of candidate endpoints is tried as a potential solution.
         // In higher quality modes, the various nearby lattice neighbors of each candidate endpoint are also explored, which allows the current solution to "wobble" or "migrate"
         // to areas with lower error.
         // This entire process can be repeated up to X times (depending on the quality level) until a local minimum is established.
         // This method is very stable and scalable. It could be implemented more elegantly, but I'm now very cautious of touching this code.
         if (pass)
         {
            low_color = unpack_to_vec3F_raw(m_best_solution.m_coords.m_low_color);
            high_color = unpack_to_vec3F_raw(m_best_solution.m_coords.m_high_color);
         }

         const uint64 prev_best_error = m_best_solution.m_error;
         if (!prev_best_error)
            break;

         // Sweep low endpoint along principle axis, record positions
         int prev_packed_color[2] = { -1, -1 };
         uint num_low_trials = 0;
         vec3F initial_probe_low_color(low_color + vec3F(.5f));
         for (uint i = 0; i < probe_range; i++)
         {
            const int ls = i ? 0 : 1;
            int x = pProbe_table[i];

            for (int s = ls; s < 2; s++)
            {
               vec3F probe_low_color(initial_probe_low_color + scaled_principle_axis[s] * (float)x);

               int r = math::clamp((int)floor(probe_low_color[0]), 0, 31);
               int g = math::clamp((int)floor(probe_low_color[1]), 0, 63);
               int b = math::clamp((int)floor(probe_low_color[2]), 0, 31);

               int packed_color = b | (g << 5U) | (r << 11U);
               if (packed_color != prev_packed_color[s])
               {
                  probe_low[num_low_trials++] = packed_color;
                  prev_packed_color[s] = packed_color;
               }
            }
         }

         prev_packed_color[0] = -1;
         prev_packed_color[1] = -1;

         // Sweep high endpoint along principle axis, record positions
         uint num_high_trials = 0;
         vec3F initial_probe_high_color(high_color + vec3F(.5f));
         for (uint i = 0; i < probe_range; i++)
         {
            const int ls = i ? 0 : 1;
            int x = pProbe_table[i];

            for (int s = ls; s < 2; s++)
            {
               vec3F probe_high_color(initial_probe_high_color + scaled_principle_axis[s] * (float)x);

               int r = math::clamp((int)floor(probe_high_color[0]), 0, 31);
               int g = math::clamp((int)floor(probe_high_color[1]), 0, 63);
               int b = math::clamp((int)floor(probe_high_color[2]), 0, 31);

               int packed_color = b | (g << 5U) | (r << 11U);
               if (packed_color != prev_packed_color[s])
               {
                  probe_high[num_high_trials++] = packed_color;
                  prev_packed_color[s] = packed_color;
               }
            }
         }

         // Now try all unique combinations.
         for (uint i = 0; i < num_low_trials; i++)
         {
            for (uint j = 0; j < num_high_trials; j++)
            {
               dxt1_solution_coordinates coords((uint16)probe_low[i], (uint16)probe_high[j]);
               coords.canonicalize();

               solution_hash_map::insert_result solution_res(m_solutions_tried.insert(coords.m_low_color | (coords.m_high_color << 16U)));
               if (!solution_res.second)
                  continue;

               evaluate_solution(coords, true, &m_best_solution);
            }
         }

         if (m_pParams->m_quality >= cCRNDXTQualityNormal)
         {
            // Generate new candidates by exploring the low color's direct lattice neighbors
            color_quad_u8 lc(dxt1_block::unpack_color(m_best_solution.m_coords.m_low_color, false));

            for (int i = 0; i < 26; i++)
            {
               int r = lc.r + g_adjacency[i].x;
               if ((r < 0) || (r > 31)) continue;

               int g = lc.g + g_adjacency[i].y;
               if ((g < 0) || (g > 63)) continue;

               int b = lc.b + g_adjacency[i].z;
               if ((b < 0) || (b > 31)) continue;

               dxt1_solution_coordinates coords(dxt1_block::pack_color(r, g, b, false), m_best_solution.m_coords.m_high_color);
               coords.canonicalize();

               solution_hash_map::insert_result solution_res(m_solutions_tried.insert(coords.m_low_color | (coords.m_high_color << 16U)));
               if (solution_res.second)
                  evaluate_solution(coords, true, &m_best_solution);
            }

            if (m_pParams->m_quality == cCRNDXTQualityUber)
            {
               // Generate new candidates by exploring the low color's direct lattice neighbors - this time, explore much further separately on each axis.
               lc = dxt1_block::unpack_color(m_best_solution.m_coords.m_low_color, false);

               for (int a = 0; a < 3; a++)
               {
                  int limit = (a == 1) ? 63 : 31;

                  for (int s = -2; s <= 2; s += 4)
                  {
                     color_quad_u8 c(lc);
                     int q = c[a] + s;
                     if ((q < 0) || (q > limit)) continue;

                     c[a] = (uint8)q;

                     dxt1_solution_coordinates coords(dxt1_block::pack_color(c, false), m_best_solution.m_coords.m_high_color);
                     coords.canonicalize();

                     solution_hash_map::insert_result solution_res(m_solutions_tried.insert(coords.m_low_color | (coords.m_high_color << 16U)));
                     if (solution_res.second)
                        evaluate_solution(coords, true, &m_best_solution);
                  }
               }
            }

            // Generate new candidates by exploring the high color's direct lattice neighbors
            color_quad_u8 hc(dxt1_block::unpack_color(m_best_solution.m_coords.m_high_color, false));

            for (int i = 0; i < 26; i++)
            {
               int r = hc.r + g_adjacency[i].x;
               if ((r < 0) || (r > 31)) continue;

               int g = hc.g + g_adjacency[i].y;
               if ((g < 0) || (g > 63)) continue;

               int b = hc.b + g_adjacency[i].z;
               if ((b < 0) || (b > 31)) continue;

               dxt1_solution_coordinates coords(m_best_solution.m_coords.m_low_color, dxt1_block::pack_color(r, g, b, false));
               coords.canonicalize();

               solution_hash_map::insert_result solution_res(m_solutions_tried.insert(coords.m_low_color | (coords.m_high_color << 16U)));
               if (solution_res.second)
                  evaluate_solution(coords, true, &m_best_solution);
            }

            if (m_pParams->m_quality == cCRNDXTQualityUber)
            {
               // Generate new candidates by exploring the high color's direct lattice neighbors - this time, explore much further separately on each axis.
               hc = dxt1_block::unpack_color(m_best_solution.m_coords.m_high_color, false);

               for (int a = 0; a < 3; a++)
               {
                  int limit = (a == 1) ? 63 : 31;

                  for (int s = -2; s <= 2; s += 4)
                  {
                     color_quad_u8 c(hc);
                     int q = c[a] + s;
                     if ((q < 0) || (q > limit)) continue;

                     c[a] = (uint8)q;

                     dxt1_solution_coordinates coords(m_best_solution.m_coords.m_low_color, dxt1_block::pack_color(c, false));
                     coords.canonicalize();

                     solution_hash_map::insert_result solution_res(m_solutions_tried.insert(coords.m_low_color | (coords.m_high_color << 16U)));
                     if (solution_res.second)
                        evaluate_solution(coords, true, &m_best_solution);
                  }
               }
            }
         }

         if ((!m_best_solution.m_error) || ((pass) && (m_best_solution.m_error == prev_best_error)))
            break;

         if (m_pParams->m_quality >= cCRNDXTQualityUber)
         {
            // Attempt to refine current solution's endpoints given the current selectors using least squares.
            refine_solution(1);
         }
      }

      if (m_pParams->m_quality >= cCRNDXTQualityNormal)
      {
         if ((m_best_solution.m_error) && (!m_pParams->m_pixels_have_alpha))
         {
            bool choose_solid_block = false;
            if (m_best_solution.are_selectors_all_equal())
            {
               // All selectors equal - try various solid-block optimizations
               choose_solid_block = try_average_block_as_solid();
            }

            if ((!choose_solid_block) && (m_pParams->m_quality == cCRNDXTQualityUber))
            {
               // Per-component 1D endpoint optimization.
               optimize_endpoint_comps();
            }
         }

         if (m_pParams->m_quality == cCRNDXTQualityUber)
         {
            if (m_best_solution.m_error)
            {
               // The pixels may have already been DXTc compressed by another compressor.
               // It's usually possible to recover the endpoints used to previously pack the block.
               try_combinatorial_encoding();
            }
         }
      }

      return_solution(*m_pResults, m_best_solution);

      if (m_pParams->m_endpoint_caching)
      {
         // Remember result for later reruse.
         m_prev_results[m_num_prev_results & (cMaxPrevResults - 1)] = m_best_solution.m_coords;
         m_num_prev_results++;
      }

      return true;
   }

   static inline int mul_8bit(int a, int b)
   {
      int t = a * b + 128;
      return (t + (t >> 8)) >> 8;
   }

   bool dxt1_endpoint_optimizer::handle_multicolor_block()
   {
      uint num_passes = 1;
      vec3F perceptual_weights(1.0f);

      if (m_perceptual)
      {
         // Compute RGB weighting for use in perceptual mode.
         // The more saturated the block, the more the weights deviate from (1,1,1).
         float ave_redness = 0;
         float ave_blueness = 0;
         float ave_l = 0;

         for (uint i = 0; i < m_unique_colors.size(); i++)
         {
            const color_quad_u8& c = m_unique_colors[i].m_color;
            const float weight = (float)m_unique_colors[i].m_weight;

            int l = mul_8bit(c.r + c.g + c.b, 0x55); // /3
            ave_l += l;
            l = math::maximum(1, l);

            float scale = weight / static_cast<float>(l);

            ave_redness += scale * c.r;
            ave_blueness += scale * c.b;
         }

         ave_redness /= m_total_unique_color_weight;
         ave_blueness /= m_total_unique_color_weight;
         ave_l /= m_total_unique_color_weight;

         ave_l = math::minimum(1.0f, ave_l * 16.0f / 255.0f);

         //float r = ave_l * powf(math::saturate(ave_redness / 3.0f), 5.0f);
         //float b = ave_l * powf(math::saturate(ave_blueness / 3.0f), 5.0f);

         float p = ave_l * powf(math::saturate(math::maximum(ave_redness, ave_blueness) * 1.0f/3.0f), 2.75f);

         if (p >= 1.0f)
            num_passes = 1;
         else
         {
            num_passes = 2;
            perceptual_weights = vec3F::lerp(vec3F(.212f, .72f, .072f), perceptual_weights, p);
         }
      }

      for (uint pass_index = 0; pass_index < num_passes; pass_index++)
      {
         compute_vectors(perceptual_weights);

         compute_pca(m_principle_axis, m_norm_unique_colors_weighted, vec3F(.2837149f, 0.9540631f, 0.096277453f));

#if 0
         matrix44F m(matrix44F::make_scale_matrix(perceptual_weights[0], perceptual_weights[1], perceptual_weights[2]));
         matrix44F im(m.get_inverse());
         im.transpose_in_place();
         m_principle_axis = m_principle_axis * im;
#else
         // Purposely scale the components of the principle axis by the perceptual weighting.
         // There's probably a cleaner way to go about this, but it works (more competitive in perceptual mode against nvdxt.exe or ATI_Compress).
         m_principle_axis[0] /= perceptual_weights[0];
         m_principle_axis[1] /= perceptual_weights[1];
         m_principle_axis[2] /= perceptual_weights[2];
#endif
         m_principle_axis.normalize_in_place();

         if (num_passes > 1)
         {
            // Check for obviously wild principle axes and try to compensate by backing off the component weightings.
            if (fabs(m_principle_axis[0]) >= .795f)
               perceptual_weights.set(.424f, .6f, .072f);
            else if (fabs(m_principle_axis[2]) >= .795f)
               perceptual_weights.set(.212f, .6f, .212f);
            else
               break;
         }
      }

      // Find bounds of projection onto (potentially skewed) principle axis.
      float l = 1e+9;
      float h = -1e+9;

      for (uint i = 0; i < m_norm_unique_colors.size(); i++)
      {
         float d = m_norm_unique_colors[i] * m_principle_axis;
         l = math::minimum(l, d);
         h = math::maximum(h, d);
      }

      vec3F low_color(m_mean_norm_color + l * m_principle_axis);
      vec3F high_color(m_mean_norm_color + h * m_principle_axis);

      if (!low_color.is_within_bounds(0.0f, 1.0f))
      {
         // Low color is outside the lattice, so bring it back in by casting a ray.
         vec3F coord;
         float t;
         aabb3F bounds(vec3F(0.0f), vec3F(1.0f));
         intersection::result res = intersection::ray_aabb(coord, t, ray3F(low_color, m_principle_axis), bounds);
         if (res == intersection::cSuccess)
            low_color = coord;
      }

      if (!high_color.is_within_bounds(0.0f, 1.0f))
      {
         // High color is outside the lattice, so bring it back in by casting a ray.
         vec3F coord;
         float t;
         aabb3F bounds(vec3F(0.0f), vec3F(1.0f));
         intersection::result res = intersection::ray_aabb(coord, t, ray3F(high_color, -m_principle_axis), bounds);
         if (res == intersection::cSuccess)
            high_color = coord;
      }

      // Now optimize the endpoints using the projection bounds on the (potentially skewed) principle axis as a starting point.
      if (!optimize_endpoints(low_color, high_color))
         return false;

      return true;
   }

   bool dxt1_endpoint_optimizer::handle_grayscale_block()
   {
      // TODO
      return true;
   }

   // Tries quantizing the block to 4 colors using vanilla LBG. It tries all combinations of the quantized results as potential endpoints.
   bool dxt1_endpoint_optimizer::try_median4(const vec3F& low_color, const vec3F& high_color)
   {
      vec3F means[4];

      if (m_unique_colors.size() <= 4)
      {
         for (uint i = 0; i < 4; i++)
            means[i] = m_norm_unique_colors[math::minimum<int>(m_norm_unique_colors.size() - 1, i)];
      }
      else
      {
         means[0] = low_color - m_mean_norm_color;
         means[3] = high_color - m_mean_norm_color;
         means[1] = vec3F::lerp(means[0], means[3], 1.0f/3.0f);
         means[2] = vec3F::lerp(means[0], means[3], 2.0f/3.0f);

         fast_random rm;

         const uint cMaxIters = 8;
         uint reassign_rover = 0;
         float prev_total_dist = math::cNearlyInfinite;
         for (uint iter = 0; iter < cMaxIters; iter++)
         {
            vec3F new_means[4];
            float new_weights[4];
            utils::zero_object(new_means);
            utils::zero_object(new_weights);

            float total_dist = 0;

            for (uint i = 0; i < m_unique_colors.size(); i++)
            {
               const vec3F& v = m_norm_unique_colors[i];

               float best_dist = means[0].squared_distance(v);
               int best_index = 0;

               for (uint j = 1; j < 4; j++)
               {
                  float dist = means[j].squared_distance(v);
                  if (dist < best_dist)
                  {
                     best_dist = dist;
                     best_index = j;
                  }
               }

               total_dist += best_dist;

               new_means[best_index] += v * (float)m_unique_colors[i].m_weight;
               new_weights[best_index] += (float)m_unique_colors[i].m_weight;
            }

            uint highest_index = 0;
            float highest_weight = 0;
            bool empty_cell = false;
            for (uint j = 0; j < 4; j++)
            {
               if (new_weights[j] > 0.0f)
               {
                  means[j] = new_means[j] / new_weights[j];
                  if (new_weights[j] > highest_weight)
                  {
                     highest_weight = new_weights[j];
                     highest_index = j;
                  }
               }
               else
                  empty_cell = true;
            }

            if (!empty_cell)
            {
               if (fabs(total_dist - prev_total_dist) < .00001f)
                  break;

               prev_total_dist = total_dist;
            }
            else
               prev_total_dist = math::cNearlyInfinite;

            if ((empty_cell) && (iter != (cMaxIters - 1)))
            {
               const uint ri = (highest_index + reassign_rover) & 3;
               reassign_rover++;

               for (uint j = 0; j < 4; j++)
               {
                  if (new_weights[j] == 0.0f)
                  {
                     means[j] = means[ri];
                     means[j] += vec3F::make_random(rm, -.00196f, .00196f);
                  }
               }
            }
         }
      }

      bool improved = false;

      for (uint i = 0; i < 3; i++)
      {
         for (uint j = i + 1; j < 4; j++)
         {
            const vec3F v0(means[i] + m_mean_norm_color);
            const vec3F v1(means[j] + m_mean_norm_color);

            dxt1_solution_coordinates sc(
               color_quad_u8((int)floor(.5f + v0[0] * 31.0f), (int)floor(.5f + v0[1] * 63.0f), (int)floor(.5f + v0[2] * 31.0f), 255),
               color_quad_u8((int)floor(.5f + v1[0] * 31.0f), (int)floor(.5f + v1[1] * 63.0f), (int)floor(.5f + v1[2] * 31.0f), 255), false );

            sc.canonicalize();

            improved |= evaluate_solution(sc, true, &m_best_solution, false);
         }
      }

      improved |= refine_solution((m_pParams->m_quality == cCRNDXTQualityUber) ? 1 : 0);

      return improved;
   }

   // Given candidate low/high endpoints, find the optimal selectors for 3 and 4 color blocks, compute the resulting error,
   // and use the candidate if it results in less error than the best found result so far.
   bool dxt1_endpoint_optimizer::evaluate_solution(
      const dxt1_solution_coordinates& coords,
      bool early_out,
      potential_solution* pBest_solution,
      bool alternate_rounding)
   {
      m_total_evals++;

      if ((!m_pSolutions) || (alternate_rounding))
      {
         if (m_pParams->m_quality >= cCRNDXTQualityBetter)
            return evaluate_solution_uber(m_trial_solution, coords, early_out, pBest_solution, alternate_rounding);
         else
            return evaluate_solution_fast(m_trial_solution, coords, early_out, pBest_solution, alternate_rounding);
      }

      evaluate_solution_uber(m_trial_solution, coords, false, NULL, alternate_rounding);

      CRNLIB_ASSERT(m_trial_solution.m_valid);

      // Caller has requested all considered candidate solutions for later analysis.
      m_pSolutions->resize(m_pSolutions->size() + 1);
      solution& new_solution = m_pSolutions->back();
      new_solution.m_selectors.resize(m_pParams->m_num_pixels);
      new_solution.m_results.m_pSelectors = &new_solution.m_selectors[0];

      return_solution(new_solution.m_results, m_trial_solution);

      if ((pBest_solution) && (m_trial_solution.m_error < m_best_solution.m_error))
      {
         *pBest_solution = m_trial_solution;
         return true;
      }

      return false;
   }

   inline uint dxt1_endpoint_optimizer::color_distance(bool perceptual, const color_quad_u8& e1, const color_quad_u8& e2, bool alpha)
   {
      if (perceptual)
      {
         return color::color_distance(true, e1, e2, alpha);
      }
      else if (m_pParams->m_grayscale_sampling)
      {
         // Computes error assuming shader will be converting the result to grayscale.
         int y0 = color::RGB_to_Y(e1);
         int y1 = color::RGB_to_Y(e2);
         int yd = y0  - y1;
         if (alpha)
         {
            int da = (int)e1[3] - (int)e2[3];
            return yd * yd + da * da;
         }
         else
         {
            return yd * yd;
         }
      }
      else if (m_has_color_weighting)
      {
         // Compute error using user provided color component weights.
         int dr = (int)e1[0] - (int)e2[0];
         int dg = (int)e1[1] - (int)e2[1];
         int db = (int)e1[2] - (int)e2[2];

         dr = (dr * dr) * m_pParams->m_color_weights[0];
         dg = (dg * dg) * m_pParams->m_color_weights[1];
         db = (db * db) * m_pParams->m_color_weights[2];

         if (alpha)
         {
            int da = (int)e1[3] - (int)e2[3];
            da = (da * da) * (m_pParams->m_color_weights[0] + m_pParams->m_color_weights[1] + m_pParams->m_color_weights[2]);
            return dr + dg + db + da;
         }
         else
         {
            return dr + dg + db;
         }
      }
      else
      {
         return color::color_distance(false, e1, e2, alpha);
      }
   }

   bool dxt1_endpoint_optimizer::evaluate_solution_uber(
      potential_solution& solution,
      const dxt1_solution_coordinates& coords,
      bool early_out,
      potential_solution* pBest_solution,
      bool alternate_rounding)
   {
      solution.m_coords = coords;
      solution.m_selectors.resize(m_unique_colors.size());

      if ((pBest_solution) && (early_out))
         solution.m_error = pBest_solution->m_error;
      else
         solution.m_error = cUINT64_MAX;

      solution.m_alpha_block = false;
      solution.m_valid = false;

      uint first_block_type = 0;
      uint last_block_type = 1;

      if ((m_pParams->m_pixels_have_alpha) || (m_pParams->m_force_alpha_blocks))
         first_block_type = 1;
      else if (!m_pParams->m_use_alpha_blocks)
         last_block_type = 0;

      m_trial_selectors.resize(m_unique_colors.size());

      color_quad_u8 colors[cDXT1SelectorValues];

      colors[0] = dxt1_block::unpack_color(coords.m_low_color, true);
      colors[1] = dxt1_block::unpack_color(coords.m_high_color, true);

      for (uint block_type = first_block_type; block_type <= last_block_type; block_type++)
      {
         uint64 trial_error = 0;

         if (!block_type)
         {
            colors[2].set_noclamp_rgba( (colors[0].r * 2 + colors[1].r + alternate_rounding) / 3, (colors[0].g * 2 + colors[1].g + alternate_rounding) / 3, (colors[0].b * 2 + colors[1].b + alternate_rounding) / 3, 0);
            colors[3].set_noclamp_rgba( (colors[1].r * 2 + colors[0].r + alternate_rounding) / 3, (colors[1].g * 2 + colors[0].g + alternate_rounding) / 3, (colors[1].b * 2 + colors[0].b + alternate_rounding) / 3, 0);

            if (m_perceptual)
            {
               for (int unique_color_index = (int)m_unique_colors.size() - 1; unique_color_index >= 0; unique_color_index--)
               {
                  const color_quad_u8& c = m_unique_colors[unique_color_index].m_color;

                  uint best_error = color_distance(true, c, colors[0], false);
                  uint best_color_index = 0;

                  uint err = color_distance(true, c, colors[1], false);
                  if (err < best_error) { best_error = err; best_color_index = 1; }

                  err = color_distance(true, c, colors[2], false);
                  if (err < best_error) { best_error = err; best_color_index = 2; }

                  err = color_distance(true, c, colors[3], false);
                  if (err < best_error) { best_error = err; best_color_index = 3; }

                  trial_error += best_error * m_unique_colors[unique_color_index].m_weight;
                  if (trial_error >= solution.m_error)
                     break;

                  m_trial_selectors[unique_color_index] = static_cast<uint8>(best_color_index);
               }
            }
            else
            {
               for (int unique_color_index = (int)m_unique_colors.size() - 1; unique_color_index >= 0; unique_color_index--)
               {
                  const color_quad_u8& c = m_unique_colors[unique_color_index].m_color;

                  uint best_error = color_distance(false, c, colors[0], false);
                  uint best_color_index = 0;

                  uint err = color_distance(false, c, colors[1], false);
                  if (err < best_error) { best_error = err; best_color_index = 1; }

                  err = color_distance(false, c, colors[2], false);
                  if (err < best_error) { best_error = err; best_color_index = 2; }

                  err = color_distance(false, c, colors[3], false);
                  if (err < best_error) { best_error = err; best_color_index = 3; }

                  trial_error += best_error * m_unique_colors[unique_color_index].m_weight;
                  if (trial_error >= solution.m_error)
                     break;

                  m_trial_selectors[unique_color_index] = static_cast<uint8>(best_color_index);
               }
            }
         }
         else
         {
            colors[2].set_noclamp_rgba( (colors[0].r + colors[1].r + alternate_rounding) >> 1, (colors[0].g + colors[1].g + alternate_rounding) >> 1, (colors[0].b + colors[1].b + alternate_rounding) >> 1, 255U);

            if (m_perceptual)
            {
               for (int unique_color_index = (int)m_unique_colors.size() - 1; unique_color_index >= 0; unique_color_index--)
               {
                  const color_quad_u8& c = m_unique_colors[unique_color_index].m_color;

                  uint best_error = color_distance(true, c, colors[0], false);
                  uint best_color_index = 0;

                  uint err = color_distance(true, c, colors[1], false);
                  if (err < best_error) { best_error = err; best_color_index = 1; }

                  err = color_distance(true, c, colors[2], false);
                  if (err < best_error) { best_error = err; best_color_index = 2; }

                  trial_error += best_error * m_unique_colors[unique_color_index].m_weight;
                  if (trial_error >= solution.m_error)
                     break;

                  m_trial_selectors[unique_color_index] = static_cast<uint8>(best_color_index);
               }
            }
            else
            {
               for (int unique_color_index = (int)m_unique_colors.size() - 1; unique_color_index >= 0; unique_color_index--)
               {
                  const color_quad_u8& c = m_unique_colors[unique_color_index].m_color;

                  uint best_error = color_distance(false, c, colors[0], false);
                  uint best_color_index = 0;

                  uint err = color_distance(false, c, colors[1], false);
                  if (err < best_error) { best_error = err; best_color_index = 1; }

                  err = color_distance(false, c, colors[2], false);
                  if (err < best_error) { best_error = err; best_color_index = 2; }

                  trial_error += best_error * m_unique_colors[unique_color_index].m_weight;
                  if (trial_error >= solution.m_error)
                     break;

                  m_trial_selectors[unique_color_index] = static_cast<uint8>(best_color_index);
               }
            }
         }

         if (trial_error < solution.m_error)
         {
            solution.m_error = trial_error;
            solution.m_alpha_block = (block_type != 0);
            solution.m_selectors = m_trial_selectors;
            solution.m_valid = true;
         }
      }

      if ((!solution.m_alpha_block) && (solution.m_coords.m_low_color == solution.m_coords.m_high_color))
      {
         uint s;
         if ((solution.m_coords.m_low_color & 31) != 31)
         {
            solution.m_coords.m_low_color++;
            s = 1;
         }
         else
         {
            solution.m_coords.m_high_color--;
            s = 0;
         }

         for (uint i = 0; i < m_unique_colors.size(); i++)
            solution.m_selectors[i] = static_cast<uint8>(s);
      }

      if ((pBest_solution) && (solution.m_error < pBest_solution->m_error))
      {
         *pBest_solution = solution;
         return true;
      }

      return false;
   }

   bool dxt1_endpoint_optimizer::evaluate_solution_fast(
      potential_solution& solution,
      const dxt1_solution_coordinates& coords,
      bool early_out,
      potential_solution* pBest_solution,
      bool alternate_rounding)
   {
      solution.m_coords = coords;
      solution.m_selectors.resize(m_unique_colors.size());

      if ((pBest_solution) && (early_out))
         solution.m_error = pBest_solution->m_error;
      else
         solution.m_error = cUINT64_MAX;

      solution.m_alpha_block = false;
      solution.m_valid = false;

      uint first_block_type = 0;
      uint last_block_type = 1;

      if ((m_pParams->m_pixels_have_alpha) || (m_pParams->m_force_alpha_blocks))
         first_block_type = 1;
      else if (!m_pParams->m_use_alpha_blocks)
         last_block_type = 0;

      m_trial_selectors.resize(m_unique_colors.size());

      color_quad_u8 colors[cDXT1SelectorValues];
      colors[0] = dxt1_block::unpack_color(coords.m_low_color, true);
      colors[1] = dxt1_block::unpack_color(coords.m_high_color, true);

      int vr = colors[1].r - colors[0].r;
      int vg = colors[1].g - colors[0].g;
      int vb = colors[1].b - colors[0].b;
      if (m_perceptual)
      {
         vr *= 8;
         vg *= 24;
      }

      int stops[4];
      stops[0] = colors[0].r*vr + colors[0].g*vg + colors[0].b*vb;
      stops[1] = colors[1].r*vr + colors[1].g*vg + colors[1].b*vb;

      int dirr = vr * 2;
      int dirg = vg * 2;
      int dirb = vb * 2;

      for (uint block_type = first_block_type; block_type <= last_block_type; block_type++)
      {
         uint64 trial_error = 0;

         if (!block_type)
         {
            colors[2].set_noclamp_rgba( (colors[0].r * 2 + colors[1].r + alternate_rounding) / 3, (colors[0].g * 2 + colors[1].g + alternate_rounding) / 3, (colors[0].b * 2 + colors[1].b + alternate_rounding) / 3, 255U);
            colors[3].set_noclamp_rgba( (colors[1].r * 2 + colors[0].r + alternate_rounding) / 3, (colors[1].g * 2 + colors[0].g + alternate_rounding) / 3, (colors[1].b * 2 + colors[0].b + alternate_rounding) / 3, 255U);

            stops[2] = colors[2].r*vr + colors[2].g*vg + colors[2].b*vb;
            stops[3] = colors[3].r*vr + colors[3].g*vg + colors[3].b*vb;

            // 0 2 3 1
            int c0Point = stops[1] + stops[3];
            int halfPoint = stops[3] + stops[2];
            int c3Point = stops[2] + stops[0];

            for (int unique_color_index = (int)m_unique_colors.size() - 1; unique_color_index >= 0; unique_color_index--)
            {
               const color_quad_u8& c = m_unique_colors[unique_color_index].m_color;

               int dot = c.r*dirr + c.g*dirg + c.b*dirb;

               uint8 best_color_index;
               if (dot < halfPoint)
                  best_color_index = (dot < c3Point) ? 0 : 2;
               else
                  best_color_index = (dot < c0Point) ? 3 : 1;

               uint best_error = color_distance(m_perceptual, c, colors[best_color_index], false);

               trial_error += best_error * m_unique_colors[unique_color_index].m_weight;
               if (trial_error >= solution.m_error)
                  break;

               m_trial_selectors[unique_color_index] = static_cast<uint8>(best_color_index);
            }
         }
         else
         {
            colors[2].set_noclamp_rgba( (colors[0].r + colors[1].r + alternate_rounding) >> 1, (colors[0].g + colors[1].g + alternate_rounding) >> 1, (colors[0].b + colors[1].b + alternate_rounding) >> 1, 255U);

            stops[2] = colors[2].r*vr + colors[2].g*vg + colors[2].b*vb;

            // 0 2 1
            int c02Point = stops[0] + stops[2];
            int c21Point = stops[2] + stops[1];

            for (int unique_color_index = (int)m_unique_colors.size() - 1; unique_color_index >= 0; unique_color_index--)
            {
               const color_quad_u8& c = m_unique_colors[unique_color_index].m_color;

               int dot = c.r*dirr + c.g*dirg + c.b*dirb;

               uint8 best_color_index;
               if (dot < c02Point)
                  best_color_index = 0;
               else if (dot < c21Point)
                  best_color_index = 2;
               else
                  best_color_index = 1;

               uint best_error = color_distance(m_perceptual, c, colors[best_color_index], false);

               trial_error += best_error * m_unique_colors[unique_color_index].m_weight;
               if (trial_error >= solution.m_error)
                  break;

               m_trial_selectors[unique_color_index] = static_cast<uint8>(best_color_index);
            }
         }

         if (trial_error < solution.m_error)
         {
            solution.m_error = trial_error;
            solution.m_alpha_block = (block_type != 0);
            solution.m_selectors = m_trial_selectors;
            solution.m_valid = true;
         }
      }

      if ((!solution.m_alpha_block) && (solution.m_coords.m_low_color == solution.m_coords.m_high_color))
      {
         uint s;
         if ((solution.m_coords.m_low_color & 31) != 31)
         {
            solution.m_coords.m_low_color++;
            s = 1;
         }
         else
         {
            solution.m_coords.m_high_color--;
            s = 0;
         }

         for (uint i = 0; i < m_unique_colors.size(); i++)
            solution.m_selectors[i] = static_cast<uint8>(s);
      }

      if ((pBest_solution) && (solution.m_error < pBest_solution->m_error))
      {
         *pBest_solution = solution;
         return true;
      }

      return false;
   }

   unique_color dxt1_endpoint_optimizer::lerp_color(const color_quad_u8& a, const color_quad_u8& b, float f, int rounding)
   {
      color_quad_u8 res;

      float r = rounding ? 1.0f : 0.0f;
      res[0] = static_cast<uint8>(math::clamp(math::float_to_int(r + math::lerp<float>(a[0], b[0], f)), 0, 255));
      res[1] = static_cast<uint8>(math::clamp(math::float_to_int(r + math::lerp<float>(a[1], b[1], f)), 0, 255));
      res[2] = static_cast<uint8>(math::clamp(math::float_to_int(r + math::lerp<float>(a[2], b[2], f)), 0, 255));
      res[3] = 255;

      return unique_color(res, 1);
   }

   // The block may have been already compressed using another DXTc compressor, such as squish, ATI_Compress, ryg_dxt, etc.
   // Attempt to recover the endpoints used by that block compressor.
   void dxt1_endpoint_optimizer::try_combinatorial_encoding()
   {
      if ((m_unique_colors.size() < 2) || (m_unique_colors.size() > 4))
         return;

      m_temp_unique_colors = m_unique_colors;

      if (m_temp_unique_colors.size() == 2)
      {
         // a    b    c   d
         // 0.0  1/3 2/3  1.0

         for (uint k = 0; k < 2; k++)
         {
            for (uint q = 0; q < 2; q++)
            {
               const uint r = q ^ 1;

               // a b
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, 2.0f, k));
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, 3.0f, k));

               // a c
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, .5f, k));
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, 1.5f, k));

               // a d

               // b c
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, -1.0f, k));
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, 2.0f, k));

               // b d
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, -.5f, k));
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, .5f, k));

               // c d
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, -2.0f, k));
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[q].m_color, m_temp_unique_colors[r].m_color, -1.0f, k));
            }
         }
      }
      else if (m_temp_unique_colors.size() == 3)
      {
         // a    b    c   d
         // 0.0  1/3 2/3  1.0

         for (uint i = 0; i <= 2; i++)
         {
            for (uint j = 0; j <= 2; j++)
            {
               if (i == j)
                  continue;

               // a b c
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[i].m_color, m_temp_unique_colors[j].m_color, 1.5f));

               // a b d
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[i].m_color, m_temp_unique_colors[j].m_color, 2.0f/3.0f));

               // a c d
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[i].m_color, m_temp_unique_colors[j].m_color, 1.0f/3.0f));

               // b c d
               m_temp_unique_colors.push_back(lerp_color(m_temp_unique_colors[i].m_color, m_temp_unique_colors[j].m_color, -.5f));
            }
         }
      }

      m_unique_packed_colors.resize(0);

      for (uint i = 0; i < m_temp_unique_colors.size(); i++)
      {
         const color_quad_u8& unique_color = m_temp_unique_colors[i].m_color;
         const uint16 packed_color = dxt1_block::pack_color(unique_color, true);

         if (std::find(m_unique_packed_colors.begin(), m_unique_packed_colors.end(), packed_color) != m_unique_packed_colors.end())
            continue;

         m_unique_packed_colors.push_back(packed_color);
      }

      if (m_unique_packed_colors.size() < 2)
         return;

      for (uint alt_rounding = 0; alt_rounding < 2; alt_rounding++)
      {
         for (uint i = 0; i < m_unique_packed_colors.size() - 1; i++)
         {
            for (uint j = i + 1; j < m_unique_packed_colors.size(); j++)
            {
               evaluate_solution(
                  dxt1_solution_coordinates(m_unique_packed_colors[i], m_unique_packed_colors[j]),
                  true,
                  (alt_rounding == 0) ? &m_best_solution : NULL,
                  (alt_rounding != 0));

               if (m_trial_solution.m_error == 0)
               {
                  if (alt_rounding)
                     m_best_solution = m_trial_solution;

                  return;
               }
            }
         }
      }

      return;
   }

   // The fourth (transparent) color in 3 color "transparent" blocks is black, which can be optionally exploited for small gains in DXT1 mode if the caller
   // doesn't actually use alpha. (But not in DXT5 mode, because 3-color blocks aren't permitted by GPU's for DXT5.)
   bool dxt1_endpoint_optimizer::try_alpha_as_black_optimization()
   {
      const params*  pOrig_params = m_pParams;
      pOrig_params;
      results*       pOrig_results = m_pResults;

      uint num_dark_colors = 0;

      for (uint i = 0; i < m_unique_colors.size(); i++)
         if ( (m_unique_colors[i].m_color[0] <= 4) && (m_unique_colors[i].m_color[1] <= 4) && (m_unique_colors[i].m_color[2] <= 4) )
            num_dark_colors++;

      if ( (!num_dark_colors) || (num_dark_colors == m_unique_colors.size()) )
         return true;

      params trial_params(*m_pParams);
      crnlib::vector<color_quad_u8> trial_colors;
      trial_colors.insert(0, m_pParams->m_pPixels, m_pParams->m_num_pixels);

      trial_params.m_pPixels = trial_colors.get_ptr();
      trial_params.m_pixels_have_alpha = true;

      for (uint i = 0; i < trial_colors.size(); i++)
         if ( (trial_colors[i][0] <= 4) && (trial_colors[i][1] <= 4) && (trial_colors[i][2] <= 4) )
            trial_colors[i][3] = 0;

      results trial_results;

      crnlib::vector<uint8> trial_selectors(m_pParams->m_num_pixels);
      trial_results.m_pSelectors = trial_selectors.get_ptr();

      if (!compute_internal(trial_params, trial_results, NULL))
         return false;

      CRNLIB_ASSERT(trial_results.m_alpha_block);

      color_quad_u8 c[4];
      dxt1_block::get_block_colors3(c, trial_results.m_low_color, trial_results.m_high_color);

      uint64 trial_error = 0;

      for (uint i = 0; i < trial_colors.size(); i++)
      {
         if (trial_colors[i][3] == 0)
         {
            CRNLIB_ASSERT(trial_selectors[i] == 3);
         }
         else
         {
            CRNLIB_ASSERT(trial_selectors[i] != 3);
         }

         trial_error += color_distance(m_perceptual, trial_colors[i], c[trial_selectors[i]], false);
      }

      if (trial_error < pOrig_results->m_error)
      {
         pOrig_results->m_error = trial_error;

         pOrig_results->m_low_color = trial_results.m_low_color;
         pOrig_results->m_high_color = trial_results.m_high_color;

         if (pOrig_results->m_pSelectors)
            memcpy(pOrig_results->m_pSelectors, trial_results.m_pSelectors, m_pParams->m_num_pixels);

         pOrig_results->m_alpha_block = true;
      }

      return true;
   }

   bool dxt1_endpoint_optimizer::compute_internal(const params& p, results& r, solution_vec* pSolutions)
   {
      clear();

      m_pParams = &p;
      m_pResults = &r;
      m_pSolutions = pSolutions;

      m_has_color_weighting = (m_pParams->m_color_weights[0] != 1) || (m_pParams->m_color_weights[1] != 1) || (m_pParams->m_color_weights[2] != 1);
      m_perceptual = m_pParams->m_perceptual && !m_has_color_weighting && !m_pParams->m_grayscale_sampling;

      find_unique_colors();

      m_best_solution.clear();

      if (m_unique_colors.empty())
         return handle_all_transparent_block();
      else if ((m_unique_colors.size() == 1) && (!m_has_transparent_pixels))
         return handle_solid_block();
      else
      {
         if (!handle_multicolor_block())
            return false;

         if ((m_all_pixels_grayscale) && (m_best_solution.m_error))
         {
            if (!handle_grayscale_block())
               return false;
         }
      }

      return true;
   }

   bool dxt1_endpoint_optimizer::compute(const params& p, results& r, solution_vec* pSolutions)
   {
      if (!p.m_pPixels)
         return false;

      bool status = compute_internal(p, r, pSolutions);
      if (!status)
         return false;

      if ( (m_pParams->m_use_alpha_blocks) && (m_pParams->m_use_transparent_indices_for_black) && (!m_pParams->m_pixels_have_alpha) && (!pSolutions) )
      {
         if (!try_alpha_as_black_optimization())
            return false;
      }

      return true;
   }

   // Build array of unique colors and their weights.
   void dxt1_endpoint_optimizer::find_unique_colors()
   {
      m_has_transparent_pixels = false;

      uint num_opaque_pixels = 0;

      const uint alpha_thresh = m_pParams->m_pixels_have_alpha ? (m_pParams->m_dxt1a_alpha_threshold << 24U) : 0;

      const uint32* pSrc_pixels = reinterpret_cast<const uint32*>(m_pParams->m_pPixels);
      const uint32* pSrc_pixels_end = pSrc_pixels + m_pParams->m_num_pixels;

      m_unique_colors.resize(m_pParams->m_num_pixels);
      uint num_unique_colors = 0;

      m_all_pixels_grayscale = true;

      do
      {
         uint32 c = utils::read_le32(pSrc_pixels);
         pSrc_pixels++;

         if (c < alpha_thresh)
         {
            m_has_transparent_pixels = true;
            continue;
         }

         if (m_all_pixels_grayscale)
         {
            uint r = c & 0xFF;
            uint g = (c >> 8) & 0xFF;
            uint b = (c >> 16) & 0xFF;
            if ((r != g) || (r != b))
               m_all_pixels_grayscale = false;
         }

         c |= 0xFF000000U;

         unique_color_hash_map::insert_result ins_result(m_unique_color_hash_map.insert(c, num_unique_colors));

         if (ins_result.second)
         {
            utils::write_le32(&m_unique_colors[num_unique_colors].m_color.m_u32, c);
            m_unique_colors[num_unique_colors].m_weight = 1;
            num_unique_colors++;
         }
         else
            m_unique_colors[ins_result.first->second].m_weight++;

         num_opaque_pixels++;

      } while (pSrc_pixels != pSrc_pixels_end);

      m_unique_colors.resize(num_unique_colors);

      m_total_unique_color_weight = num_opaque_pixels;
   }

} // namespace crnlib
