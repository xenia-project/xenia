// File: crn_colorized_console.cpp
// See Copyright Notice and license at the end of inc/crnlib.h
#include "crn_core.h"
#include "crn_colorized_console.h"
#ifdef CRNLIB_USE_WIN32_API
#include "crn_winhdr.h"
#endif

namespace crnlib
{
   void colorized_console::init()
   {
      console::init();
      console::add_console_output_func(console_output_func, NULL);
   }

   void colorized_console::deinit()
   {
      console::remove_console_output_func(console_output_func);
      console::deinit();
   }

   void colorized_console::tick()
   {
   }

#ifdef CRNLIB_USE_WIN32_API
   bool colorized_console::console_output_func(eConsoleMessageType type, const char* pMsg, void* pData)
   {
      pData;

      if (console::get_output_disabled())
         return true;

      HANDLE cons = GetStdHandle(STD_OUTPUT_HANDLE);

      DWORD attr = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE;
      switch (type)
      {
         case cDebugConsoleMessage:    attr = FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
         case cMessageConsoleMessage:  attr = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY; break;
         case cWarningConsoleMessage:  attr = FOREGROUND_GREEN | FOREGROUND_RED | FOREGROUND_INTENSITY; break;
         case cErrorConsoleMessage:    attr = FOREGROUND_RED | FOREGROUND_INTENSITY; break;
         default: break;
      }

      if (INVALID_HANDLE_VALUE != cons)
         SetConsoleTextAttribute(cons, (WORD)attr);

      if ((console::get_prefixes()) && (console::get_at_beginning_of_line()))
      {
         switch (type)
         {
            case cDebugConsoleMessage:
               printf("Debug: %s", pMsg);
               break;
            case cWarningConsoleMessage:
               printf("Warning: %s", pMsg);
               break;
            case cErrorConsoleMessage:
               printf("Error: %s", pMsg);
               break;
            default:
               printf("%s", pMsg);
               break;
         }
      }
      else
      {
         printf("%s", pMsg);
      }

      if (console::get_crlf())
         printf("\n");

      if (INVALID_HANDLE_VALUE != cons)
         SetConsoleTextAttribute(cons, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);

      return true;
   }
#else
   bool colorized_console::console_output_func(eConsoleMessageType type, const char* pMsg, void* pData)
   {
      pData;
      if (console::get_output_disabled())
         return true;

      if ((console::get_prefixes()) && (console::get_at_beginning_of_line()))
      {
         switch (type)
         {
         case cDebugConsoleMessage:
            printf("Debug: %s", pMsg);
            break;
         case cWarningConsoleMessage:
            printf("Warning: %s", pMsg);
            break;
         case cErrorConsoleMessage:
            printf("Error: %s", pMsg);
            break;
         default:
            printf("%s", pMsg);
            break;
         }
      }
      else
      {
         printf("%s", pMsg);
      }

      if (console::get_crlf())
         printf("\n");

      return true;
   }
#endif

} // namespace crnlib

