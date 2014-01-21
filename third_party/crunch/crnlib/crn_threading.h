// File: crn_threading.h
// See Copyright Notice and license at the end of inc/crnlib.h

#if CRNLIB_USE_WIN32_API
   #include "crn_threading_win32.h"
#elif CRNLIB_USE_PTHREADS_API
   #include "crn_threading_pthreads.h"
#else
   #include "crn_threading_null.h"
#endif
