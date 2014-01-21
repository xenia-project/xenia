// File: lzham_timer.cpp
// See Copyright Notice and license at the end of include/lzham.h
#include "lzham_core.h"
#include "lzham_timer.h"

#ifndef LZHAM_USE_WIN32_API
   #include <time.h>
#endif   

namespace lzham
{
   unsigned long long lzham_timer::g_init_ticks;
   unsigned long long lzham_timer::g_freq;
   double lzham_timer::g_inv_freq;
   
   #if LZHAM_USE_WIN32_API
      inline void query_counter(timer_ticks *pTicks)
      {
         QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(pTicks));
      }
      inline void query_counter_frequency(timer_ticks *pTicks)
      {
         QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(pTicks));
      }
   #else
      inline void query_counter(timer_ticks *pTicks)
      {
         *pTicks = clock();
      }
      inline void query_counter_frequency(timer_ticks *pTicks)
      {
         *pTicks = CLOCKS_PER_SEC;
      }
   #endif
   
   lzham_timer::lzham_timer() :
      m_start_time(0),
      m_stop_time(0),
      m_started(false),
      m_stopped(false)
   {
      if (!g_inv_freq) 
         init();
   }

   lzham_timer::lzham_timer(timer_ticks start_ticks)
   {
      if (!g_inv_freq) 
         init();
      
      m_start_time = start_ticks;
      
      m_started = true;
      m_stopped = false;
   }

   void lzham_timer::start(timer_ticks start_ticks)
   {
      m_start_time = start_ticks;
      
      m_started = true;
      m_stopped = false;
   }

   void lzham_timer::start()
   {
      query_counter(&m_start_time);
      
      m_started = true;
      m_stopped = false;
   }

   void lzham_timer::stop()
   {
      LZHAM_ASSERT(m_started);
                  
      query_counter(&m_stop_time);
      
      m_stopped = true;
   }

   double lzham_timer::get_elapsed_secs() const
   {
      LZHAM_ASSERT(m_started);
      if (!m_started)
         return 0;
      
      timer_ticks stop_time = m_stop_time;
      if (!m_stopped)
         query_counter(&stop_time);
         
      timer_ticks delta = stop_time - m_start_time;
      return delta * g_inv_freq;
   }

   timer_ticks lzham_timer::get_elapsed_us() const
   {
      LZHAM_ASSERT(m_started);
      if (!m_started)
         return 0;
         
      timer_ticks stop_time = m_stop_time;
      if (!m_stopped)
         query_counter(&stop_time);
      
      timer_ticks delta = stop_time - m_start_time;
      return (delta * 1000000ULL + (g_freq >> 1U)) / g_freq;      
   }

   void lzham_timer::init()
   {
      if (!g_inv_freq)
      {
         query_counter_frequency(&g_freq);
         g_inv_freq = 1.0f / g_freq;
         
         query_counter(&g_init_ticks);
      }
   }

   timer_ticks lzham_timer::get_init_ticks()
   {
      if (!g_inv_freq) 
         init();
      
      return g_init_ticks;
   }

   timer_ticks lzham_timer::get_ticks()
   {
      if (!g_inv_freq) 
         init();
      
      timer_ticks ticks;
      query_counter(&ticks);
      return ticks - g_init_ticks;
   }

   double lzham_timer::ticks_to_secs(timer_ticks ticks)
   {
      if (!g_inv_freq) 
         init();
      
      return ticks * g_inv_freq;
   }
   
} // namespace lzham