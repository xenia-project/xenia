/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

/**
 * Pre-include file for an ordinal table.
 * Use this to build tables of constants describing the ordinals:
 *
 * // Build an ordinal enum to make it easy to lookup ordinals.
 * #include "xenia/kernel/util/ordinal_table_pre.inc"
 * namespace ordinals {
 * enum {
 *   #include "xenia/kernel/my_module/my_module_table.inc"
 * };
 * }  // namespace ordinals
 * #include "xenia/kernel/util/ordinal_table_post.inc"
 */

#define XE_EXPORT(module, ordinal, name, type) name = ordinal
