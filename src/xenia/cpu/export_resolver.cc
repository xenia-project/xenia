/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/cpu/export_resolver.h"

#include "xenia/base/assert.h"
#include "xenia/base/math.h"
#include "xenia/base/string.h"

namespace xe {
namespace cpu {

ExportResolver::Table::Table(const std::string_view module_name,
                             const std::vector<Export*>* exports_by_ordinal)
    : exports_by_ordinal_(exports_by_ordinal) {
  module_name_ = utf8::find_base_name_from_guest_path(module_name);

  exports_by_name_.reserve(exports_by_ordinal_->size());
  for (size_t i = 0; i < exports_by_ordinal_->size(); ++i) {
    auto export_entry = exports_by_ordinal_->at(i);
    if (export_entry) {
      exports_by_name_.push_back(export_entry);
    }
  }
  std::sort(
      exports_by_name_.begin(), exports_by_name_.end(),
      [](Export* a, Export* b) { return std::strcmp(a->name, b->name) < 0; });
}

ExportResolver::ExportResolver() = default;

ExportResolver::~ExportResolver() = default;

void ExportResolver::RegisterTable(
    const std::string_view module_name,
    const std::vector<xe::cpu::Export*>* exports) {
  tables_.emplace_back(module_name, exports);

  all_exports_by_name_.reserve(all_exports_by_name_.size() + exports->size());
  for (size_t i = 0; i < exports->size(); ++i) {
    auto export_entry = exports->at(i);
    if (export_entry) {
      all_exports_by_name_.push_back(export_entry);
    }
  }
  std::sort(
      all_exports_by_name_.begin(), all_exports_by_name_.end(),
      [](Export* a, Export* b) { return std::strcmp(a->name, b->name) < 0; });
}

Export* ExportResolver::GetExportByOrdinal(const std::string_view module_name,
                                           uint16_t ordinal) {
  for (const auto& table : tables_) {
    if (xe::utf8::starts_with_case(module_name, table.module_name())) {
      if (ordinal >= table.exports_by_ordinal().size()) {
        return nullptr;
      }
      return table.exports_by_ordinal().at(ordinal);
    }
  }
  return nullptr;
}

void ExportResolver::SetVariableMapping(const std::string_view module_name,
                                        uint16_t ordinal, uint32_t value) {
  auto export_entry = GetExportByOrdinal(module_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->variable_ptr = value;
}

void ExportResolver::SetFunctionMapping(const std::string_view module_name,
                                        uint16_t ordinal,
                                        xe_kernel_export_shim_fn shim) {
  auto export_entry = GetExportByOrdinal(module_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->function_data.trampoline = (ExportTrampoline)(void*)shim;
}

void ExportResolver::SetFunctionMapping(const std::string_view module_name,
                                        uint16_t ordinal,
                                        ExportTrampoline trampoline) {
  auto export_entry = GetExportByOrdinal(module_name, ordinal);
  assert_not_null(export_entry);
  export_entry->tags |= ExportTag::kImplemented;
  export_entry->function_data.trampoline = trampoline;
}

}  // namespace cpu
}  // namespace xe
