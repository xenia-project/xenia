/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_CPU_EXPORT_RESOLVER_H_
#define XENIA_CPU_EXPORT_RESOLVER_H_

#include <string>
#include <vector>

#include "xenia/base/math.h"
#include "xenia/cpu/ppc/ppc_context.h"

namespace xe {
namespace cpu {

enum class ExportCategory : uint8_t {
  kNone = 0,
  kAudio,
  kAvatars,
  kContent,
  kDebug,
  kFileSystem,
  kInput,
  kLocale,
  kMemory,
  kMisc,
  kModules,
  kNetworking,
  kThreading,
  kUI,
  kUserProfiles,
  kVideo,
};

struct ExportTag {
  using type = uint32_t;

  // packed like so:
  // ll...... cccccccc ........ ..bihssi

  static constexpr int CategoryShift = 16;

  // Export is implemented in some form and can be used.
  static constexpr type kImplemented = 1u << 0;
  // Export is a stub and is probably bad.
  static constexpr type kStub = 1u << 1;
  // Export is known to cause problems, or may not be complete.
  static constexpr type kSketchy = 1u << 2;
  // Export is called *a lot*.
  static constexpr type kHighFrequency = 1u << 3;
  // Export is important and should always be logged.
  static constexpr type kImportant = 1u << 4;
  // Export blocks the calling thread
  static constexpr type kBlocking = 1u << 5;
  static constexpr type kIsVariable = 1u << 6;
  // Export will be logged on each call.
  static constexpr type kLog = 1u << 30;
  // Export's result will be logged on each call.
  static constexpr type kLogResult = 1u << 31;
};

// DEPRECATED
typedef void (*xe_kernel_export_shim_fn)(void*, void*);

typedef void (*ExportTrampoline)(ppc::PPCContext* ppc_context);
#pragma pack(push, 1)
class Export {
 public:
  enum class Type {
    kFunction = 0,
    kVariable = 1,
  };
  constexpr Export(uint16_t ordinal, Type type, const char* name,
                   ExportTag::type tags = 0)
      : function_data({nullptr}),
        name(name ? name : ""),
        tags(tags),
        ordinal(ordinal)

  {
    if (type == Type::kVariable) {
      this->tags |= ExportTag::kIsVariable;
    }
  }
  union {
    // Variable data. Only valid when kXEKernelExportFlagVariable is set.
    // This is an address in the client memory space that the variable can
    // be found at.
    uint32_t variable_ptr;

    struct {

      // Trampoline that is called from the guest-to-host thunk.
      // Expects only PPC context as first arg.
      ExportTrampoline trampoline;
    } function_data;
  };
  const char* const name;
  ExportTag::type tags;
  uint16_t ordinal;
  // Type type;

  constexpr bool is_implemented() const {
    return (tags & ExportTag::kImplemented) == ExportTag::kImplemented;
  }
  constexpr Type get_type() const {
    return (this->tags & ExportTag::kIsVariable) ? Type::kVariable
                                                 : Type::kFunction;
  }
};
#pragma pack(pop)
class ExportResolver {
 public:
  class Table {
   public:
    Table(const std::string_view module_name,
          const std::vector<Export*>* exports);

    const std::string& module_name() const { return module_name_; }
    const std::vector<Export*>& exports_by_ordinal() const {
      return *exports_by_ordinal_;
    }
    const std::vector<Export*>& exports_by_name() const {
      return exports_by_name_;
    }

   private:
    std::string module_name_;
    const std::vector<Export*>* exports_by_ordinal_ = nullptr;
    std::vector<Export*> exports_by_name_;
  };

  ExportResolver();
  ~ExportResolver();

  void RegisterTable(const std::string_view module_name,
                     const std::vector<Export*>* exports);
  const std::vector<Table>& tables() const { return tables_; }
  const std::vector<Export*>& all_exports_by_name() const {
    return all_exports_by_name_;
  }

  Export* GetExportByOrdinal(const std::string_view module_name,
                             uint16_t ordinal);

  void SetVariableMapping(const std::string_view module_name, uint16_t ordinal,
                          uint32_t value);
  void SetFunctionMapping(const std::string_view module_name, uint16_t ordinal,
                          xe_kernel_export_shim_fn shim);
  void SetFunctionMapping(const std::string_view module_name, uint16_t ordinal,
                          ExportTrampoline trampoline);

 private:
  std::vector<Table> tables_;
  std::vector<Export*> all_exports_by_name_;
};

}  // namespace cpu
}  // namespace xe

#endif  // XENIA_CPU_EXPORT_RESOLVER_H_
