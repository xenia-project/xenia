// File: crn_console.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_console.h"
#include "crn_data_stream.h"
#include "crn_threading.h"

namespace crnlib
{
   eConsoleMessageType                       console::m_default_category = cInfoConsoleMessage;
   crnlib::vector<console::console_func>      console::m_output_funcs;
   bool                                      console::m_crlf = true;
   bool                                      console::m_prefixes = true;
   bool                                      console::m_output_disabled;
   data_stream*                              console::m_pLog_stream;
   mutex*                                    console::m_pMutex;
   uint                                      console::m_num_messages[cCMTTotal];
   bool                                      console::m_at_beginning_of_line = true;

   const uint cConsoleBufSize = 4096;

   void console::init()
   {
      if (!m_pMutex)
      {
         m_pMutex = crnlib_new<mutex>();
      }
   }

   void console::deinit()
   {
      if (m_pMutex)
      {
         crnlib_delete(m_pMutex);
         m_pMutex = NULL;
      }
   }

   void console::disable_crlf()
   {
      init();

      m_crlf = false;
   }

   void console::enable_crlf()
   {
      init();

      m_crlf = true;
   }

   void console::vprintf(eConsoleMessageType type, const char* p, va_list args)
   {
      init();

      scoped_mutex lock(*m_pMutex);

      m_num_messages[type]++;

      char buf[cConsoleBufSize];
      vsprintf_s(buf, cConsoleBufSize, p, args);

      bool handled = false;

      if (m_output_funcs.size())
      {
         for (uint i = 0; i < m_output_funcs.size(); i++)
            if (m_output_funcs[i].m_func(type, buf, m_output_funcs[i].m_pData))
               handled = true;
      }

      const char* pPrefix = NULL;
      if ((m_prefixes) && (m_at_beginning_of_line))
      {
         switch (type)
         {
            case cDebugConsoleMessage:    pPrefix = "Debug: ";   break;
            case cWarningConsoleMessage:  pPrefix = "Warning: "; break;
            case cErrorConsoleMessage:    pPrefix = "Error: ";   break;
            default: break;
         }
      }

      if ((!m_output_disabled) && (!handled))
      {
         if (pPrefix)
            ::printf("%s", pPrefix);
         ::printf(m_crlf ? "%s\n" : "%s", buf);
      }

      uint n = strlen(buf);
      m_at_beginning_of_line = (m_crlf) || ((n) && (buf[n - 1] == '\n'));

      if ((type != cProgressConsoleMessage) && (m_pLog_stream))
      {
         // Yes this is bad.
         dynamic_string tmp_buf(buf);

         tmp_buf.translate_lf_to_crlf();

         m_pLog_stream->printf(m_crlf ? "%s\r\n" : "%s", tmp_buf.get_ptr());
         m_pLog_stream->flush();
      }
   }

   void console::printf(eConsoleMessageType type, const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(type, p, args);
      va_end(args);
   }

   void console::printf(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(m_default_category, p, args);
      va_end(args);
   }

   void console::set_default_category(eConsoleMessageType category)
   {
      init();

      m_default_category = category;
   }

   eConsoleMessageType console::get_default_category()
   {
      init();

      return m_default_category;
   }

   void console::add_console_output_func(console_output_func pFunc, void* pData)
   {
      init();

      scoped_mutex lock(*m_pMutex);

      m_output_funcs.push_back(console_func(pFunc, pData));
   }

   void console::remove_console_output_func(console_output_func pFunc)
   {
      init();

      scoped_mutex lock(*m_pMutex);

      for (int i = m_output_funcs.size() - 1; i >= 0; i--)
      {
         if (m_output_funcs[i].m_func == pFunc)
         {
            m_output_funcs.erase(m_output_funcs.begin() + i);
         }
      }

      if (!m_output_funcs.size())
      {
         m_output_funcs.clear();
      }
   }

   void console::progress(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cProgressConsoleMessage, p, args);
      va_end(args);
   }

   void console::info(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cInfoConsoleMessage, p, args);
      va_end(args);
   }

   void console::message(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cMessageConsoleMessage, p, args);
      va_end(args);
   }

   void console::cons(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cConsoleConsoleMessage, p, args);
      va_end(args);
   }

   void console::debug(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cDebugConsoleMessage, p, args);
      va_end(args);
   }

   void console::warning(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cWarningConsoleMessage, p, args);
      va_end(args);
   }

   void console::error(const char* p, ...)
   {
      va_list args;
      va_start(args, p);
      vprintf(cErrorConsoleMessage, p, args);
      va_end(args);
   }

} // namespace crnlib
