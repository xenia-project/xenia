// File: crn_tree_clusterizer.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_matrix.h"

namespace crnlib
{
   template<typename VectorType>
   class tree_clusterizer
   {
   public:
      tree_clusterizer() :
         m_overall_variance(0.0f)
      {
      }

      void clear()
      {
         m_hist.clear();
         m_codebook.clear();
         m_nodes.clear();
         m_overall_variance = 0.0f;
      }

      void add_training_vec(const VectorType& v, uint weight)
      {
         const std::pair<typename vector_map_type::iterator, bool> insert_result( m_hist.insert( std::make_pair(v, 0U) ) );

         typename vector_map_type::iterator it(insert_result.first);

         uint max_weight = UINT_MAX - weight;
         if (weight > max_weight)
            it->second = UINT_MAX;
         else
            it->second = it->second + weight;
      }

      bool generate_codebook(uint max_size)
      {
         if (m_hist.empty())
            return false;

         double ttsum = 0.0f;

         vq_node root;
         root.m_vectors.reserve(static_cast<uint>(m_hist.size()));

         for (typename vector_map_type::const_iterator it = m_hist.begin(); it != m_hist.end(); ++it)
         {
            const VectorType& v = it->first;
            const uint weight = it->second;

            root.m_centroid += (v * (float)weight);
            root.m_total_weight += weight;
            root.m_vectors.push_back( std::make_pair(v, weight) );

            ttsum += v.dot(v) * weight;
         }

         root.m_variance = (float)(ttsum - (root.m_centroid.dot(root.m_centroid) / root.m_total_weight));

         root.m_centroid *= (1.0f / root.m_total_weight);

         m_nodes.clear();
         m_nodes.reserve(max_size * 2 + 1);

         m_nodes.push_back(root);

         // Warning: if this code is NOT compiled with -fno-strict-aliasing, m_nodes.get_ptr() can be NULL here. (Argh!)

         uint total_leaves = 1;

         while (total_leaves < max_size)
         {
            int worst_node_index = -1;
            float worst_variance = -1.0f;

            for (uint i = 0; i < m_nodes.size(); i++)
            {
               vq_node& node = m_nodes[i];

               // Skip internal and unsplittable nodes.
               if ((node.m_left != -1) || (node.m_unsplittable))
                  continue;

               if (node.m_variance > worst_variance)
               {
                  worst_variance = node.m_variance;
                  worst_node_index = i;
               }
            }

            if (worst_variance <= 0.0f)
               break;

            split_node(worst_node_index);
            total_leaves++;
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

         return true;
      }

      inline float get_overall_variance() const { return m_overall_variance; }

      inline uint get_codebook_size() const
      {
         return m_codebook.size();
      }

      inline const VectorType& get_codebook_entry(uint index) const
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

   private:
      typedef std::map<VectorType, uint> vector_map_type;

      vector_map_type m_hist;

      struct vq_node
      {
         vq_node() : m_centroid(cClear), m_total_weight(0), m_left(-1), m_right(-1), m_codebook_index(-1), m_unsplittable(false) { }

         VectorType        m_centroid;
         uint64            m_total_weight;

         float             m_variance;

         crnlib::vector< std::pair<VectorType, uint> > m_vectors;

         int               m_left;
         int               m_right;

         int               m_codebook_index;

         bool              m_unsplittable;
      };

      typedef crnlib::vector<vq_node> node_vec_type;

      node_vec_type m_nodes;

      vector_vec_type m_codebook;

      float m_overall_variance;

      random m_rand;

      void split_node(uint index)
      {
         vq_node& parent_node = m_nodes[index];

         if (parent_node.m_vectors.size() == 1)
            return;

         VectorType furthest(0);
         double furthest_dist = -1.0f;

         for (uint i = 0; i < parent_node.m_vectors.size(); i++)
         {
            const VectorType& v = parent_node.m_vectors[i].first;

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
            const VectorType& v = parent_node.m_vectors[i].first;

            double dist = v.squared_distance(furthest);
            if (dist > opposite_dist)
            {
               opposite_dist = dist;
               opposite = v;
            }
         }

         VectorType left_child((furthest + parent_node.m_centroid) * .5f);
         VectorType right_child((opposite + parent_node.m_centroid) * .5f);

         if (parent_node.m_vectors.size() > 2)
         {
            const uint N = VectorType::num_elements;

            matrix<N, N, float> covar;
            covar.clear();

            for (uint i = 0; i < parent_node.m_vectors.size(); i++)
            {
               const VectorType v(parent_node.m_vectors[i].first - parent_node.m_centroid);
               const VectorType w(v * (float)parent_node.m_vectors[i].second);

               for (uint x = 0; x < N; x++)
                  for (uint y = x; y < N; y++)
                     covar[x][y] = covar[x][y] + v[x] * w[y];
            }

            if (N > 1)
            {
               //for (uint x = 0; x < (N - 1); x++)
               for (uint x = 0; x != (N - 1); x++)
                  for (uint y = x + 1; y < N; y++)
                     covar[y][x] = covar[x][y];
            }

            covar /= float(parent_node.m_total_weight);

            VectorType axis(1.0f);
            // Starting with an estimate of the principle axis should work better, but doesn't in practice?
            //left_child - right_child);
            //axis.normalize();

            for (uint iter = 0; iter < 10; iter++)
            {
               VectorType x;

               double max_sum = 0;

               for (uint i = 0; i < N; i++)
               {
                  double sum = 0;

                  for (uint j = 0; j < N; j++)
                     sum += axis[j] * covar[i][j];

                  x[i] = (float)sum;

                  max_sum = i ? math::maximum(max_sum, sum) : sum;
               }

               if (max_sum != 0.0f)
                  x *= (float)(1.0f / max_sum);

               axis = x;
            }

            axis.normalize();

            VectorType new_left_child(0.0f);
            VectorType new_right_child(0.0f);

            double left_weight = 0.0f;
            double right_weight = 0.0f;

            for (uint i = 0; i < parent_node.m_vectors.size(); i++)
            {
               const float weight = (float)parent_node.m_vectors[i].second;

               const VectorType& v = parent_node.m_vectors[i].first;

               double t = (v - parent_node.m_centroid) * axis;
               if (t < 0.0f)
               {
                  new_left_child += v * weight;
                  left_weight += weight;
               }
               else
               {
                  new_right_child += v * weight;
                  right_weight += weight;
               }
            }

            if ((left_weight > 0.0f) && (right_weight > 0.0f))
            {
               left_child = new_left_child * (float)(1.0f/left_weight);
               right_child = new_right_child * (float)(1.0f/right_weight);
            }
         }

         uint64 left_weight = 0;
         uint64 right_weight = 0;

         crnlib::vector< std::pair<VectorType, uint> > left_children;
         crnlib::vector< std::pair<VectorType, uint> > right_children;

         left_children.reserve(parent_node.m_vectors.size() / 2);
         right_children.reserve(parent_node.m_vectors.size() / 2);

         float prev_total_variance = 1e+10f;

         float left_variance = 0.0f;
         float right_variance = 0.0f;

         // FIXME: Excessive upper limit
         const uint cMaxLoops = 1024;
         for (uint total_loops = 0; total_loops < cMaxLoops; total_loops++)
         {
            left_children.resize(0);
            right_children.resize(0);

            VectorType new_left_child(cClear);
            VectorType new_right_child(cClear);

            double left_ttsum = 0.0f;
            double right_ttsum = 0.0f;

            left_weight = 0;
            right_weight = 0;

            for (uint i = 0; i < parent_node.m_vectors.size(); i++)
            {
               const VectorType& v = parent_node.m_vectors[i].first;
               const uint weight = parent_node.m_vectors[i].second;

               double left_dist2 = left_child.squared_distance(v);
               double right_dist2 = right_child.squared_distance(v);

               if (left_dist2 < right_dist2)
               {
                  left_children.push_back(parent_node.m_vectors[i]);

                  new_left_child += (v * (float)weight);
                  left_weight += weight;

                  left_ttsum += v.dot(v) * weight;
               }
               else
               {
                  right_children.push_back(parent_node.m_vectors[i]);

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

            if (((prev_total_variance - total_variance) / total_variance) < .00001f)
               break;

            prev_total_variance = total_variance;
         }

         const uint left_child_index = m_nodes.size();
         const uint right_child_index = m_nodes.size() + 1;

         parent_node.m_left = m_nodes.size();
         parent_node.m_right = m_nodes.size() + 1;

         m_nodes.resize(m_nodes.size() + 2);

         // parent_node is invalid now, because m_nodes has been changed

         vq_node& left_child_node = m_nodes[left_child_index];
         vq_node& right_child_node = m_nodes[right_child_index];

         left_child_node.m_centroid = left_child;
         left_child_node.m_total_weight = left_weight;
         left_child_node.m_vectors.swap(left_children);
         left_child_node.m_variance = left_variance;

         right_child_node.m_centroid = right_child;
         right_child_node.m_total_weight = right_weight;
         right_child_node.m_vectors.swap(right_children);
         right_child_node.m_variance = right_variance;
      }

   };

} // namespace crnlib

