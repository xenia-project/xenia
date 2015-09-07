/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_XAM_ORDINALS_H_
#define XENIA_KERNEL_XAM_XAM_ORDINALS_H_

#include "xenia/cpu/export_resolver.h"

// Build an ordinal enum to make it easy to lookup ordinals.
#include "xenia/kernel/util/ordinal_table_pre.inc"
namespace ordinals {
enum {
#include "xenia/kernel/xam/xam_table.inc"
};
}  // namespace ordinals
#include "xenia/kernel/util/ordinal_table_post.inc"

#endif  // XENIA_KERNEL_XAM_XAM_ORDINALS_H_
