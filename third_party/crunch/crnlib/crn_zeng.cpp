// File: crn_zeng.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
// Modified Zeng's technique for codebook/palette reordering
// Evaluation of some reordering techniques for image VQ index compression, António R. C. Paiva , O J. Pinho
// http://citeseerx.ist.psu.edu/viewdoc/summary?doi=10.1.1.88.7221 
#include "crn_core.h"
#include "crn_zeng.h"
#include "crn_sparse_array.h"
#include <deque>

#define USE_SPARSE_ARRAY 1

namespace crnlib
{
#if USE_SPARSE_ARRAY         
   typedef sparse_array<uint, 4> hist_type;
#else   
   typedef crnlib::vector<uint> hist_type;
#endif   
   
   static inline void update_hist(hist_type& hist, int i, int j, int n)
   {  
      if (i == j)
         return;
         
      if ((i != -1) && (j != -1) && (i < j))
      {  
         CRNLIB_ASSERT( (i >= 0) && (i < (int)n) );
         CRNLIB_ASSERT( (j >= 0) && (j < (int)n) );

         uint index = i * n + j;
                  
#if USE_SPARSE_ARRAY         
         uint freq = hist[index];
         freq++;
         hist.set(index, freq);
#else
         hist[index]++;         
#endif         
      }
   }
   
   static inline uint read_hist(hist_type& hist, int i, int j, int n)
   {
      if (i > j)
         utils::swap(i, j);
      
      return hist[i * n + j];
   }
         
   void create_zeng_reorder_table(uint n, uint num_indices, const uint* pIndices, crnlib::vector<uint>& remap_table, zeng_similarity_func pFunc, void* pContext, float similarity_func_weight)
   {
      CRNLIB_ASSERT((n > 0) && (num_indices > 0));
      CRNLIB_ASSERT_CLOSED_RANGE(similarity_func_weight, 0.0f, 1.0f);
      
//      printf("create_zeng_reorder_table start:\n");

      remap_table.clear();
      remap_table.resize(n);

      if (num_indices <= 1)
         return;

      const uint t = n * n;
      hist_type xhist(t);

      for (uint i = 0; i < num_indices; i++)
      {
         const int prev_val = (i > 0) ? pIndices[i-1] : -1;
         const int cur_val = pIndices[i];
         const int next_val = (i < (num_indices - 1)) ? pIndices[i+1] : -1;

         update_hist(xhist, cur_val, prev_val, n);
         update_hist(xhist, cur_val, next_val, n);
      }

#if 0      
      uint total1 = 0, total2 = 0;
      for (uint i = 0; i < n; i++)
      {
         for (uint j = 0; j < n; j++)
         {
            if (i == j)
               continue;
               
            //uint a = hist[i * n + j];
            //total1 += a;
            
            uint c = read_hist(xhist, i, j, n);
            total2 += c;
         }
      }
      
      printf("%u %u\n", total1, total2);
#endif      

      uint max_freq = 0;
      uint max_index = 0;
      for (uint i = 0; i < t; i++)
      {
         if (xhist[i] > max_freq)
         {
            max_freq = xhist[i];
            max_index = i;
         }
      }

      uint x = max_index / n;
      uint y = max_index % n;

      crnlib::vector<uint16> values_chosen; 
      values_chosen.reserve(n);
      
      values_chosen.push_back(static_cast<uint16>(x));
      values_chosen.push_back(static_cast<uint16>(y));

      crnlib::vector<uint16> values_remaining;
      if (n > 2)
         values_remaining.reserve(n - 2);
      for (uint i = 0; i < n; i++)
         if ((i != x) && (i != y))
            values_remaining.push_back(static_cast<uint16>(i));

      crnlib::vector<uint> total_freq_to_chosen_values(n);
      for (uint i = 0; i < values_remaining.size(); i++)
      {
         uint u = values_remaining[i];

         uint total_freq = 0;

         for (uint j = 0; j < values_chosen.size(); j++)
         {
            uint l = values_chosen[j];

            total_freq += read_hist(xhist, u, l, n); //[u * n + l];
         }

         total_freq_to_chosen_values[u] = total_freq;
      }

      while (!values_remaining.empty())
      {
         double best_freq = 0;
         uint best_i = 0;
         
         for (uint i = 0; i < values_remaining.size(); i++)
         {
            uint u = values_remaining[i];

   #if 0
            double total_freq = 0;

            for (uint j = 0; j < values_chosen.size(); j++)
            {
               uint l = values_chosen[j];

               total_freq += read_hist(xhist, u, l, n); //[u * n + l];
            }

            CRNLIB_ASSERT(total_freq_to_chosen_values[u] == total_freq);
   #else
            double total_freq = total_freq_to_chosen_values[u];
   #endif         
   
            if (pFunc)
            {
               float weight = math::maximum<float>(
                  (*pFunc)(u, values_chosen.front(), pContext), 
                  (*pFunc)(u, values_chosen.back(), pContext) );
                  
               CRNLIB_ASSERT_CLOSED_RANGE(weight, 0.0f, 1.0f);
               
               weight = math::lerp(1.0f - similarity_func_weight, 1.0f + similarity_func_weight, weight);
                                          
               total_freq = (total_freq + 1.0f) * weight;
            }
            
            if (total_freq > best_freq)
            {
               best_freq = total_freq;
               best_i = i;  
            }            
         }

         const uint u = values_remaining[best_i];

         float side = 0;
         int left_freq = 0;
         int right_freq = 0;

         for (uint j = 0; j < values_chosen.size(); j++)
         {
            const uint l = values_chosen[j];

            int freq = read_hist(xhist, u, l, n); //[u * n + l];
            int scale = (values_chosen.size() + 1 - 2 * (j + 1));

            side = side + (float)(scale * freq);

            if (scale < 0)
               right_freq += -scale * freq;
            else
               left_freq += scale * freq;
         }

         if (pFunc)
         {
            float weight_left = (*pFunc)(u, values_chosen.front(), pContext);
            float weight_right = (*pFunc)(u, values_chosen.back(), pContext);
            
            weight_left = math::lerp(1.0f - similarity_func_weight, 1.0f + similarity_func_weight, weight_left);
            weight_right = math::lerp(1.0f - similarity_func_weight, 1.0f + similarity_func_weight, weight_right);
            
            side = weight_left * left_freq - weight_right * right_freq;
         }

         if (side > 0)
            values_chosen.push_front(static_cast<uint16>(u));
         else
            values_chosen.push_back(static_cast<uint16>(u));

         values_remaining.erase(values_remaining.begin() + best_i);

         for (uint i = 0; i < values_remaining.size(); i++)
         {
            const uint r = values_remaining[i];

            total_freq_to_chosen_values[r] += read_hist(xhist, r, u, n); //[r * n + u];
         }
      }

      for (uint i = 0; i < n; i++)   
      {
         uint v = values_chosen[i];
         remap_table[v] = i;
      }

   #if 0
      uint before_sum = 0;
      uint after_sum = 0;
      {
         printf("\nBEFORE:\n");
         crnlib::vector<uint> delta_hist(n*2);

         int sum = 0;
         for (uint i = 1; i < num_indices; i++)
         {
            int prev = pIndices[i-1];
            int cur = pIndices[i];
            delta_hist[prev-cur+n]++;
            sum += labs(prev-cur);
         }

         printf("\n");
         for (uint i = 0; i < n*2; i++)   
            printf("%04u ", delta_hist[i]);

         printf("\nSum: %i\n", sum);
         before_sum = sum;
      }      

      {
         printf("AFTER:\n");
         crnlib::vector<uint> delta_hist(n*2);

         int sum = 0;
         for (uint i = 1; i < num_indices; i++)
         {
            int prev = remap_table[pIndices[i-1]];
            int cur = remap_table[pIndices[i]];
            delta_hist[prev-cur+n]++;
            sum += labs(prev-cur);
         }

         printf("\n");
         for (uint i = 0; i < n*2; i++)   
            printf("%04u ", delta_hist[i]);

         printf("\nSum: %i\n", sum);
         after_sum = sum;
      }      
      printf("Before sum: %u, After sum: %u\n", before_sum, after_sum);
   #endif   
   
//      printf("create_zeng_reorder_table end:\n");
   }

} // namespace crnlib
   
