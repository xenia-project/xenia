/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_ASSERT_H_
#define XENIA_ASSERT_H_

#include <assert.h>

#include <xenia/assert.h>
#include <xenia/config.h>
#include <xenia/platform.h>
#include <xenia/platform_includes.h>
#include <xenia/string.h>
#include <xenia/types.h>


#if 0 && XE_COMPILER_MSVC && defined(UNICODE) && UNICODE
// http://msdn.microsoft.com/en-us/library/b0084kay.aspx
#if !defined(__WFILE__)
#define WIDEN2(x)           L##x
#define WIDEN(x)            WIDEN2(x)
#define __WFILE__           WIDEN(__FILE__)
#define __WFUNCTION__       WIDEN(__FUNCTION__)
#endif
#define XE_CURRENT_FILE     __WFILE__
#define XE_CURRENT_FUNCTION __WFUNCTION__
#else
#define XE_CURRENT_FILE     __FILE__
#define XE_CURRENT_FUNCTION __FUNCTION__
#endif  // MSVC
#define XE_CURRENT_LINE     __LINE__


#define __XE_ASSERT(expr)               assert(expr)
#if XE_OPTION_ENABLE_ASSERTS
#define XEASSERTCORE(expr)              __XE_ASSERT(expr)
#else
#define XEASSERTCORE(expr)              XE_EMPTY_MACRO
#endif  // ENABLE_ASSERTS

#define XEASSERTALWAYS()                XEASSERTCORE( 0              )
#define XEASSERT(expr)                  XEASSERTCORE( (expr)         )
#define XEASSERTTRUE(expr)              XEASSERTCORE( (expr)         )
#define XEASSERTFALSE(expr)             XEASSERTCORE(!(expr)         )
#define XEASSERTZERO(expr)              XEASSERTCORE( (expr) == 0    )
#define XEASSERTNOTZERO(expr)           XEASSERTCORE( (expr) != 0    )
#define XEASSERTNULL(expr)              XEASSERTCORE( (expr) == NULL )
#define XEASSERTNOTNULL(expr)           XEASSERTCORE( (expr) != NULL )


#if XE_COMPILER_MSVC
// http://msdn.microsoft.com/en-us/library/bb918086.aspx
// TODO(benvanik): if 2010+, use static_assert?
//     http://msdn.microsoft.com/en-us/library/dd293588.aspx
#define XESTATICASSERT(expr, message)   _STATIC_ASSERT(expr)
//#elif XE_COMPILER_GNUC
// http://stackoverflow.com/questions/3385515/static-assert-in-c
//#define XESTATICASSERT(expr, message)   ({ extern int __attribute__((error("assertion failure: '" #expr "' not true - " #message))) compile_time_check(); ((expr)?0:compile_time_check()),0; })
#else
// http://stackoverflow.com/questions/3385515/static-assert-in-c
#define XESTATICASSERT3(expr, L)        typedef char static_assertion_##L[(expr)?1:-1]
#define XESTATICASSERT2(expr, L)        XESTATICASSERT3(expr, L)
#define XESTATICASSERT(expr, message)   XESTATICASSERT2(expr, __LINE__)
#endif  // MSVC

#define XEASSERTSTRUCTSIZE(target, size) XESTATICASSERT(sizeof(target) == size, "bad definition for " ## target ## ": must be " ## size ## " bytes")

#endif  // XENIA_ASSERT_H_
