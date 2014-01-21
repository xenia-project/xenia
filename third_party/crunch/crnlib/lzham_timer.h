// File: lzham_timer.h
// See Copyright Notice and license at the end of include/lzham.h
#pragma once

namespace lzham
{
   typedef unsigned long long timer_ticks;
      
   class lzham_timer
   {
   public:
      lzham_timer();
      lzham_timer(timer_ticks start_ticks);
      
      void start();
      void start(timer_ticks start_ticks);
      
      void stop();
         
      double get_elapsed_secs() const;
      inline double get_elapsed_ms() const { return get_elapsed_secs() * 1000.0f; }
      timer_ticks get_elapsed_us() const;
      
      static void init();
      static inline timer_ticks get_ticks_per_sec() { return g_freq; }
      static timer_ticks get_init_ticks();
      static timer_ticks get_ticks();
      static double ticks_to_secs(timer_ticks ticks);
      static inline double ticks_to_ms(timer_ticks ticks) { return ticks_to_secs(ticks) * 1000.0f; }
      static inline double get_secs() { return ticks_to_secs(get_ticks()); }
      static inline double get_ms() { return ticks_to_ms(get_ticks()); }
                   
   private:
      static timer_ticks g_init_ticks;
      static timer_ticks g_freq;
      static double g_inv_freq;
      
      timer_ticks m_start_time;
      timer_ticks m_stop_time;
      
      bool m_started : 1;
      bool m_stopped : 1;
   };

   enum var_args_t { cVarArgs };
   
#if LZHAM_PERF_SECTIONS
   class scoped_perf_section
   {
   public:
      inline scoped_perf_section() :
         m_start_ticks(lzham_timer::get_ticks())
      {
         m_name[0] = '?';
         m_name[1] = '\0';
      }
      
      inline scoped_perf_section(const char *pName) :
         m_start_ticks(lzham_timer::get_ticks())
      {
         strcpy_s(m_name, pName);
         
         lzham_buffered_printf("Thread: 0x%08X, BEGIN Time: %3.3fms, Section: %s\n", GetCurrentThreadId(), lzham_timer::ticks_to_ms(m_start_ticks), m_name);
      }
      
      inline scoped_perf_section(var_args_t, const char *pName, ...) :
         m_start_ticks(lzham_timer::get_ticks())
      {
         va_list args;
         va_start(args, pName);
         vsprintf_s(m_name, sizeof(m_name), pName, args);
         va_end(args);
         
         lzham_buffered_printf("Thread: 0x%08X, BEGIN Time: %3.3fms, Section: %s\n", GetCurrentThreadId(), lzham_timer::ticks_to_ms(m_start_ticks), m_name);
      }
   
      inline ~scoped_perf_section()
      {
         double end_ms = lzham_timer::get_ms();
         double start_ms = lzham_timer::ticks_to_ms(m_start_ticks);
         
         lzham_buffered_printf("Thread: 0x%08X, END Time: %3.3fms, Total: %3.3fms, Section: %s\n", GetCurrentThreadId(), end_ms, end_ms - start_ms, m_name);
      }

   private:
      char m_name[64];   
      timer_ticks m_start_ticks;
   };
#else
   class scoped_perf_section
   {
   public:
      inline scoped_perf_section() { }
      inline scoped_perf_section(const char *pName) { (void)pName; }
      inline scoped_perf_section(var_args_t, const char *pName, ...) { (void)pName; }
   };
#endif // LZHAM_PERF_SECTIONS   

} // namespace lzham
