// File: crn_qdxt5.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_qdxt5.h"
#include "crn_dxt5a.h"
#include "crn_image.h"
#include "crn_image_utils.h"
#include "crn_dxt_fast.h"
#include "crn_dxt_hc_common.h"

#define QDXT5_DEBUGGING 0

namespace crnlib
{
    qdxt5::qdxt5(task_pool& task_pool) :
      m_pTask_pool(&task_pool),
      m_main_thread_id(0),
      m_canceled(false),
      m_progress_start(0),
      m_progress_range(100),
      m_num_blocks(0),
      m_pBlocks(NULL),
      m_pDst_elements(NULL),
      m_elements_per_block(0),
      m_max_selector_clusters(0),
      m_prev_percentage_complete(-1),
      m_selector_clusterizer(task_pool)
   {
   }

   qdxt5::~qdxt5()
   {
   }

   void qdxt5::clear()
   {
      m_main_thread_id = 0;
      m_num_blocks = 0;
      m_pBlocks = 0;
      m_pDst_elements = NULL;
      m_elements_per_block = 0;
      m_params.clear();
      m_endpoint_clusterizer.clear();
      m_endpoint_cluster_indices.clear();
      m_max_selector_clusters = 0;
      m_canceled = false;
      m_progress_start = 0;
      m_progress_range = 100;
      m_selector_clusterizer.clear();

      for (uint i = 0; i <= qdxt5_params::cMaxQuality; i++)
         m_cached_selector_cluster_indices[i].clear();

      m_cluster_hash.clear();

      m_prev_percentage_complete = -1;
   }

   bool qdxt5::init(uint n, const dxt_pixel_block* pBlocks, const qdxt5_params& params)
   {
      clear();

      CRNLIB_ASSERT(n && pBlocks);

      m_main_thread_id = crn_get_current_thread_id();

      m_num_blocks = n;
      m_pBlocks = pBlocks;
      m_params = params;

      m_endpoint_clusterizer.reserve_training_vecs(m_num_blocks);

      m_progress_start = 0;
      m_progress_range = 75;

      image_u8 debug_img;

      const bool debugging = true;

      if ((m_params.m_hierarchical) && (m_params.m_num_mips))
      {
         vec2F_clusterizer::training_vec_array& training_vecs = m_endpoint_clusterizer.get_training_vecs();
         training_vecs.resize(m_num_blocks);

         uint encoding_hist[cNumChunkEncodings];
         utils::zero_object(encoding_hist);

         uint total_processed_blocks = 0;
         uint next_progress_threshold = 512;

         for (uint level = 0; level < m_params.m_num_mips; level++)
         {
            const qdxt5_params::mip_desc& level_desc = m_params.m_mip_desc[level];

            const uint num_chunks_x = (level_desc.m_block_width + cChunkBlockWidth - 1) / cChunkBlockWidth;
            const uint num_chunks_y = (level_desc.m_block_height + cChunkBlockHeight - 1) / cChunkBlockHeight;

            const uint level_width = level_desc.m_block_width * 4;
            const uint level_height = level_desc.m_block_height * 4;

            if (debugging)
               debug_img.resize(num_chunks_x * cChunkPixelWidth, num_chunks_y * cChunkPixelHeight);

            for (uint chunk_y = 0; chunk_y < num_chunks_y; chunk_y++)
            {
               for (uint chunk_x = 0; chunk_x < num_chunks_x; chunk_x++)
               {
                  color_quad_u8 chunk_pixels[cChunkPixelWidth * cChunkPixelHeight];

                  for (uint y = 0; y < cChunkPixelHeight; y++)
                  {
                     const uint pix_y = math::minimum<uint>(chunk_y * cChunkPixelHeight + y, level_height - 1);

                     const uint outer_block_index = level_desc.m_first_block + ((pix_y >> 2) * level_desc.m_block_width);

                     for (uint x = 0; x < cChunkPixelWidth; x++)
                     {
                        const uint pix_x = math::minimum<uint>(chunk_x * cChunkPixelWidth + x, level_width - 1);

                        const uint block_index = outer_block_index + (pix_x >> 2);

                        const dxt_pixel_block& block = m_pBlocks[block_index];

                        const color_quad_u8& p = block.m_pixels[pix_y & 3][pix_x & 3];

                        chunk_pixels[x + y * 8] = p;
                     }
                  }

                  struct layout_results
                  {
                     uint m_low_color;
                     uint m_high_color;
                     uint8 m_selectors[cChunkPixelWidth * cChunkPixelHeight];
                     uint64 m_error;
                     //float m_penalty;
                  };
                  layout_results layouts[cNumChunkTileLayouts];

                  for (uint l = 0; l < cNumChunkTileLayouts; l++)
                  {
                     const uint width = g_chunk_tile_layouts[l].m_width;
                     const uint height = g_chunk_tile_layouts[l].m_height;
                     const uint x_ofs = g_chunk_tile_layouts[l].m_x_ofs;
                     const uint y_ofs = g_chunk_tile_layouts[l].m_y_ofs;

                     color_quad_u8 layout_pixels[cChunkPixelWidth * cChunkPixelHeight];
                     for (uint y = 0; y < height; y++)
                        for (uint x = 0; x < width; x++)
                           layout_pixels[x + y * width] = chunk_pixels[(x_ofs + x) + (y_ofs + y) * cChunkPixelWidth];

                     const uint n = width * height;
                     dxt_fast::compress_alpha_block(n, layout_pixels, layouts[l].m_low_color, layouts[l].m_high_color, layouts[l].m_selectors, m_params.m_comp_index);

                     uint c[dxt5_block::cMaxSelectorValues];
                     dxt5_block::get_block_values(c, layouts[l].m_low_color, layouts[l].m_high_color);

                     uint64 error = 0;
                     for (uint i = 0; i < n; i++)
                        error += math::square((int)layout_pixels[i][m_params.m_comp_index] - (int)c[layouts[l].m_selectors[i]]);

                     layouts[l].m_error = error;
                  }

                  double best_peak_snr = -1.0f;
                  uint best_encoding = 0;

                  for (uint e = 0; e < cNumChunkEncodings; e++)
                  {
                     const chunk_encoding_desc& encoding_desc = g_chunk_encodings[e];

                     double total_error = 0;

                     for (uint t = 0; t < encoding_desc.m_num_tiles; t++)
                        total_error += (double)layouts[encoding_desc.m_tiles[t].m_layout_index].m_error;

                     double mean_squared = total_error * (1.0f / 64.0f);
                     double root_mean_squared = sqrt(mean_squared);

                     double peak_snr = 999999.0f;
                     if (mean_squared)
                        peak_snr = math::clamp<double>(log10(255.0f / root_mean_squared) * 20.0f, 0.0f, 500.0f);

                     float adaptive_tile_alpha_psnr_derating = 2.4f;
                     //if (level)
                     //   adaptive_tile_alpha_psnr_derating = math::lerp(adaptive_tile_alpha_psnr_derating * .5f, .3f, math::maximum((level - 1) / float(m_params.m_num_mips - 2), 1.0f));
                     if ((level) && (adaptive_tile_alpha_psnr_derating > .25f))
                     {
                        adaptive_tile_alpha_psnr_derating = math::maximum(.25f, adaptive_tile_alpha_psnr_derating / powf(3.0f, static_cast<float>(level)));
                     }

                     float alpha_derating = math::lerp( 0.0f, adaptive_tile_alpha_psnr_derating, (g_chunk_encodings[e].m_num_tiles - 1) / 3.0f );
                     peak_snr = peak_snr - alpha_derating;

                     //for (uint t = 0; t < encoding_desc.m_num_tiles; t++)
                     //   peak_snr -= (double)layouts[encoding_desc.m_tiles[t].m_layout_index].m_penalty;

                     if (peak_snr > best_peak_snr)
                     {
                        best_peak_snr = peak_snr;
                        best_encoding = e;
                     }
                  }

                  encoding_hist[best_encoding]++;

                  const chunk_encoding_desc& encoding_desc = g_chunk_encodings[best_encoding];

                  for (uint t = 0; t < encoding_desc.m_num_tiles; t++)
                  {
                     const chunk_tile_desc& tile_desc = encoding_desc.m_tiles[t];

                     uint layout_index = tile_desc.m_layout_index;
                     const layout_results& layout = layouts[layout_index];

                     uint c[dxt5_block::cMaxSelectorValues];
                     if (debugging)
                        dxt5_block::get_block_values(c, layout.m_low_color, layout.m_high_color);

                     color_quad_u8 tile_pixels[cChunkPixelWidth * cChunkPixelHeight];

                     for (uint y = 0; y < tile_desc.m_height; y++)
                     {
                        const uint pix_y = y + tile_desc.m_y_ofs;

                        for (uint x = 0; x < tile_desc.m_width; x++)
                        {
                           const uint pix_x = x + tile_desc.m_x_ofs;

                           uint a = chunk_pixels[pix_x + pix_y * cChunkPixelWidth][m_params.m_comp_index];

                           tile_pixels[x + y * tile_desc.m_width].set(a, a, a, 255);

                           if (debugging)
                              debug_img(chunk_x * 8 + pix_x, chunk_y * 8 + pix_y) = c[layout.m_selectors[x + y * tile_desc.m_width]];
                        }
                     }

                     color_quad_u8 l, h;
                     dxt_fast::find_representative_colors(tile_desc.m_width * tile_desc.m_height, tile_pixels, l, h);

                     const uint dist = math::square<int>((int)l[0] - (int)h[0]);

                     const int cAlphaErrorToWeight = 8;
                     const uint cMaxWeight = 8;
                     uint weight = math::clamp<uint>(dist / cAlphaErrorToWeight, 1, cMaxWeight);

                     vec2F ev;

                     ev[0] = l[0];
                     ev[1] = h[0];

                     for (uint y = 0; y < (tile_desc.m_height >> 2); y++)
                     {
                        uint block_y = chunk_y * cChunkBlockHeight + y + (tile_desc.m_y_ofs >> 2);
                        if (block_y >= level_desc.m_block_height)
                           continue;

                        for (uint x = 0; x < (tile_desc.m_width >> 2); x++)
                        {
                           uint block_x = chunk_x * cChunkBlockWidth + x + (tile_desc.m_x_ofs >> 2);
                           if (block_x >= level_desc.m_block_width)
                              break;

                           uint block_index = level_desc.m_first_block + block_x + block_y * level_desc.m_block_width;

                           training_vecs[block_index].first = ev;
                           training_vecs[block_index].second = weight;

                           total_processed_blocks++;

                        } // x
                     } // y
                  } //t

                  if (total_processed_blocks >= next_progress_threshold)
                  {
                     next_progress_threshold += 512;

                     if (!update_progress(total_processed_blocks, m_num_blocks - 1))
                        return false;
                  }

               } // chunk_x
            } // chunk_y

#if QDXT5_DEBUGGING
            if (debugging)
               image_utils::write_to_file(dynamic_wstring(cVarArg, "debug_%u.tga", level).get_ptr(), debug_img, image_utils::cWriteFlagIgnoreAlpha);
#endif

         } // level

#if 0
         trace("chunk encoding hist: ");
         for (uint i = 0; i < cNumChunkEncodings; i++)
            trace("%u ", encoding_hist[i]);
         trace("\n");
#endif
      }
      else
      {
         for (uint block_index = 0; block_index < m_num_blocks; block_index++)
         {
            if ((block_index & 511) == 0)
            {
               if (!update_progress(block_index, m_num_blocks - 1))
                  return false;
            }

            color_quad_u8 c[16];
            for (uint y = 0; y < cDXTBlockSize; y++)
               for (uint x = 0; x < cDXTBlockSize; x++)
                  c[x+y*cDXTBlockSize].set(m_pBlocks[block_index].m_pixels[y][x][m_params.m_comp_index], 255);

            color_quad_u8 l, h;
            dxt_fast::find_representative_colors(cDXTBlockSize * cDXTBlockSize, c, l, h);

            const uint dist = math::square<int>((int)l[0] - (int)h[0]);

            const int cAlphaErrorToWeight = 8;
            const uint cMaxWeight = 8;
            uint weight = math::clamp<uint>(dist / cAlphaErrorToWeight, 1, cMaxWeight);

            vec2F ev;

            ev[0] = l[0];
            ev[1] = h[0];

            m_endpoint_clusterizer.add_training_vec(ev, weight);
         }
      }

      const uint cMaxEndpointClusters = 65535U;

      m_progress_start = 75;
      m_progress_range = 20;

      if (!m_endpoint_clusterizer.generate_codebook(cMaxEndpointClusters, generate_codebook_progress_callback, this))
         return false;

      crnlib::hash_map<uint64, empty_type> selector_hash;

      m_progress_start = 95;
      m_progress_range = 5;

      for (uint block_index = 0; block_index < m_num_blocks; block_index++)
      {
         if ((block_index & 511) == 0)
         {
            if (!update_progress(block_index, m_num_blocks - 1))
               return false;
         }

         dxt5_block dxt_blk;
         dxt_fast::compress_alpha_block(&dxt_blk, &m_pBlocks[block_index].m_pixels[0][0], m_params.m_comp_index);

         uint64 selectors = 0;
         for (uint i = 0; i < dxt5_block::cNumSelectorBytes; i++)
            selectors |= static_cast<uint64>(dxt_blk.m_selectors[i]) << (i * 8U);

         selector_hash.insert(selectors);
      }

      m_max_selector_clusters = selector_hash.size() + 128;

      update_progress(1, 1);

      return true;
   }

   bool qdxt5::update_progress(uint value, uint max_value)
   {
      if (!m_params.m_pProgress_func)
         return true;

      uint percentage = max_value ? (m_progress_start + (value * m_progress_range + (max_value / 2)) / max_value) : 100;
      if ((int)percentage == m_prev_percentage_complete)
         return true;
      m_prev_percentage_complete = percentage;

      if (!m_params.m_pProgress_func(m_params.m_progress_start + (percentage * m_params.m_progress_range) / 100U, m_params.m_pProgress_data))
      {
         m_canceled = true;
         return false;
      }

      return true;
   }

   void qdxt5::pack_endpoints_task(uint64 data, void* pData_ptr)
   {
      pData_ptr;
      const uint thread_index = static_cast<uint>(data);

      crnlib::vector<color_quad_u8> cluster_pixels;
      cluster_pixels.reserve(1024);

      crnlib::vector<uint8> selectors;
      selectors.reserve(1024);

      dxt5_endpoint_optimizer optimizer;
      dxt5_endpoint_optimizer::params p;
      dxt5_endpoint_optimizer::results r;

      p.m_quality = m_params.m_dxt_quality;
      p.m_comp_index = m_params.m_comp_index;
      p.m_use_both_block_types = m_params.m_use_both_block_types;

      uint cluster_index_progress_mask = math::next_pow2(m_endpoint_cluster_indices.size() / 100);
      cluster_index_progress_mask /= 2;
      cluster_index_progress_mask = math::maximum<uint>(cluster_index_progress_mask, 8);
      cluster_index_progress_mask -= 1;

      for (uint cluster_index = 0; cluster_index < m_endpoint_cluster_indices.size(); cluster_index++)
      {
         if (m_canceled)
            return;

         if ((cluster_index & cluster_index_progress_mask) == 0)
         {
            if (crn_get_current_thread_id() == m_main_thread_id)
            {
               if (!update_progress(cluster_index, m_endpoint_cluster_indices.size() - 1))
                  return;
            }
         }

         if (m_pTask_pool->get_num_threads())
         {
            if ((cluster_index % (m_pTask_pool->get_num_threads() + 1)) != thread_index)
               continue;
         }

         const crnlib::vector<uint>& cluster_indices = m_endpoint_cluster_indices[cluster_index];

         selectors.resize(cluster_indices.size() * cDXTBlockSize * cDXTBlockSize);

         cluster_pixels.resize(cluster_indices.size() * cDXTBlockSize * cDXTBlockSize);

         color_quad_u8* pDst = &cluster_pixels[0];

         for (uint block_iter = 0; block_iter < cluster_indices.size(); block_iter++)
         {
            const uint block_index = cluster_indices[block_iter];

            //const color_quad_u8* pSrc_pixels = &m_pBlocks[block_index].m_pixels[0][0];
            const color_quad_u8* pSrc_pixels = (const color_quad_u8*)m_pBlocks[block_index].m_pixels;

            for (uint i = 0; i < cDXTBlockSize * cDXTBlockSize; i++)
            {
               const color_quad_u8& src = pSrc_pixels[i];

               *pDst++ = src;
            }
         }

         p.m_block_index = cluster_index;
         p.m_num_pixels = cluster_pixels.size();
         p.m_pPixels = cluster_pixels.begin();

         r.m_pSelectors = selectors.begin();

         uint low_color;
         uint high_color;
         if (m_params.m_dxt_quality != cCRNDXTQualitySuperFast)
         {
            optimizer.compute(p, r);
            low_color = r.m_first_endpoint;
            high_color = r.m_second_endpoint;
         }
         else
         {
            dxt_fast::compress_alpha_block(cluster_pixels.size(), cluster_pixels.begin(), low_color, high_color, selectors.begin(), m_params.m_comp_index);
         }

         const uint8* pSrc_selectors = selectors.begin();

         for (uint block_iter = 0; block_iter < cluster_indices.size(); block_iter++)
         {
            const uint block_index = cluster_indices[block_iter];

            dxt5_block& dxt_block = get_block(block_index);

            dxt_block.set_low_alpha(low_color);
            dxt_block.set_high_alpha(high_color);

            for (uint y = 0; y < 4; y++)
               for (uint x = 0; x < 4; x++)
                  dxt_block.set_selector(x, y, *pSrc_selectors++);
         }
      }
   }

   struct optimize_selectors_params
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(optimize_selectors_params);

      optimize_selectors_params(
         crnlib::vector< crnlib::vector<uint> >&  selector_cluster_indices) :
         m_selector_cluster_indices(selector_cluster_indices)
      {
      }

      crnlib::vector< crnlib::vector<uint> >&  m_selector_cluster_indices;
   };

   void qdxt5::optimize_selectors_task(uint64 data, void* pData_ptr)
   {
      const uint thread_index = static_cast<uint>(data);

      optimize_selectors_params& task_params = *static_cast<optimize_selectors_params*>(pData_ptr);

      crnlib::vector<uint> block_categories[2];
      block_categories[0].reserve(2048);
      block_categories[1].reserve(2048);

      for (uint cluster_index = 0; cluster_index < task_params.m_selector_cluster_indices.size(); cluster_index++)
      {
         if (m_canceled)
            return;

         if ((cluster_index & 255) == 0)
         {
            if (crn_get_current_thread_id() == m_main_thread_id)
            {
               if (!update_progress(cluster_index, task_params.m_selector_cluster_indices.size() - 1))
                  return;
            }
         }

         if (m_pTask_pool->get_num_threads())
         {
            if ((cluster_index % (m_pTask_pool->get_num_threads() + 1)) != thread_index)
               continue;
         }

         const crnlib::vector<uint>& selector_indices = task_params.m_selector_cluster_indices[cluster_index];

         if (selector_indices.size() <= 1)
            continue;

         block_categories[0].resize(0);
         block_categories[1].resize(0);

         for (uint block_iter = 0; block_iter < selector_indices.size(); block_iter++)
         {
            const uint block_index = selector_indices[block_iter];

            const dxt5_block& src_block = get_block(block_index);

            block_categories[src_block.is_alpha6_block()].push_back(block_index);
         }

         dxt5_block blk;
         utils::zero_object(blk);

         for (uint block_type = 0; block_type <= 1; block_type++)
         {
            const crnlib::vector<uint>& block_indices = block_categories[block_type];
            if (block_indices.size() <= 1)
               continue;

            for (uint y = 0; y < cDXTBlockSize; y++)
            {
               for (uint x = 0; x < cDXTBlockSize; x++)
               {
                  uint best_s = 0;
                  uint64 best_error = 0xFFFFFFFFFFULL;

                  for (uint s = 0; s < dxt5_block::cMaxSelectorValues; s++)
                  {
                     uint64 total_error = 0;

                     for (uint block_iter = 0; block_iter < block_indices.size(); block_iter++)
                     {
                        const uint block_index = block_indices[block_iter];

                        const color_quad_u8& orig_color = m_pBlocks[block_index].m_pixels[y][x];

                        const dxt5_block& dst_block = get_block(block_index);

                        uint values[dxt5_block::cMaxSelectorValues];
                        dxt5_block::get_block_values(values, dst_block.get_low_alpha(), dst_block.get_high_alpha());

                        int error = math::square((int)orig_color[m_params.m_comp_index] - (int)values[s]);

                        total_error += error;
                     }

                     if (total_error < best_error)
                     {
                        best_error = total_error;
                        best_s = s;
                     }
                  }

                  blk.set_selector(x, y, best_s);

               } // x
            } // y

            for (uint block_iter = 0; block_iter < block_indices.size(); block_iter++)
            {
               const uint block_index = block_indices[block_iter];

               dxt5_block& dst_block = get_block(block_index);

               memcpy(dst_block.m_selectors, blk.m_selectors, sizeof(dst_block.m_selectors));
            }
         }

      } // cluster_index
   }

   bool qdxt5::generate_codebook_progress_callback(uint percentage_completed, void* pData)
   {
      return static_cast<qdxt5*>(pData)->update_progress(percentage_completed, 100U);
   }

   bool qdxt5::create_selector_clusters(uint max_selector_clusters, crnlib::vector< crnlib::vector<uint> >& selector_cluster_indices)
   {
      weighted_selector_vec_array selector_vecs[2];
      crnlib::vector<uint> selector_vec_remap[2];

      for (uint block_type = 0; block_type < 2; block_type++)
      {
         for (uint block_iter = 0; block_iter < m_num_blocks; block_iter++)
         {
            dxt5_block& dxt5_block = get_block(block_iter);
            if ((uint)dxt5_block.is_alpha6_block() != block_type)
               continue;

            vec16F sv;
            float* pDst = &sv[0];

            bool uses_absolute_values = false;

            for (uint y = 0; y < 4; y++)
            {
               for (uint x = 0; x < 4; x++)
               {
                  const uint s = dxt5_block.get_selector(x, y);

                  float f;
                  if (dxt5_block.is_alpha6_block())
                  {
                     if (s >= 6)
                     {
                        uses_absolute_values = true;
                        f = 0.0f;
                     }
                     else
                        f = g_dxt5_alpha6_to_linear[s];
                  }
                  else
                     f = g_dxt5_to_linear[s];

                  *pDst++ = f;
               }
            }

            if (uses_absolute_values)
               continue;

            int low_alpha = dxt5_block.get_low_alpha();
            int high_alpha = dxt5_block.get_high_alpha();
            int dist = math::square(low_alpha - high_alpha);

            const uint cAlphaDistToWeight = 8;
            const uint cMaxWeight = 2048;
            uint weight = math::clamp<uint>(dist / cAlphaDistToWeight, 1, cMaxWeight);

            selector_vecs[block_type].resize(selector_vecs[block_type].size() + 1);
            selector_vecs[block_type].back().m_vec = sv;
            selector_vecs[block_type].back().m_weight = weight;

            selector_vec_remap[block_type].push_back(block_iter);
         }
      }

      selector_cluster_indices.clear();

      for (uint block_type = 0; block_type < 2; block_type++)
      {
         if (selector_vecs[block_type].empty())
            continue;

         if ((selector_vecs[block_type].size() / (float)m_num_blocks) < .01f)
            continue;
         uint max_clusters = static_cast<uint>((math::emulu(selector_vecs[block_type].size(), max_selector_clusters) + (m_num_blocks - 1)) / m_num_blocks);
         max_clusters = math::minimum(math::maximum(64U, max_clusters), selector_vecs[block_type].size());
         if (max_clusters >= selector_vecs[block_type].size())
            continue;

#if QDXT5_DEBUGGING
         trace("max_clusters (%u): %u\n", block_type, max_clusters);
#endif

         crnlib::vector< crnlib::vector<uint> > block_type_selector_cluster_indices;

         if (!block_type)
         {
            m_progress_start = m_progress_range;
            m_progress_range = 16;
         }
         else
         {
            m_progress_start = m_progress_range + 16;
            m_progress_range = 17;
         }

         if (!m_selector_clusterizer.create_clusters(
            selector_vecs[block_type], max_clusters, block_type_selector_cluster_indices, generate_codebook_progress_callback, this))
         {
            return false;
         }

         const uint first_cluster = selector_cluster_indices.size();
         selector_cluster_indices.enlarge(block_type_selector_cluster_indices.size());

         for (uint i = 0; i < block_type_selector_cluster_indices.size(); i++)
         {
            crnlib::vector<uint>& indices = selector_cluster_indices[first_cluster + i];
            indices.swap(block_type_selector_cluster_indices[i]);

            for (uint j = 0; j < indices.size(); j++)
               indices.at(j) = selector_vec_remap[block_type][indices.at(j)];
         }
      }

      return true;
   }

   bool qdxt5::pack(dxt5_block* pDst_elements, uint elements_per_block, const qdxt5_params& params)
   {
      CRNLIB_ASSERT(m_num_blocks);

      m_main_thread_id = crn_get_current_thread_id();
      m_canceled = false;

      m_pDst_elements = pDst_elements;
      m_elements_per_block = elements_per_block;
      m_params = params;

      m_prev_percentage_complete = -1;

      CRNLIB_ASSERT(m_params.m_quality_level <= qdxt5_params::cMaxQuality);
      const float quality = m_params.m_quality_level / (float)qdxt5_params::cMaxQuality;
      const float endpoint_quality = powf(quality, 2.1f);
      const float selector_quality = powf(quality, 1.65f);

      const uint max_endpoint_clusters = math::clamp<uint>(static_cast<uint>(m_endpoint_clusterizer.get_codebook_size() * endpoint_quality), 16U, m_endpoint_clusterizer.get_codebook_size());
      const uint max_selector_clusters = math::clamp<uint>(static_cast<uint>(m_max_selector_clusters * selector_quality), 32U, m_max_selector_clusters);

#if QDXT5_DEBUGGING
      trace("max endpoint clusters: %u\n", max_endpoint_clusters);
      trace("max selector clusters: %u\n", max_selector_clusters);
#endif

      if (quality >= 1.0f)
      {
         m_endpoint_cluster_indices.resize(m_num_blocks);
         for (uint i = 0; i < m_num_blocks; i++)
         {
            m_endpoint_cluster_indices[i].resize(1);
            m_endpoint_cluster_indices[i][0] = i;
         }
      }
      else
         m_endpoint_clusterizer.retrieve_clusters(max_endpoint_clusters, m_endpoint_cluster_indices);

      uint total_blocks = 0;
      uint max_blocks = 0;
      for (uint i = 0; i < m_endpoint_cluster_indices.size(); i++)
      {
         uint num = m_endpoint_cluster_indices[i].size();
         total_blocks += num;
         max_blocks = math::maximum(max_blocks, num);
      }

      crnlib::vector< crnlib::vector<uint> >& selector_cluster_indices = m_cached_selector_cluster_indices[params.m_quality_level];

      m_progress_start = 0;
      if (quality >= 1.0f)
         m_progress_range = 100;
      else if (selector_cluster_indices.empty())
         m_progress_range = (m_params.m_dxt_quality == cCRNDXTQualitySuperFast) ? 10 : 33;
      else
         m_progress_range = (m_params.m_dxt_quality == cCRNDXTQualitySuperFast) ? 10 : 50;

      for (uint i = 0; i <= m_pTask_pool->get_num_threads(); i++)
         m_pTask_pool->queue_object_task(this, &qdxt5::pack_endpoints_task, i);
      m_pTask_pool->join();

      if (m_canceled)
         return false;

      if (quality >= 1.0f)
         return true;

      if (selector_cluster_indices.empty())
      {
         create_selector_clusters(max_selector_clusters, selector_cluster_indices);

         if (m_canceled)
         {
            selector_cluster_indices.clear();

            return false;
         }
      }

      m_progress_start += m_progress_range;
      m_progress_range = 100 - m_progress_start;

      optimize_selectors_params optimize_selectors_task_params(selector_cluster_indices);

      for (uint i = 0; i <= m_pTask_pool->get_num_threads(); i++)
         m_pTask_pool->queue_object_task(this, &qdxt5::optimize_selectors_task, i, &optimize_selectors_task_params);

      m_pTask_pool->join();

      return !m_canceled;
   }

} // namespace crnlib
