// File: crn_huffman_codes.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_huffman_codes.h"

namespace crnlib
{
   struct sym_freq
   {
      uint m_freq;
      uint16 m_left;
      uint16 m_right;

      inline bool operator< (const sym_freq& other) const
      {
         return m_freq > other.m_freq;
      }
   };
   
   static inline sym_freq* radix_sort_syms(uint num_syms, sym_freq* syms0, sym_freq* syms1)
   {  
      const uint cMaxPasses = 2;
      uint hist[256 * cMaxPasses];
            
      memset(hist, 0, sizeof(hist[0]) * 256 * cMaxPasses);

      sym_freq* p = syms0;
      sym_freq* q = syms0 + (num_syms >> 1) * 2;

      for ( ; p != q; p += 2)
      {
         const uint freq0 = p[0].m_freq;
         const uint freq1 = p[1].m_freq;

         hist[        freq0         & 0xFF]++;
         hist[256 + ((freq0 >>  8) & 0xFF)]++;

         hist[        freq1        & 0xFF]++;
         hist[256 + ((freq1 >>  8) & 0xFF)]++;
      }

      if (num_syms & 1)
      {
         const uint freq = p->m_freq;

         hist[        freq        & 0xFF]++;
         hist[256 + ((freq >>  8) & 0xFF)]++;
      }
      
      sym_freq* pCur_syms = syms0;
      sym_freq* pNew_syms = syms1;

      for (uint pass = 0; pass < cMaxPasses; pass++)
      {
         const uint* pHist = &hist[pass << 8];

         uint offsets[256];

         uint cur_ofs = 0;
         for (uint i = 0; i < 256; i += 2)
         {
            offsets[i] = cur_ofs;
            cur_ofs += pHist[i];

            offsets[i+1] = cur_ofs;
            cur_ofs += pHist[i+1];
         }

         const uint pass_shift = pass << 3;

         sym_freq* p = pCur_syms;
         sym_freq* q = pCur_syms + (num_syms >> 1) * 2;

         for ( ; p != q; p += 2)
         {
            uint c0 = p[0].m_freq;
            uint c1 = p[1].m_freq;
            
            if (pass)
            {
               c0 >>= 8;
               c1 >>= 8;
            }
            
            c0 &= 0xFF;
            c1 &= 0xFF;

            if (c0 == c1)
            {
               uint dst_offset0 = offsets[c0];

               offsets[c0] = dst_offset0 + 2;

               pNew_syms[dst_offset0] = p[0];
               pNew_syms[dst_offset0 + 1] = p[1];
            }
            else
            {
               uint dst_offset0 = offsets[c0]++;
               uint dst_offset1 = offsets[c1]++;

               pNew_syms[dst_offset0] = p[0];
               pNew_syms[dst_offset1] = p[1];
            }
         }

         if (num_syms & 1)
         {
            uint c = ((p->m_freq) >> pass_shift) & 0xFF;

            uint dst_offset = offsets[c];
            offsets[c] = dst_offset + 1;

            pNew_syms[dst_offset] = *p;
         }

         sym_freq* t = pCur_syms;
         pCur_syms = pNew_syms;
         pNew_syms = t;
      }            

#ifdef CRNLIB_ASSERTS_ENABLED
      uint prev_freq = 0;
      for (uint i = 0; i < num_syms; i++)
      {
         CRNLIB_ASSERT(!(pCur_syms[i].m_freq < prev_freq));
         prev_freq = pCur_syms[i].m_freq;
      }
#endif
      
      return pCur_syms;
   }
   
   struct huffman_work_tables
   {
      enum { cMaxInternalNodes = cHuffmanMaxSupportedSyms };
      
      sym_freq syms0[cHuffmanMaxSupportedSyms + 1 + cMaxInternalNodes];
      sym_freq syms1[cHuffmanMaxSupportedSyms + 1 + cMaxInternalNodes];
                  
      uint16 queue[cMaxInternalNodes];
   };
   
   void* create_generate_huffman_codes_tables()
   {
      return crnlib_new<huffman_work_tables>();
   }
   
   void free_generate_huffman_codes_tables(void* p)
   {
      crnlib_delete(static_cast<huffman_work_tables*>(p)); 
   }

#if USE_CALCULATE_MINIMUM_REDUNDANCY      
   /* calculate_minimum_redundancy() written by
      Alistair Moffat, alistair@cs.mu.oz.au,
      Jyrki Katajainen, jyrki@diku.dk
      November 1996.
   */
   static void calculate_minimum_redundancy(int A[], int n) {
         int root;                  /* next root node to be used */
         int leaf;                  /* next leaf to be used */
         int next;                  /* next value to be assigned */
         int avbl;                  /* number of available nodes */
         int used;                  /* number of internal nodes */
         int dpth;                  /* current depth of leaves */

         /* check for pathological cases */
         if (n==0) { return; }
         if (n==1) { A[0] = 0; return; }

         /* first pass, left to right, setting parent pointers */
         A[0] += A[1]; root = 0; leaf = 2;
         for (next=1; next < n-1; next++) {
            /* select first item for a pairing */
            if (leaf>=n || A[root]<A[leaf]) {
               A[next] = A[root]; A[root++] = next;
            } else
               A[next] = A[leaf++];

            /* add on the second item */
            if (leaf>=n || (root<next && A[root]<A[leaf])) {
               A[next] += A[root]; A[root++] = next;
            } else
               A[next] += A[leaf++];
         }

         /* second pass, right to left, setting internal depths */
         A[n-2] = 0;
         for (next=n-3; next>=0; next--)
            A[next] = A[A[next]]+1;

         /* third pass, right to left, setting leaf depths */
         avbl = 1; used = dpth = 0; root = n-2; next = n-1;
         while (avbl>0) {
            while (root>=0 && A[root]==dpth) {
               used++; root--;
            }
            while (avbl>used) {
               A[next--] = dpth; avbl--;
            }
            avbl = 2*used; dpth++; used = 0;
         }
   }
#endif
     
   bool generate_huffman_codes(void* pContext, uint num_syms, const uint16* pFreq, uint8* pCodesizes, uint& max_code_size, uint& total_freq_ret)
   {
      if ((!num_syms) || (num_syms > cHuffmanMaxSupportedSyms))
         return false;
                  
      huffman_work_tables& state = *static_cast<huffman_work_tables*>(pContext);;
            
      uint max_freq = 0;
      uint total_freq = 0;
      
      uint num_used_syms = 0;
      for (uint i = 0; i < num_syms; i++)
      {
         uint freq = pFreq[i];
         
         if (!freq)
            pCodesizes[i] = 0;
         else
         {
            total_freq += freq;
            max_freq = math::maximum(max_freq, freq);
            
            sym_freq& sf = state.syms0[num_used_syms];
            sf.m_left = (uint16)i;
            sf.m_right = cUINT16_MAX;
            sf.m_freq = freq;
            num_used_syms++;
         }            
      }
      
      total_freq_ret = total_freq;

      if (num_used_syms == 1)
      {
         pCodesizes[state.syms0[0].m_left] = 1;
         return true;
      }

      sym_freq* syms = radix_sort_syms(num_used_syms, state.syms0, state.syms1);

#if USE_CALCULATE_MINIMUM_REDUNDANCY
      int x[cHuffmanMaxSupportedSyms];
      for (uint i = 0; i < num_used_syms; i++)
         x[i] = state.syms0[i].m_freq;
      
      calculate_minimum_redundancy(x, num_used_syms);
      
      uint max_len = 0;
      for (uint i = 0; i < num_used_syms; i++)
      {
         uint len = x[i];
         max_len = math::maximum(len, max_len);
         pCodesizes[state.syms0[i].m_left] = static_cast<uint8>(len);
      }
      
      return true;
#else    
      // Dummy node
      sym_freq& sf = state.syms0[num_used_syms];
      sf.m_left = cUINT16_MAX;
      sf.m_right = cUINT16_MAX;
      sf.m_freq = UINT_MAX;
      
      uint next_internal_node = num_used_syms + 1;
            
      uint queue_front = 0;
      uint queue_end = 0;
            
      uint next_lowest_sym = 0;
      
      uint num_nodes_remaining = num_used_syms;
      do
      {
         uint left_freq = syms[next_lowest_sym].m_freq;
         uint left_child = next_lowest_sym;
         
         if ((queue_end > queue_front) && (syms[state.queue[queue_front]].m_freq < left_freq))
         {
            left_child = state.queue[queue_front];
            left_freq = syms[left_child].m_freq;
            
            queue_front++;
         }
         else
            next_lowest_sym++;
         
         uint right_freq = syms[next_lowest_sym].m_freq;
         uint right_child = next_lowest_sym;

         if ((queue_end > queue_front) && (syms[state.queue[queue_front]].m_freq < right_freq))
         {
            right_child = state.queue[queue_front];
            right_freq = syms[right_child].m_freq;
            
            queue_front++;
         }
         else
            next_lowest_sym++;
                        
         const uint internal_node_index = next_internal_node;
         next_internal_node++;

         CRNLIB_ASSERT(next_internal_node < CRNLIB_ARRAYSIZE(state.syms0));
         
         syms[internal_node_index].m_freq = left_freq + right_freq;
         syms[internal_node_index].m_left = static_cast<uint16>(left_child);
         syms[internal_node_index].m_right = static_cast<uint16>(right_child);
         
         CRNLIB_ASSERT(queue_end < huffman_work_tables::cMaxInternalNodes);
         state.queue[queue_end] = static_cast<uint16>(internal_node_index);
         queue_end++;
                  
         num_nodes_remaining--;
         
      } while (num_nodes_remaining > 1);
      
      CRNLIB_ASSERT(next_lowest_sym == num_used_syms);
      CRNLIB_ASSERT((queue_end - queue_front) == 1);
      
      uint cur_node_index = state.queue[queue_front];
      
      uint32* pStack = (syms == state.syms0) ? (uint32*)state.syms1 : (uint32*)state.syms0;
      uint32* pStack_top = pStack;

      uint max_level = 0;
      
      for ( ; ; ) 
      {
         uint level = cur_node_index >> 16;
         uint node_index = cur_node_index & 0xFFFF;
         
         uint left_child = syms[node_index].m_left;
         uint right_child = syms[node_index].m_right;
         
         uint next_level = (cur_node_index + 0x10000) & 0xFFFF0000;
                           
         if (left_child < num_used_syms)
         {
            max_level = math::maximum(max_level, level);
            
            pCodesizes[syms[left_child].m_left] = static_cast<uint8>(level + 1);
            
            if (right_child < num_used_syms)
            {
               pCodesizes[syms[right_child].m_left] = static_cast<uint8>(level + 1);
               
               if (pStack == pStack_top) break;
               cur_node_index = *--pStack;
            }
            else
            {
               cur_node_index = next_level | right_child;
            }
         }
         else
         {
            if (right_child < num_used_syms)
            {
               max_level = math::maximum(max_level, level);
               
               pCodesizes[syms[right_child].m_left] = static_cast<uint8>(level + 1);
                              
               cur_node_index = next_level | left_child;
            }
            else
            {
               *pStack++ = next_level | left_child;
                              
               cur_node_index = next_level | right_child;
            }
         }
      }
      
      max_code_size = max_level + 1;
#endif
                  
      return true;
   }

} // namespace crnlib

