/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_TYPES_H_
#define XENIA_TYPES_H_

#define XEDECLARECLASS1(ns1, name) \
    namespace ns1 { class name; }
#define XEDECLARECLASS2(ns1, ns2, name) \
    namespace ns1 { namespace ns2 { \
      class name; \
    } }

#define XEFAIL()                goto XECLEANUP
#define XEEXPECT(expr)          if (!(expr)         ) { goto XECLEANUP; }
#define XEEXPECTTRUE(expr)      if (!(expr)         ) { goto XECLEANUP; }
#define XEEXPECTFALSE(expr)     if ( (expr)         ) { goto XECLEANUP; }
#define XEEXPECTZERO(expr)      if ( (expr) != 0    ) { goto XECLEANUP; }
#define XEEXPECTNOTZERO(expr)   if ( (expr) == 0    ) { goto XECLEANUP; }
#define XEEXPECTNULL(expr)      if ( (expr) != NULL ) { goto XECLEANUP; }
#define XEEXPECTNOTNULL(expr)   if ( (expr) == NULL ) { goto XECLEANUP; }

#endif  // XENIA_TYPES_H_
