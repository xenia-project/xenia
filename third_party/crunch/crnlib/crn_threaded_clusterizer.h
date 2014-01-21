// File: crn_threaded_clusterizer.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_clusterizer.h"
#include "crn_threading.h"

namespace crnlib
{
   template<typename VectorType>
   class threaded_clusterizer
   {
      CRNLIB_NO_COPY_OR_ASSIGNMENT_OP(threaded_clusterizer);

   public:
      threaded_clusterizer(task_pool& tp) :
         m_pTask_pool(&tp),
         m_pProgress_callback(NULL),
         m_pProgress_callback_data(NULL),
         m_canceled(false)
      {
      }

      void clear()
      {
         for (uint i = 0; i < cMaxClusterizers; i++)
            m_clusterizers[i].clear();
      }

      struct weighted_vec
      {
         weighted_vec() { }
         weighted_vec(const VectorType& v, uint w) : m_vec(v), m_weight(w) { }

         VectorType m_vec;
         uint m_weight;
      };
      typedef crnlib::vector<weighted_vec> weighted_vec_array;

      typedef bool (*progress_callback_func)(uint percentage_completed, void* pProgress_data);

      bool create_clusters(
         const weighted_vec_array& weighted_vecs,
         uint max_clusters, crnlib::vector< crnlib::vector<uint> >& cluster_indices,
         progress_callback_func pProgress_callback,
         void* pProgress_callback_data)
      {
         m_main_thread_id = crn_get_current_thread_id();
         m_canceled = false;
         m_pProgress_callback = pProgress_callback;
         m_pProgress_callback_data = pProgress_callback_data;

         if (max_clusters >= 128)
         {
            crnlib::vector<uint> primary_indices(weighted_vecs.size());
            for (uint i = 0; i < weighted_vecs.size(); i++)
               primary_indices[i] = i;

            CRNLIB_ASSUME(cMaxClusterizers == 4);

            crnlib::vector<uint> indices[6];

            compute_split(weighted_vecs, primary_indices, indices[0], indices[1]);
            compute_split(weighted_vecs, indices[0], indices[2], indices[3]);
            compute_split(weighted_vecs, indices[1], indices[4], indices[5]);

            create_clusters_task_state task_state[4];

            m_cluster_task_displayed_progress = false;

            uint total_partitions = 0;
            for (uint i = 0; i < 4; i++)
            {
               const uint num_indices = indices[2 + i].size();
               if (num_indices)
                  total_partitions++;
            }

            for (uint i = 0; i < 4; i++)
            {
               const uint num_indices = indices[2 + i].size();
               if (!num_indices)
                  continue;

               task_state[i].m_pWeighted_vecs = &weighted_vecs;
               task_state[i].m_pIndices = &indices[2 + i];
               task_state[i].m_max_clusters = (max_clusters + (total_partitions / 2)) / total_partitions;

               m_pTask_pool->queue_object_task(this, &threaded_clusterizer::create_clusters_task, i, &task_state[i]);
            }

            m_pTask_pool->join();

            if (m_canceled)
               return false;

            uint total_clusters = 0;
            for (uint i = 0; i < 4; i++)
               total_clusters += task_state[i].m_cluster_indices.size();

            cluster_indices.reserve(total_clusters);
            cluster_indices.resize(0);

            for (uint i = 0; i < 4; i++)
            {
               const uint ofs = cluster_indices.size();

               cluster_indices.resize(ofs + task_state[i].m_cluster_indices.size());

               for (uint j = 0; j < task_state[i].m_cluster_indices.size(); j++)
               {
                  cluster_indices[ofs + j].swap( task_state[i].m_cluster_indices[j] );
               }
            }
         }
         else
         {
            m_clusterizers[0].clear();
            m_clusterizers[0].get_training_vecs().reserve(weighted_vecs.size());

            for (uint i = 0; i < weighted_vecs.size(); i++)
            {
               const weighted_vec& v = weighted_vecs[i];

               m_clusterizers[0].add_training_vec(v.m_vec, v.m_weight);
            }

            m_clusterizers[0].generate_codebook(max_clusters, generate_codebook_progress_callback, this, false);//m_params.m_dxt_quality <= cCRNDXTQualityFast);

            const uint num_clusters = m_clusterizers[0].get_codebook_size();

            m_clusterizers[0].retrieve_clusters(num_clusters, cluster_indices);
         }

         return !m_canceled;
      }

   private:
      task_pool* m_pTask_pool;

      crn_thread_id_t m_main_thread_id;

      struct create_clusters_task_state
      {
         create_clusters_task_state() : m_pWeighted_vecs(NULL), m_pIndices(NULL), m_max_clusters(0)
         {
         }

         const weighted_vec_array*                 m_pWeighted_vecs;
         crnlib::vector<uint>*                     m_pIndices;
         crnlib::vector< crnlib::vector<uint> >    m_cluster_indices;
         uint                                      m_max_clusters;
      };

      typedef clusterizer<VectorType> vector_clusterizer;

      enum { cMaxClusterizers = 4 };
      vector_clusterizer m_clusterizers[cMaxClusterizers];
      bool m_cluster_task_displayed_progress;

      progress_callback_func m_pProgress_callback;
      void* m_pProgress_callback_data;
      bool m_canceled;

      static bool generate_codebook_progress_callback(uint percentage_completed, void* pData)
      {
         threaded_clusterizer* pClusterizer = static_cast<threaded_clusterizer*>(pData);

         if (!pClusterizer->m_pProgress_callback)
            return true;

         if (!pClusterizer->m_pProgress_callback(percentage_completed, pClusterizer->m_pProgress_callback_data))
         {
            pClusterizer->m_canceled = true;
            return false;
         }
         return true;
      }

      void compute_pca(VectorType& axis_res, VectorType& centroid_res, const weighted_vec_array& vecs, const vector<uint>& indices)
      {
         const uint N = VectorType::num_elements;

         VectorType centroid(0.0f);
         double total_weight = 0.0f;
         for (uint i = 0; i < indices.size(); i++)
         {
            const weighted_vec& v = vecs[indices[i]];
            centroid += v.m_vec * static_cast<float>(v.m_weight);
            total_weight += v.m_weight;
         }

         if (total_weight == 0.0f)
         {
            axis_res.clear();
            centroid_res = centroid;
            return;
         }

         double one_over_total_weight = 1.0f / total_weight;
         for (uint i = 0; i < N; i++)
            centroid[i] = static_cast<float>(centroid[i] * one_over_total_weight);

         matrix<N, N, float> covar;
         covar.clear();

         for (uint i = 0; i < indices.size(); i++)
         {
            const weighted_vec& weighted_vec = vecs[indices[i]];

            const VectorType v(weighted_vec.m_vec - centroid);
            const VectorType w(v * static_cast<float>(weighted_vec.m_weight));

            for (uint x = 0; x < N; x++)
               for (uint y = x; y < N; y++)
                  covar[x][y] = covar[x][y] + v[x] * w[y];
         }

         for (uint x = 0; x < N; x++)
            for (uint y = x; y < N; y++)
               covar[x][y] = static_cast<float>(covar[x][y] * one_over_total_weight);

         for (uint x = 0; x < (N - 1); x++)
            for (uint y = x + 1; y < N; y++)
               covar[y][x] = covar[x][y];

         VectorType axis;
         for (uint i = 0; i < N; i++)
            axis[i] = math::lerp(.75f, 1.25f, i * (1.0f / (N - 1)));

         VectorType prev_axis(axis);

         const uint cMaxIterations = 10;
         for (uint iter = 0; iter < cMaxIterations; iter++)
         {
            VectorType x;

            double max_sum = 0;

            for (uint i = 0; i < N; i++)
            {
               double sum = 0;

               for (uint j = 0; j < N; j++)
                  sum += axis[j] * covar[i][j];

               x[i] = static_cast<float>(sum);

               max_sum = math::maximum(max_sum, fabs(sum));
            }

            if (max_sum != 0.0f)
               x *= static_cast<float>(1.0f / max_sum);

            VectorType delta_axis(prev_axis - x);

            prev_axis = axis;
            axis = x;

            if (delta_axis.norm() < .0025f)
               break;
         }

         axis.normalize();

         axis_res = axis;
         centroid_res = centroid;
      }

      void compute_division(
         const VectorType& axis, const VectorType& centroid, const weighted_vec_array& vecs, const vector<uint>& indices,
         vector<uint>& left_indices,
         vector<uint>& right_indices)
      {
         left_indices.resize(0);
         right_indices.resize(0);

         for (uint i = 0; i < indices.size(); i++)
         {
            const uint vec_index = indices[i];
            const VectorType v(vecs[vec_index].m_vec - centroid);

            float t = v * axis;
            if (t < 0.0f)
               left_indices.push_back(vec_index);
            else
               right_indices.push_back(vec_index);
         }
      }

      void compute_split(
         const weighted_vec_array& vecs, const vector<uint>& indices,
         vector<uint>& left_indices,
         vector<uint>& right_indices)
      {
         VectorType axis, centroid;
         compute_pca(axis, centroid, vecs, indices);

         compute_division(axis, centroid, vecs, indices, left_indices, right_indices);
      }

      static bool generate_codebook_dummy_progress_callback(uint percentage_completed, void* pData)
      {
         percentage_completed;

         if (static_cast<threaded_clusterizer*>(pData)->m_canceled)
            return false;

         return true;
      }

      void create_clusters_task(uint64 data, void* pData_ptr)
      {
         if (m_canceled)
            return;

         const uint partition_index = static_cast<uint>(data);
         create_clusters_task_state& state = *static_cast<create_clusters_task_state*>(pData_ptr);

         m_clusterizers[partition_index].clear();

         for (uint i = 0; i < state.m_pIndices->size(); i++)
         {
            const uint index = (*state.m_pIndices)[i];
            const weighted_vec& v = (*state.m_pWeighted_vecs)[index];

            m_clusterizers[partition_index].add_training_vec(v.m_vec, v.m_weight);
         }

         if (m_canceled)
            return;

         const bool is_main_thread = (crn_get_current_thread_id() == m_main_thread_id);

         const bool quick = false;
         m_clusterizers[partition_index].generate_codebook(
            state.m_max_clusters,
            (is_main_thread && !m_cluster_task_displayed_progress) ? generate_codebook_progress_callback : generate_codebook_dummy_progress_callback,
            this,
            quick);

         if (is_main_thread)
            m_cluster_task_displayed_progress = true;

         if (m_canceled)
            return;

         const uint num_clusters = m_clusterizers[partition_index].get_codebook_size();

         m_clusterizers[partition_index].retrieve_clusters(num_clusters, state.m_cluster_indices);

         for (uint i = 0; i < state.m_cluster_indices.size(); i++)
         {
            crnlib::vector<uint>& indices = state.m_cluster_indices[i];

            for (uint j = 0; j < indices.size(); j++)
               indices[j] = (*state.m_pIndices)[indices[j]];
         }
      }

   };

} // namespace crnlib
