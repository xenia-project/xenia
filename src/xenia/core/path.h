/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CORE_PATH_H_
#define XENIA_CORE_PATH_H_

#include <xenia/common.h>
#include <xenia/core/pal.h>
#include <xenia/core/ref.h>


void xe_path_join(const xechar_t* left, const xechar_t* right,
                  xechar_t* out_path, size_t out_path_size);
void xe_path_get_absolute(const xechar_t* path, xechar_t* out_abs_path,
                          size_t abs_path_size);

const xechar_t* xe_path_get_tmp(const xechar_t* prefix);

#endif  // XENIA_CORE_PATH_H_
