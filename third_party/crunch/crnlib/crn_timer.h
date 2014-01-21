// File: crn_win32_timer.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once

namespace crnlib
{
   typedef unsigned long long timer_ticks;

   class timer
   {
   public:
      timer();
      timer(timer_ticks start_ticks);

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

   // Prints object's lifetime to stdout
   class timed_scope
   {
      const char* m_pName;
      timer m_tm;

   public:
      inline timed_scope(char* pName = "timed_scope") : m_pName(pName) { m_tm.start(); }
      
      inline double get_elapsed_secs() const { return m_tm.get_elapsed_secs(); }
      inline double get_elapsed_ms() const { return m_tm.get_elapsed_ms(); }

      const timer &get_timer() const { return m_tm; }
            timer &get_timer() { return m_tm; }

      inline ~timed_scope() { double secs = m_tm.get_elapsed_secs(); printf("%s: %f secs, %f ms\n", m_pName, secs, secs * 1000.0f); }
   };

} // namespace crnlib
