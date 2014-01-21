// File: crn_clusterizer.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_matrix.h"

namespace crnlib
{
   template<typename VectorType>
   class clusterizer
   {
   public:
      clusterizer() :
         m_overall_variance(0.0f),
         m_split_index(0),
         m_heap_size(0),
         m_quick(false)
      {
      }

      void clear()
      {
         m_training_vecs.clear();
         m_codebook.clear();
         m_nodes.clear();
         m_overall_variance = 0.0f;
         m_split_index = 0;
         m_heap_size = 0;
         m_quick = false;
      }

      void reserve_training_vecs(uint num_expected)
      {
         m_training_vecs.reserve(num_expected);
      }

      void add_training_vec(const VectorType& v, uint weight)
      {
         m_training_vecs.push_back( std::make_pair(v, weight) );
      }

      typedef bool (*progress_callback_func_ptr)(uint percentage_completed, void* pData);

      bool generate_codebook(uint max_size, progress_callback_func_ptr pProgress_callback = NULL, void* pProgress_data = NULL, bool quick = false)
      {
         if (m_training_vecs.empty())
            return false;

         m_quick = quick;

         double ttsum = 0.0f;

         vq_node root;
         root.m_vectors.reserve(m_training_vecs.size());

         for (uint i = 0; i < m_training_vecs.size(); i++)
         {
            const VectorType& v = m_training_vecs[i].first;
            const uint weight = m_training_vecs[i].second;

            root.m_centroid += (v * (float)weight);
            root.m_total_weight += weight;
            root.m_vectors.push_back(i);

            ttsum += v.dot(v) * weight;
         }

         root.m_variance = (float)(ttsum - (root.m_centroid.dot(root.m_centroid) / root.m_total_weight));

         root.m_centroid *= (1.0f / root.m_total_weight);

         m_nodes.clear();
         m_nodes.reserve(max_size * 2 + 1);

         m_nodes.push_back(root);

         m_heap.resize(max_size + 1);
         m_heap[1] = 0;
         m_heap_size = 1;

         m_split_index = 0;

         uint total_leaves = 1;

         m_left_children.reserve(m_training_vecs.size() + 1);
         m_right_children.reserve(m_training_vecs.size() + 1);

         int prev_percentage = -1;
         while ((total_leaves < max_size) && (m_heap_size))
         {
            int worst_node_index = m_heap[1];

            m_heap[1] = m_heap[m_heap_size];
            m_heap_size--;
            if (m_heap_size)
               down_heap(1);

            split_node(worst_node_index);
            total_leaves++;

            if ((pProgress_callback) && ((total_leaves & 63) == 0) && (max_size))
            {
               int cur_percentage = (total_leaves * 100U + (max_size / 2U)) / max_size;
               if (cur_percentage != prev_percentage)
               {
                  if (!(*pProgress_callback)(cur_percentage, pProgress_data))
                     return false;

                  prev_percentage = cur_percentage;
               }
            }
         }

         m_codebook.clear();

         m_overall_variance = 0.0f;

         for (uint i = 0; i < m_nodes.size(); i++)
         {
            vq_node& node = m_nodes[i];
            if (node.m_left != -1)
            {
               CRNLIB_ASSERT(node.m_right != -1);
               continue;
            }

            CRNLIB_ASSERT((node.m_left == -1) && (node.m_right == -1));

            node.m_codebook_index = m_codebook.size();
            m_codebook.push_back(node.m_centroid);

            m_overall_variance += node.m_variance;
         }

         m_heap.clear();
         m_left_children.clear();
         m_right_children.clear();

         return true;
      }

      inline uint get_num_training_vecs() const { return m_training_vecs.size(); }
      const VectorType& get_training_vec(uint index) const { return m_training_vecs[index].first; }
      uint get_training_vec_weight(uint index) const { return m_training_vecs[index].second; }

      typedef crnlib::vector< std::pair<VectorType, uint> > training_vec_array;

      const training_vec_array& get_training_vecs() const   { return m_training_vecs; }
            training_vec_array& get_training_vecs()         { return m_training_vecs; }

      inline float get_overall_variance() const { return m_overall_variance; }

      inline uint get_codebook_size() const
      {
         return m_codebook.size();
      }

      inline const VectorType& get_codebook_entry(uint index) const
      {
         return m_codebook[index];
      }

      VectorType& get_codebook_entry(uint index)
      {
         return m_codebook[index];
      }

      typedef crnlib::vector<VectorType> vector_vec_type;
      inline const vector_vec_type& get_codebook() const
      {
         return m_codebook;
      }

      uint find_best_codebook_entry(const VectorType& v) const
      {
         uint cur_node_index = 0;

         for ( ; ; )
         {
            const vq_node& cur_node = m_nodes[cur_node_index];

            if (cur_node.m_left == -1)
               return cur_node.m_codebook_index;

            const vq_node& left_node = m_nodes[cur_node.m_left];
            const vq_node& right_node = m_nodes[cur_node.m_right];

            float left_dist = left_node.m_centroid.squared_distance(v);
            float right_dist = right_node.m_centroid.squared_distance(v);

            if (left_dist < right_dist)
               cur_node_index = cur_node.m_left;
            else
               cur_node_index = cur_node.m_right;
         }
      }

      const VectorType& find_best_codebook_entry(const VectorType& v, uint max_codebook_size) const
      {
         uint cur_node_index = 0;

         for ( ; ; )
         {
            const vq_node& cur_node = m_nodes[cur_node_index];

            if ((cur_node.m_left == -1) || ((cur_node.m_codebook_index + 1) >= (int)max_codebook_size))
               return cur_node.m_centroid;

            const vq_node& left_node = m_nodes[cur_node.m_left];
            const vq_node& right_node = m_nodes[cur_node.m_right];

            float left_dist = left_node.m_centroid.squared_distance(v);
            float right_dist = right_node.m_centroid.squared_distance(v);

            if (left_dist < right_dist)
               cur_node_index = cur_node.m_left;
            else
               cur_node_index = cur_node.m_right;
         }
      }

      uint find_best_codebook_entry_fs(const VectorType& v) const
      {
         float best_dist = math::cNearlyInfinite;
         uint best_index = 0;

         for (uint i = 0; i < m_codebook.size(); i++)
         {
            float dist = m_codebook[i].squared_distance(v);
            if (dist < best_dist)
            {
               best_dist = dist;
               best_index = i;
               if (best_dist == 0.0f)
                  break;
            }
         }

         return best_index;
      }

      void retrieve_clusters(uint max_clusters, crnlib::vector< crnlib::vector<uint> >& clusters) const
      {
         clusters.resize(0);
         clusters.reserve(max_clusters);

         crnlib::vector<uint> stack;
         stack.reserve(512);

         uint cur_node_index = 0;

         for ( ; ; )
         {
            const vq_node& cur_node = m_nodes[cur_node_index];

            if ( (cur_node.is_leaf()) || ((cur_node.m_codebook_index + 2) > (int)max_clusters) )
            {
               clusters.resize(clusters.size() + 1);
               clusters.back() = cur_node.m_vectors;

               if (stack.empty())
                  break;
               cur_node_index = stack.back();
               stack.pop_back();
               continue;
            }

            cur_node_index = cur_node.m_left;
            stack.push_back(cur_node.m_right);
         }
      }

   private:
      training_vec_array m_training_vecs;

      struct vq_node
      {
         vq_node() : m_centroid(cClear), m_total_weight(0), m_left(-1), m_right(-1), m_codebook_index(-1), m_unsplittable(false) { }

         VectorType        m_centroid;
         uint64            m_total_weight;

         float             m_variance;

         crnlib::vector<uint> m_vectors;

         int               m_left;
         int               m_right;

         int               m_codebook_index;

         bool              m_unsplittable;

         bool is_leaf() const { return m_left < 0; }
      };

      typedef crnlib::vector<vq_node> node_vec_type;

      node_vec_type m_nodes;

      vector_vec_type m_codebook;

      float m_overall_variance;

      uint m_split_index;

      crnlib::vector<uint> m_heap;
      uint m_heap_size;

      bool m_quick;

      void insert_heap(uint node_index)
      {
         const float variance = m_nodes[node_index].m_variance;
         uint pos = ++m_heap_size;

         if (m_heap_size >= m_heap.size())
            m_heap.resize(m_heap_size + 1);

         for ( ; ; )
         {
            uint parent = pos >> 1;
            if (!parent)
               break;

            float parent_variance = m_nodes[m_heap[parent]].m_variance;
            if (parent_variance > variance)
               break;

            m_heap[pos] = m_heap[parent];

            pos = parent;
         }

         m_heap[pos] = node_index;
      }

      void down_heap(uint pos)
      {
         uint child;
         uint orig = m_heap[pos];

         const float orig_variance = m_nodes[orig].m_variance;

         while ((child = (pos << 1)) <= m_heap_size)
         {
            if (child < m_heap_size)
            {
               if (m_nodes[m_heap[child]].m_variance < m_nodes[m_heap[child + 1]].m_variance)
                  child++;
            }

            if (orig_variance > m_nodes[m_heap[child]].m_variance)
               break;

            m_heap[pos] = m_heap[child];

            pos = child;
         }

         m_heap[pos] = orig;
      }

      void compute_split_estimate(VectorType& left_child_res, VectorType& right_child_res, const vq_node& parent_node)
      {
         VectorType furthest(0);
         double furthest_dist = -1.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;

            double dist = v.squared_distance(parent_node.m_centroid);
            if (dist > furthest_dist)
            {
               furthest_dist = dist;
               furthest = v;
            }
         }

         VectorType opposite(0);
         double opposite_dist = -1.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;

            double dist = v.squared_distance(furthest);
            if (dist > opposite_dist)
            {
               opposite_dist = dist;
               opposite = v;
            }
         }

         left_child_res = (furthest + parent_node.m_centroid) * .5f;
         right_child_res = (opposite + parent_node.m_centroid) * .5f;
      }

      void compute_split_pca(VectorType& left_child_res, VectorType& right_child_res, const vq_node& parent_node)
      {
         if (parent_node.m_vectors.size() == 2)
         {
            left_child_res = m_training_vecs[parent_node.m_vectors[0]].first;
            right_child_res = m_training_vecs[parent_node.m_vectors[1]].first;
            return;
         }

         const uint N = VectorType::num_elements;

         matrix<N, N, float> covar;
         covar.clear();

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const VectorType v(m_training_vecs[parent_node.m_vectors[i]].first - parent_node.m_centroid);
            const VectorType w(v * (float)m_training_vecs[parent_node.m_vectors[i]].second);

            for (uint x = 0; x < N; x++)
               for (uint y = x; y < N; y++)
                  covar[x][y] = covar[x][y] + v[x] * w[y];
         }

         float one_over_total_weight = 1.0f / parent_node.m_total_weight;

         for (uint x = 0; x < N; x++)
            for (uint y = x; y < N; y++)
               covar[x][y] *= one_over_total_weight;

         for (uint x = 0; x < (N - 1); x++)
            for (uint y = x + 1; y < N; y++)
               covar[y][x] = covar[x][y];

         VectorType axis;//(1.0f);
         if (N == 1)
            axis.set(1.0f);
         else
         {
            for (uint i = 0; i < N; i++)
               axis[i] = math::lerp(.75f, 1.25f, i * (1.0f / math::maximum<int>(N - 1, 1)));
         }

         VectorType prev_axis(axis);

         for (uint iter = 0; iter < 10; iter++)
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

         VectorType left_child(0.0f);
         VectorType right_child(0.0f);

         double left_weight = 0.0f;
         double right_weight = 0.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const float weight = (float)m_training_vecs[parent_node.m_vectors[i]].second;

            const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;

            double t = (v - parent_node.m_centroid) * axis;
            if (t < 0.0f)
            {
               left_child += v * weight;
               left_weight += weight;
            }
            else
            {
               right_child += v * weight;
               right_weight += weight;
            }
         }

         if ((left_weight > 0.0f) && (right_weight > 0.0f))
         {
            left_child_res = left_child * (float)(1.0f / left_weight);
            right_child_res = right_child * (float)(1.0f / right_weight);
         }
         else
         {
            compute_split_estimate(left_child_res, right_child_res, parent_node);
         }
      }

#if 0
      void compute_split_pca2(VectorType& left_child_res, VectorType& right_child_res, const vq_node& parent_node)
      {
         if (parent_node.m_vectors.size() == 2)
         {
            left_child_res = m_training_vecs[parent_node.m_vectors[0]].first;
            right_child_res = m_training_vecs[parent_node.m_vectors[1]].first;
            return;
         }

         const uint N = VectorType::num_elements;

         VectorType furthest;
         double furthest_dist = -1.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;

            double dist = v.squared_distance(parent_node.m_centroid);
            if (dist > furthest_dist)
            {
               furthest_dist = dist;
               furthest = v;
            }
         }

         VectorType opposite;
         double opposite_dist = -1.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;

            double dist = v.squared_distance(furthest);
            if (dist > opposite_dist)
            {
               opposite_dist = dist;
               opposite = v;
            }
         }

         VectorType axis(opposite - furthest);
         if (axis.normalize() < .000125f)
         {
            left_child_res = (furthest + parent_node.m_centroid) * .5f;
            right_child_res = (opposite + parent_node.m_centroid) * .5f;
            return;
         }

         for (uint iter = 0; iter < 2; iter++)
         {
            double next_axis[N];
            utils::zero_object(next_axis);

            for (uint i = 0; i < parent_node.m_vectors.size(); i++)
            {
               const double weight = m_training_vecs[parent_node.m_vectors[i]].second;

               VectorType v(m_training_vecs[parent_node.m_vectors[i]].first - parent_node.m_centroid);

               double dot = (v * axis) * weight;

               for (uint j = 0; j < N; j++)
                  next_axis[j] += dot * v[j];
            }

            double w = 0.0f;
            for (uint j = 0; j < N; j++)
               w += next_axis[j] * next_axis[j];

            if (w > 0.0f)
            {
               w = 1.0f / sqrt(w);
               for (uint j = 0; j < N; j++)
                  axis[j] = static_cast<float>(next_axis[j] * w);
            }
            else
               break;
         }

         VectorType left_child(0.0f);
         VectorType right_child(0.0f);

         double left_weight = 0.0f;
         double right_weight = 0.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const float weight = (float)m_training_vecs[parent_node.m_vectors[i]].second;

            const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;

            double t = (v - parent_node.m_centroid) * axis;
            if (t < 0.0f)
            {
               left_child += v * weight;
               left_weight += weight;
            }
            else
            {
               right_child += v * weight;
               right_weight += weight;
            }
         }

         if ((left_weight > 0.0f) && (right_weight > 0.0f))
         {
            left_child_res = left_child * (float)(1.0f / left_weight);
            right_child_res = right_child * (float)(1.0f / right_weight);
         }
         else
         {
            left_child_res = (furthest + parent_node.m_centroid) * .5f;
            right_child_res = (opposite + parent_node.m_centroid) * .5f;
         }
      }
#endif

      // thread safety warning: shared state!
      crnlib::vector<uint> m_left_children;
      crnlib::vector<uint> m_right_children;

      void split_node(uint index)
      {
         vq_node& parent_node = m_nodes[index];

         if (parent_node.m_vectors.size() == 1)
            return;

         VectorType left_child, right_child;
         if (m_quick)
            compute_split_estimate(left_child, right_child, parent_node);
         else
            compute_split_pca(left_child, right_child, parent_node);

         uint64 left_weight = 0;
         uint64 right_weight = 0;

         float prev_total_variance = 1e+10f;

         float left_variance = 0.0f;
         float right_variance = 0.0f;

         const uint cMaxLoops = m_quick ? 2 : 8;
         for (uint total_loops = 0; total_loops < cMaxLoops; total_loops++)
         {
            m_left_children.resize(0);
            m_right_children.resize(0);

            VectorType new_left_child(cClear);
            VectorType new_right_child(cClear);

            double left_ttsum = 0.0f;
            double right_ttsum = 0.0f;

            left_weight = 0;
            right_weight = 0;

            for (uint i = 0; i < parent_node.m_vectors.size(); i++)
            {
               const VectorType& v = m_training_vecs[parent_node.m_vectors[i]].first;
               const uint weight = m_training_vecs[parent_node.m_vectors[i]].second;

               double left_dist2 = left_child.squared_distance(v);
               double right_dist2 = right_child.squared_distance(v);

               if (left_dist2 < right_dist2)
               {
                  m_left_children.push_back(parent_node.m_vectors[i]);

                  new_left_child += (v * (float)weight);
                  left_weight += weight;

                  left_ttsum += v.dot(v) * weight;
               }
               else
               {
                  m_right_children.push_back(parent_node.m_vectors[i]);

                  new_right_child += (v * (float)weight);
                  right_weight += weight;

                  right_ttsum += v.dot(v) * weight;
               }
            }

            if ((!left_weight) || (!right_weight))
            {
               parent_node.m_unsplittable = true;
               return;
            }

            left_variance = (float)(left_ttsum - (new_left_child.dot(new_left_child) / left_weight));
            right_variance = (float)(right_ttsum - (new_right_child.dot(new_right_child) / right_weight));

            new_left_child *= (1.0f / left_weight);
            new_right_child *= (1.0f / right_weight);

            left_child = new_left_child;
            left_weight = left_weight;

            right_child = new_right_child;
            right_weight = right_weight;

            float total_variance = left_variance + right_variance;
            if (total_variance < .00001f)
               break;

            //const float variance_delta_thresh = .00001f;
            const float variance_delta_thresh = .00125f;
            if (((prev_total_variance - total_variance) / total_variance) < variance_delta_thresh)
               break;

            prev_total_variance = total_variance;
         }

         const uint left_child_index = m_nodes.size();
         const uint right_child_index = m_nodes.size() + 1;

         parent_node.m_left = m_nodes.size();
         parent_node.m_right = m_nodes.size() + 1;
         parent_node.m_codebook_index = m_split_index;
         m_split_index++;

         m_nodes.resize(m_nodes.size() + 2);

         // parent_node is invalid now, because m_nodes has been changed

         vq_node& left_child_node = m_nodes[left_child_index];
         vq_node& right_child_node = m_nodes[right_child_index];

         left_child_node.m_centroid = left_child;
         left_child_node.m_total_weight = left_weight;
         left_child_node.m_vectors.swap(m_left_children);
         left_child_node.m_variance = left_variance;
         if ((left_child_node.m_vectors.size() > 1) && (left_child_node.m_variance > 0.0f))
            insert_heap(left_child_index);

         right_child_node.m_centroid = right_child;
         right_child_node.m_total_weight = right_weight;
         right_child_node.m_vectors.swap(m_right_children);
         right_child_node.m_variance = right_variance;
         if ((right_child_node.m_vectors.size() > 1) && (right_child_node.m_variance > 0.0f))
            insert_heap(right_child_index);
      }

   };

} // namespace crnlib



