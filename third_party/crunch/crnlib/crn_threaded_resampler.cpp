// File: crn_threaded_resampler.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_threaded_resampler.h"
#include "crn_resample_filters.h"
#include "crn_threading.h"

namespace crnlib
{
   threaded_resampler::threaded_resampler(task_pool& tp) :
      m_pTask_pool(&tp),
      m_pParams(NULL),
      m_pX_contribs(NULL),
      m_pY_contribs(NULL),
      m_bytes_per_pixel(0)
   {
   }

   threaded_resampler::~threaded_resampler()
   {
      free_contrib_lists();
   }

   void threaded_resampler::free_contrib_lists()
   {
      if (m_pX_contribs)
      {
         crnlib_free(m_pX_contribs->p);
         m_pX_contribs->p = NULL;

         crnlib_free(m_pX_contribs);
         m_pX_contribs = NULL;
      }

      if (m_pY_contribs)
      {
         crnlib_free(m_pY_contribs->p);
         m_pY_contribs->p = NULL;

         crnlib_free(m_pY_contribs);
         m_pY_contribs = NULL;
      }
   }

   void threaded_resampler::resample_x_task(uint64 data, void* pData_ptr)
   {
      pData_ptr;
      const uint thread_index = (uint)data;

      for (uint src_y = 0; src_y < m_pParams->m_src_height; src_y++)
      {
         if (m_pTask_pool->get_num_threads())
         {
            if ((src_y % (m_pTask_pool->get_num_threads() + 1)) != thread_index)
               continue;
         }

         const Resampler::Contrib_List* pContribs = m_pX_contribs;
         const Resampler::Contrib_List* pContribs_end = m_pX_contribs + m_pParams->m_dst_width;

         switch (m_pParams->m_fmt)
         {
            case cPF_Y_F32:
            {
               const float* pSrc = reinterpret_cast<const float*>(static_cast<const uint8*>(m_pParams->m_pSrc_pixels) + m_pParams->m_src_pitch * src_y);
               vec4F* pDst = m_tmp_img.get_ptr() + m_pParams->m_dst_width * src_y;

               do
               {
                  const Resampler::Contrib* p = pContribs->p;
                  const Resampler::Contrib* p_end = pContribs->p + pContribs->n;

                  vec4F s(0.0f);

                  while (p != p_end)
                  {
                     const uint src_pixel = p->pixel;
                     const float src_weight = p->weight;

                     s[0] += pSrc[src_pixel] * src_weight;

                     p++;
                  }

                  *pDst++ = s;
                  pContribs++;
               } while (pContribs != pContribs_end);

               break;
            }
            case cPF_RGBX_F32:
            {
               const vec4F* pSrc = reinterpret_cast<const vec4F*>(static_cast<const uint8*>(m_pParams->m_pSrc_pixels) + m_pParams->m_src_pitch * src_y);
               vec4F* pDst = m_tmp_img.get_ptr() + m_pParams->m_dst_width * src_y;

               do
               {
                  const Resampler::Contrib* p = pContribs->p;
                  const Resampler::Contrib* p_end = pContribs->p + pContribs->n;

                  vec4F s(0.0f);

                  while (p != p_end)
                  {
                     const float src_weight = p->weight;

                     const vec4F& src_pixel = pSrc[p->pixel];

                     s[0] += src_pixel[0] * src_weight;
                     s[1] += src_pixel[1] * src_weight;
                     s[2] += src_pixel[2] * src_weight;

                     p++;
                  }

                  *pDst++ = s;
                  pContribs++;
               } while (pContribs != pContribs_end);

               break;
            }
            case cPF_RGBA_F32:
            {
               const vec4F* pSrc = reinterpret_cast<const vec4F*>(static_cast<const uint8*>(m_pParams->m_pSrc_pixels) + m_pParams->m_src_pitch * src_y);
               vec4F* pDst = m_tmp_img.get_ptr() + m_pParams->m_dst_width * src_y;

               do
               {
                  Resampler::Contrib* p = pContribs->p;
                  Resampler::Contrib* p_end = pContribs->p + pContribs->n;

                  vec4F s(0.0f);

                  while (p != p_end)
                  {
                     const float src_weight = p->weight;

                     const vec4F& src_pixel = pSrc[p->pixel];

                     s[0] += src_pixel[0] * src_weight;
                     s[1] += src_pixel[1] * src_weight;
                     s[2] += src_pixel[2] * src_weight;
                     s[3] += src_pixel[3] * src_weight;

                     p++;
                  }

                  *pDst++ = s;
                  pContribs++;
               } while (pContribs != pContribs_end);

               break;
            }
            default: break;
         }
      }
   }

   void threaded_resampler::resample_y_task(uint64 data, void* pData_ptr)
   {
      pData_ptr;

      const uint thread_index = (uint)data;

      crnlib::vector<vec4F> tmp(m_pParams->m_dst_width);

      for (uint dst_y = 0; dst_y < m_pParams->m_dst_height; dst_y++)
      {
         if (m_pTask_pool->get_num_threads())
         {
            if ((dst_y % (m_pTask_pool->get_num_threads() + 1)) != thread_index)
               continue;
         }

         const Resampler::Contrib_List& contribs = m_pY_contribs[dst_y];

         const vec4F* pSrc;

         if (contribs.n == 1)
         {
            pSrc = m_tmp_img.get_ptr() + m_pParams->m_dst_width * contribs.p[0].pixel;
         }
         else
         {
            for (uint src_y_iter = 0; src_y_iter < contribs.n; src_y_iter++)
            {
               const vec4F* p = m_tmp_img.get_ptr() + m_pParams->m_dst_width * contribs.p[src_y_iter].pixel;
               const float weight = contribs.p[src_y_iter].weight;

               if (!src_y_iter)
               {
                  for (uint i = 0; i < m_pParams->m_dst_width; i++)
                     tmp[i] = p[i] * weight;
               }
               else
               {
                  for (uint i = 0; i < m_pParams->m_dst_width; i++)
                     tmp[i] += p[i] * weight;
               }
            }

            pSrc = tmp.get_ptr();
         }

         const vec4F* pSrc_end = pSrc + m_pParams->m_dst_width;

         const float l = m_pParams->m_sample_low;
         const float h = m_pParams->m_sample_high;

         switch (m_pParams->m_fmt)
         {
            case cPF_Y_F32:
            {
               float* pDst = reinterpret_cast<float*>(static_cast<uint8*>(m_pParams->m_pDst_pixels) + m_pParams->m_dst_pitch * dst_y);

               do
               {
                  *pDst++ = math::clamp((*pSrc)[0], l, h);

                  pSrc++;

               } while (pSrc != pSrc_end);

               break;
            }
            case cPF_RGBX_F32:
            {
               vec4F* pDst = reinterpret_cast<vec4F*>(static_cast<uint8*>(m_pParams->m_pDst_pixels) + m_pParams->m_dst_pitch * dst_y);

               do
               {
                  (*pDst)[0] = math::clamp((*pSrc)[0], l, h);
                  (*pDst)[1] = math::clamp((*pSrc)[1], l, h);
                  (*pDst)[2] = math::clamp((*pSrc)[2], l, h);
                  (*pDst)[3] = h;

                  pSrc++;
                  pDst++;

               } while (pSrc != pSrc_end);

               break;
            }
            case cPF_RGBA_F32:
            {
               vec4F* pDst = reinterpret_cast<vec4F*>(static_cast<uint8*>(m_pParams->m_pDst_pixels) + m_pParams->m_dst_pitch * dst_y);

               do
               {
                  (*pDst)[0] = math::clamp((*pSrc)[0], l, h);
                  (*pDst)[1] = math::clamp((*pSrc)[1], l, h);
                  (*pDst)[2] = math::clamp((*pSrc)[2], l, h);
                  (*pDst)[3] = math::clamp((*pSrc)[3], l, h);

                  pSrc++;
                  pDst++;

               } while (pSrc != pSrc_end);

               break;
            }
            default: break;
         }
      }
   }

   bool threaded_resampler::resample(const params& p)
   {
      free_contrib_lists();

      m_pParams = &p;

      CRNLIB_ASSERT(m_pParams->m_src_width && m_pParams->m_src_height);
      CRNLIB_ASSERT(m_pParams->m_dst_width && m_pParams->m_dst_height);

      switch (p.m_fmt)
      {
         case cPF_Y_F32:
            m_bytes_per_pixel = 4;
            break;
         case cPF_RGBX_F32:
         case cPF_RGBA_F32:
            m_bytes_per_pixel = 16;
            break;
         default:
            CRNLIB_ASSERT(false);
            return false;
      }

      int filter_index = find_resample_filter(p.m_Pfilter_name);
      if (filter_index < 0)
         return false;

      const resample_filter& filter = g_resample_filters[filter_index];

      m_pX_contribs = Resampler::make_clist(m_pParams->m_src_width, m_pParams->m_dst_width, m_pParams->m_boundary_op, filter.func, filter.support, p.m_filter_x_scale, 0.0f);
      if (!m_pX_contribs)
         return false;

      m_pY_contribs = Resampler::make_clist(m_pParams->m_src_height, m_pParams->m_dst_height, m_pParams->m_boundary_op, filter.func, filter.support, p.m_filter_y_scale, 0.0f);
      if (!m_pY_contribs)
         return false;

      if (!m_tmp_img.try_resize(m_pParams->m_dst_width * m_pParams->m_src_height))
         return false;

      for (uint i = 0; i <= m_pTask_pool->get_num_threads(); i++)
         m_pTask_pool->queue_object_task(this, &threaded_resampler::resample_x_task, i, NULL);
      m_pTask_pool->join();

      for (uint i = 0; i <= m_pTask_pool->get_num_threads(); i++)
         m_pTask_pool->queue_object_task(this, &threaded_resampler::resample_y_task, i, NULL);
      m_pTask_pool->join();

      m_tmp_img.clear();
      free_contrib_lists();

      return true;
   }

} // namespace crnlib

