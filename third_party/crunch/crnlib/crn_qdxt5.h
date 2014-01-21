// File: crn_qdxt5.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_hash_map.h"
#include "crn_clusterizer.h"
#include "crn_hash.h"
#include "crn_threaded_clusterizer.h"
#include "crn_dxt.h"
#include "crn_dxt_image.h"

namespace crnlib
{
   struct qdxt5_params
   {
      qdxt5_params()
      {
         clear();
      }

      void clear()
      {
         m_quality_level = cMaxQuality;
         m_dxt_quality = cCRNDXTQualityUber;

         m_pProgress_func = NULL;
         m_pProgress_data = NULL;
         m_num_mips = 0;
         m_hierarchical = true;
         utils::zero_object(m_mip_desc);

         m_comp_index = 3;
         m_progress_start = 0;
         m_progress_range = 100;

         m_use_both_block_types = true;
      }

      void init(const dxt_image::pack_params &pp, int quality_level, bool hierarchical, int comp_index = 3)
      {
         m_dxt_quality = pp.m_quality;
         m_hierarchical = hierarchical;
         m_comp_index = comp_index;
         m_use_both_block_types = pp.m_use_both_block_types;
         m_quality_level = quality_level;
      }

      enum { cMaxQuality = cCRNMaxQualityLevel };
      uint m_quality_level;
      crn_dxt_quality m_dxt_quality;
      bool m_hierarchical;

      struct mip_desc
      {
         uint m_first_block;
         uint m_block_width;
         uint m_block_height;
      };

      uint m_num_mips;
      enum { cMaxMips = 128 };
      mip_desc m_mip_desc[cMaxMips];

      typedef bool (*progress_callback_func)(uint percentage_completed, void* pProgress_data);
      progress_callback_func m_pProgress_func;
      void* m_pProgress_data;
      uint m_progress_start;
      uint m_progress_range;

      uint m_comp_index;

      bool m_use_both_block_types;
   };

   class qdxt5
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(qdxt5);

   public:
      qdxt5(task_pool& task_pool);
      ~qdxt5();

      void clear();

      bool init(uint n, const dxt_pixel_block* pBlocks, const qdxt5_params& params);

      uint get_num_blocks() const { return m_num_blocks; }
      const dxt_pixel_block* get_blocks() const { return m_pBlocks; }

      bool pack(dxt5_block* pDst_elements, uint elements_per_block, const qdxt5_params& params);

   private:
      task_pool*           m_pTask_pool;
      crn_thread_id_t      m_main_thread_id;
      bool                 m_canceled;

      uint                 m_progress_start;
      uint                 m_progress_range;

      uint                 m_num_blocks;
      const dxt_pixel_block*   m_pBlocks;

      dxt5_block*          m_pDst_elements;
      uint                 m_elements_per_block;
      qdxt5_params         m_params;

      uint                 m_max_selector_clusters;

      int                  m_prev_percentage_complete;

      typedef vec<2, float> vec2F;
      typedef clusterizer<vec2F> vec2F_clusterizer;
      vec2F_clusterizer    m_endpoint_clusterizer;

      crnlib::vector< crnlib::vector<uint> > m_endpoint_cluster_indices;

      typedef vec<16, float> vec16F;
      typedef threaded_clusterizer<vec16F> vec16F_clusterizer;

      typedef vec16F_clusterizer::weighted_vec weighted_selector_vec;
      typedef vec16F_clusterizer::weighted_vec_array weighted_selector_vec_array;

      vec16F_clusterizer m_selector_clusterizer;

      crnlib::vector< crnlib::vector<uint> > m_cached_selector_cluster_indices[qdxt5_params::cMaxQuality + 1];

      struct cluster_id
      {
         cluster_id() : m_hash(0)
         {

         }

         cluster_id(const crnlib::vector<uint>& indices)
         {
            set(indices);
         }

         void set(const crnlib::vector<uint>& indices)
         {
            m_cells.resize(indices.size());

            for (uint i = 0; i < indices.size(); i++)
               m_cells[i] = static_cast<uint32>(indices[i]);

            std::sort(m_cells.begin(), m_cells.end());

            m_hash = fast_hash(&m_cells[0], sizeof(m_cells[0]) * m_cells.size());
         }

         bool operator< (const cluster_id& rhs) const
         {
            return m_cells < rhs.m_cells;
         }

         bool operator== (const cluster_id& rhs) const
         {
            if (m_hash != rhs.m_hash)
               return false;

            return m_cells == rhs.m_cells;
         }

         crnlib::vector<uint32> m_cells;

         size_t m_hash;

         operator size_t() const { return m_hash; }
      };

      typedef crnlib::hash_map<cluster_id, uint> cluster_hash;
      cluster_hash m_cluster_hash;
      spinlock m_cluster_hash_lock;

      static bool generate_codebook_dummy_progress_callback(uint percentage_completed, void* pData);
      static bool generate_codebook_progress_callback(uint percentage_completed, void* pData);
      bool update_progress(uint value, uint max_value);
      void pack_endpoints_task(uint64 data, void* pData_ptr);
      void optimize_selectors_task(uint64 data, void* pData_ptr);
      bool create_selector_clusters(uint max_selector_clusters, crnlib::vector< crnlib::vector<uint> >& selector_cluster_indices);

      inline dxt5_block& get_block(uint index) const { return m_pDst_elements[index * m_elements_per_block]; }
   };

} // namespace crnlib










