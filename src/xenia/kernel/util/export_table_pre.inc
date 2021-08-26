/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

/**
 * Pre-include file for an export table.
 * Use this to build tables of exports:
 *
 * // Build the export table used for resolution.
 * #include "xenia/kernel/util/export_table_pre.inc"
 * static Export my_module_export_table[] = {
 *   #include "xenia/kernel/my_module/my_module_table.inc"
 * };
 * #include "xenia/kernel/util/export_table_post.inc"
 * export_resolver_->RegisterTable(
 *     "my_module.xex",
 *     my_module_export_table, xe::countof(my_module_export_table));
 */

#define XE_EXPORT(module, ordinal, name, type) \
  xe::cpu::Export(ordinal, xe::cpu::Export::Type::type, #name)

#define FLAG(t) kXEKernelExportFlag##t
