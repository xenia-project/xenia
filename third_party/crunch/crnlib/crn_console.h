// File: crn_console.h
// See Copyright Notice and license at the end of inc/crnlib.h
#pragma once
#include "crn_dynamic_string.h"

#ifdef WIN32
#include <tchar.h>
#include <conio.h>
#endif
namespace crnlib
{
   class dynamic_string;
   class data_stream;
   class mutex;

   enum eConsoleMessageType
   {
      cDebugConsoleMessage,      // debugging messages
      cProgressConsoleMessage,   // progress messages
      cInfoConsoleMessage,       // ordinary messages
      cConsoleConsoleMessage,    // user console output
      cMessageConsoleMessage,    // high importance messages
      cWarningConsoleMessage,    // warnings
      cErrorConsoleMessage,      // errors

      cCMTTotal
   };

   typedef bool (*console_output_func)(eConsoleMessageType type, const char* pMsg, void* pData);

   class console
   {
   public:
      static void init();
      static void deinit();

      static bool is_initialized() { return m_pMutex != NULL; }

      static void set_default_category(eConsoleMessageType category);
      static eConsoleMessageType get_default_category();

      static void add_console_output_func(console_output_func pFunc, void* pData);
      static void remove_console_output_func(console_output_func pFunc);

      static void printf(const char* p, ...);

      static void vprintf(eConsoleMessageType type, const char* p, va_list args);
      static void printf(eConsoleMessageType type, const char* p, ...);

      static void cons(const char* p, ...);
      static void debug(const char* p, ...);
      static void progress(const char* p, ...);
      static void info(const char* p, ...);
      static void message(const char* p, ...);
      static void warning(const char* p, ...);
      static void error(const char* p, ...);

      // FIXME: All console state is currently global!
      static void disable_prefixes();
      static void enable_prefixes();
      static bool get_prefixes() { return m_prefixes; }
      static bool get_at_beginning_of_line() { return m_at_beginning_of_line; }

      static void disable_crlf();
      static void enable_crlf();
      static bool get_crlf() { return m_crlf; }

      static void disable_output() { m_output_disabled = true; }
      static void enable_output() { m_output_disabled = false; }
      static bool get_output_disabled() { return m_output_disabled; }

      static void set_log_stream(data_stream* pStream) { m_pLog_stream = pStream; }
      static data_stream* get_log_stream() { return m_pLog_stream; }

      static uint get_num_messages(eConsoleMessageType type) { return m_num_messages[type]; }

   private:
      static eConsoleMessageType m_default_category;

      struct console_func
      {
         console_func(console_output_func func = NULL, void* pData = NULL) : m_func(func), m_pData(pData) { }

         console_output_func  m_func;
         void*                m_pData;
      };
      static crnlib::vector<console_func> m_output_funcs;

      static bool m_crlf, m_prefixes, m_output_disabled;

      static data_stream* m_pLog_stream;

      static mutex* m_pMutex;

      static uint m_num_messages[cCMTTotal];

      static bool m_at_beginning_of_line;
   };

#if defined(WIN32)
   inline int crn_getch()
   {
      return _getch();
   }
#elif defined(__GNUC__)
   #include <termios.h>
   #include <unistd.h>
   inline int crn_getch()
   {
      struct termios oldt, newt;
      int ch;
      tcgetattr(STDIN_FILENO, &oldt);
      newt = oldt;
      newt.c_lflag &= ~(ICANON | ECHO);
      tcsetattr(STDIN_FILENO, TCSANOW, &newt);
      ch = getchar();
      tcsetattr(STDIN_FILENO, TCSANOW, &oldt);
      return ch;
   }
#else 
   inline int crn_getch()
   {
      printf("crn_getch: Unimplemented");
      return 0;
   }
#endif
} // namespace crnlib

