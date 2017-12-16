/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_SHIM_UTILS_H_
#define XENIA_KERNEL_UTIL_SHIM_UTILS_H_

#include <gflags/gflags.h>

#include <cstring>
#include <string>

#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/kernel/kernel_state.h"

DECLARE_bool(log_high_frequency_kernel_calls);

namespace xe {
namespace kernel {

using PPCContext = xe::cpu::ppc::PPCContext;

#define SHIM_CALL void __cdecl
#define SHIM_SET_MAPPING(library_name, export_name, shim_data) \
  export_resolver->SetFunctionMapping(                         \
      library_name, ordinals::export_name,                     \
      (xe::cpu::xe_kernel_export_shim_fn)export_name##_shim);

#define SHIM_MEM_BASE ppc_context->virtual_membase
#define SHIM_MEM_ADDR(a) (a ? (ppc_context->virtual_membase + a) : nullptr)

#define SHIM_MEM_8(a) xe::load_and_swap<uint8_t>(SHIM_MEM_ADDR(a))
#define SHIM_MEM_16(a) xe::load_and_swap<uint16_t>(SHIM_MEM_ADDR(a))
#define SHIM_MEM_32(a) xe::load_and_swap<uint32_t>(SHIM_MEM_ADDR(a))
#define SHIM_MEM_64(a) xe::load_and_swap<uint64_t>(SHIM_MEM_ADDR(a))
#define SHIM_SET_MEM_8(a, v) xe::store_and_swap<uint8_t>(SHIM_MEM_ADDR(a), v)
#define SHIM_SET_MEM_16(a, v) xe::store_and_swap<uint16_t>(SHIM_MEM_ADDR(a), v)
#define SHIM_SET_MEM_32(a, v) xe::store_and_swap<uint32_t>(SHIM_MEM_ADDR(a), v)
#define SHIM_SET_MEM_64(a, v) xe::store_and_swap<uint64_t>(SHIM_MEM_ADDR(a), v)

namespace util {
inline uint32_t get_arg_stack_ptr(PPCContext* ppc_context, uint8_t index) {
  return ((uint32_t)ppc_context->r[1]) + 0x54 + index * 8;
}

inline uint8_t get_arg_8(PPCContext* ppc_context, uint8_t index) {
  if (index <= 7) {
    return (uint8_t)ppc_context->r[3 + index];
  }
  uint32_t stack_address = get_arg_stack_ptr(ppc_context, index - 8);
  return SHIM_MEM_8(stack_address);
}

inline uint16_t get_arg_16(PPCContext* ppc_context, uint8_t index) {
  if (index <= 7) {
    return (uint16_t)ppc_context->r[3 + index];
  }
  uint32_t stack_address = get_arg_stack_ptr(ppc_context, index - 8);
  return SHIM_MEM_16(stack_address);
}

inline uint32_t get_arg_32(PPCContext* ppc_context, uint8_t index) {
  if (index <= 7) {
    return (uint32_t)ppc_context->r[3 + index];
  }
  uint32_t stack_address = get_arg_stack_ptr(ppc_context, index - 8);
  return SHIM_MEM_32(stack_address);
}

inline uint64_t get_arg_64(PPCContext* ppc_context, uint8_t index) {
  if (index <= 7) {
    return ppc_context->r[3 + index];
  }
  uint32_t stack_address = get_arg_stack_ptr(ppc_context, index - 8);
  return SHIM_MEM_64(stack_address);
}
}  // namespace util

#define SHIM_GET_ARG_8(n) util::get_arg_8(ppc_context, n)
#define SHIM_GET_ARG_16(n) util::get_arg_16(ppc_context, n)
#define SHIM_GET_ARG_32(n) util::get_arg_32(ppc_context, n)
#define SHIM_GET_ARG_64(n) util::get_arg_64(ppc_context, n)
#define SHIM_SET_RETURN_32(v) ppc_context->r[3] = (uint64_t)((int32_t)v)

#define SHIM_STRUCT(type, address) \
  reinterpret_cast<type*>(SHIM_MEM_ADDR(address))

namespace shim {

class Param {
 public:
  struct Init {
    PPCContext* ppc_context;
    int ordinal;
    int float_ordinal;
  };

  Param& operator=(const Param&) = delete;

  int ordinal() const { return ordinal_; }

 protected:
  Param() : ordinal_(-1) {}
  explicit Param(Init& init) : ordinal_(--init.ordinal) {}

  template <typename V>
  void LoadValue(Init& init, V* out_value) {
    if (ordinal_ <= 7) {
      *out_value = V(init.ppc_context->r[3 + ordinal_]);
    } else {
      uint32_t stack_ptr =
          uint32_t(init.ppc_context->r[1]) + 0x54 + (ordinal_ - 8) * 8;
      *out_value =
          xe::load_and_swap<V>(init.ppc_context->virtual_membase + stack_ptr);
    }
  }

  int ordinal_;
};
template <>
inline void Param::LoadValue<float>(Param::Init& init, float* out_value) {
  *out_value =
      static_cast<float>(init.ppc_context->f[1 + ++init.float_ordinal]);
}
template <>
inline void Param::LoadValue<double>(Param::Init& init, double* out_value) {
  *out_value = init.ppc_context->f[1 + ++init.float_ordinal];
}
template <typename T>
class ParamBase : public Param {
 public:
  ParamBase() : Param(), value_(0) {}
  ParamBase(T value) : Param(), value_(value) {}
  ParamBase(Init& init) : Param(init) { LoadValue<T>(init, &value_); }
  ParamBase& operator=(const T& other) {
    value_ = other;
    return *this;
  }
  operator T() const { return value_; }
  T value() const { return value_; }

 protected:
  T value_;
};

class PointerParam : public ParamBase<uint32_t> {
 public:
  PointerParam(Init& init) : ParamBase(init) {
    host_ptr_ = value_ ? init.ppc_context->virtual_membase + value_ : nullptr;
  }
  PointerParam(void* host_ptr) : ParamBase(), host_ptr_(host_ptr) {}
  PointerParam& operator=(void*& other) {
    host_ptr_ = other;
    return *this;
  }
  uint32_t guest_address() const { return value_; }
  uintptr_t host_address() const {
    return reinterpret_cast<uintptr_t>(host_ptr_);
  }
  template <typename T>
  T as() const {
    return reinterpret_cast<T>(host_ptr_);
  }
  template <typename T>
  xe::be<T>* as_array() const {
    return reinterpret_cast<xe::be<T>*>(host_ptr_);
  }
  operator void*() const { return host_ptr_; }
  operator uint8_t*() const { return reinterpret_cast<uint8_t*>(host_ptr_); }
  operator bool() const { return host_ptr_ != nullptr; }
  void* operator+(int offset) const {
    return reinterpret_cast<uint8_t*>(host_ptr_) + offset;
  }
  void Zero(size_t size) const {
    assert_not_null(host_ptr_);
    std::memset(host_ptr_, 0, size);
  }

 protected:
  void* host_ptr_;
};

template <typename T>
class PrimitivePointerParam : public ParamBase<uint32_t> {
 public:
  PrimitivePointerParam(Init& init) : ParamBase(init) {
    host_ptr_ = value_ ? reinterpret_cast<xe::be<T>*>(
                             init.ppc_context->virtual_membase + value_)
                       : nullptr;
  }
  PrimitivePointerParam(T* host_ptr) : ParamBase() {
    host_ptr_ = reinterpret_cast<xe::be<T>*>(host_ptr);
  }
  PrimitivePointerParam& operator=(const T*& other) {
    host_ptr_ = other;
    return *this;
  }
  uint32_t guest_address() const { return value_; }
  uintptr_t host_address() const {
    return reinterpret_cast<uintptr_t>(host_ptr_);
  }
  T value() const { return *host_ptr_; }
  operator T() const = delete;
  operator xe::be<T>*() const { return host_ptr_; }
  operator bool() const { return host_ptr_ != nullptr; }
  void Zero() const {
    assert_not_null(host_ptr_);
    *host_ptr_ = 0;
  }

 protected:
  xe::be<T>* host_ptr_;
};

template <typename CHAR, typename STR>
class StringPointerParam : public ParamBase<uint32_t> {
 public:
  StringPointerParam(Init& init) : ParamBase(init) {
    host_ptr_ = value_ ? reinterpret_cast<CHAR*>(
                             init.ppc_context->virtual_membase + value_)
                       : nullptr;
  }
  StringPointerParam(CHAR* host_ptr) : ParamBase(), host_ptr_(host_ptr) {}
  StringPointerParam& operator=(const CHAR*& other) {
    host_ptr_ = other;
    return *this;
  }
  uint32_t guest_address() const { return value_; }
  uintptr_t host_address() const {
    return reinterpret_cast<uintptr_t>(host_ptr_);
  }
  STR value() const { return xe::load_and_swap<STR>(host_ptr_); }
  operator CHAR*() const { return host_ptr_; }
  operator bool() const { return host_ptr_ != nullptr; }

 protected:
  CHAR* host_ptr_;
};

template <typename T>
class TypedPointerParam : public ParamBase<uint32_t> {
 public:
  TypedPointerParam(Init& init) : ParamBase(init) {
    host_ptr_ =
        value_
            ? reinterpret_cast<T*>(init.ppc_context->virtual_membase + value_)
            : nullptr;
  }
  TypedPointerParam(T* host_ptr) : ParamBase(), host_ptr_(host_ptr) {}
  TypedPointerParam& operator=(const T*& other) {
    host_ptr_ = other;
    return *this;
  }
  uint32_t guest_address() const { return value_; }
  uintptr_t host_address() const {
    return reinterpret_cast<uintptr_t>(host_ptr_);
  }
  operator T*() const { return host_ptr_; }
  operator bool() const { return host_ptr_ != nullptr; }
  T* operator->() const {
    assert_not_null(host_ptr_);
    return host_ptr_;
  }
  void Zero() const {
    assert_not_null(host_ptr_);
    std::memset(host_ptr_, 0, sizeof(T));
  }

 protected:
  T* host_ptr_;
};

template <typename T>
class Result {
 public:
  Result(T value) : value_(value) {}
  void Store(PPCContext* ppc_context) {
    ppc_context->r[3] = uint64_t(int32_t(value_));
  }
  Result() = delete;
  Result& operator=(const Result&) = delete;
  operator T() const { return value_; }

 private:
  T value_;
};

}  // namespace shim

using int_t = const shim::ParamBase<int32_t>&;
using word_t = const shim::ParamBase<uint16_t>&;
using dword_t = const shim::ParamBase<uint32_t>&;
using qword_t = const shim::ParamBase<uint64_t>&;
using float_t = const shim::ParamBase<float>&;
using double_t = const shim::ParamBase<double>&;
using lpvoid_t = const shim::PointerParam&;
using lpword_t = const shim::PrimitivePointerParam<uint16_t>&;
using lpdword_t = const shim::PrimitivePointerParam<uint32_t>&;
using lpqword_t = const shim::PrimitivePointerParam<uint64_t>&;
using lpfloat_t = const shim::PrimitivePointerParam<float>&;
using lpdouble_t = const shim::PrimitivePointerParam<double>&;
using lpstring_t = const shim::StringPointerParam<char, std::string>&;
using lpwstring_t = const shim::StringPointerParam<wchar_t, std::wstring>&;
using function_t = const shim::ParamBase<uint32_t>&;
using unknown_t = const shim::ParamBase<uint32_t>&;
using lpunknown_t = const shim::PointerParam&;
template <typename T>
using pointer_t = const shim::TypedPointerParam<T>&;

using int_result_t = shim::Result<int32_t>;
using dword_result_t = shim::Result<uint32_t>;
using pointer_result_t = shim::Result<uint32_t>;

// Exported from kernel_state.cc.
KernelState* kernel_state();
inline Memory* kernel_memory() { return kernel_state()->memory(); }

namespace shim {

inline void AppendParam(StringBuffer* string_buffer, int_t param) {
  string_buffer->AppendFormat("%d", int32_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, word_t param) {
  string_buffer->AppendFormat("%.4X", uint16_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, dword_t param) {
  string_buffer->AppendFormat("%.8X", uint32_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, qword_t param) {
  string_buffer->AppendFormat("%.16llX", uint64_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, float_t param) {
  string_buffer->AppendFormat("%G", static_cast<float>(param));
}
inline void AppendParam(StringBuffer* string_buffer, double_t param) {
  string_buffer->AppendFormat("%G", static_cast<double>(param));
}
inline void AppendParam(StringBuffer* string_buffer, lpvoid_t param) {
  string_buffer->AppendFormat("%.8X", uint32_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, lpdword_t param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("(%.8X)", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpqword_t param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("(%.16llX)", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpfloat_t param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("(%G)", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpdouble_t param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("(%G)", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpstring_t param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("(%s)", param.value().c_str());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpwstring_t param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("(%S)", param.value().c_str());
  }
}
inline void AppendParam(StringBuffer* string_buffer,
                        pointer_t<X_OBJECT_ATTRIBUTES> record) {
  string_buffer->AppendFormat("%.8X", record.guest_address());
  if (record) {
    auto name_string =
        kernel_memory()->TranslateVirtual<X_ANSI_STRING*>(record->name_ptr);
    std::string name =
        name_string == nullptr
            ? "(null)"
            : name_string->to_string(kernel_memory()->virtual_membase());
    string_buffer->AppendFormat("(%.8X,%s,%.8X)",
                                uint32_t(record->root_directory), name.c_str(),
                                uint32_t(record->attributes));
  }
}
inline void AppendParam(StringBuffer* string_buffer,
                        pointer_t<X_EX_TITLE_TERMINATE_REGISTRATION> reg) {
  string_buffer->AppendFormat("%.8X(%.8X, %.8X)", reg.guest_address(),
                              static_cast<uint32_t>(reg->notification_routine),
                              static_cast<uint32_t>(reg->priority));
}
inline void AppendParam(StringBuffer* string_buffer,
                        pointer_t<X_EXCEPTION_RECORD> record) {
  string_buffer->AppendFormat("%.8X(%.8X)", record.guest_address(),
                              uint32_t(record->exception_code));
}
template <typename T>
void AppendParam(StringBuffer* string_buffer, pointer_t<T> param) {
  string_buffer->AppendFormat("%.8X", param.guest_address());
}

enum class KernelModuleId {
  xboxkrnl,
  xam,
  xbdm,
};

template <size_t I = 0, typename... Ps>
typename std::enable_if<I == sizeof...(Ps)>::type AppendKernelCallParams(
    StringBuffer& string_buffer, xe::cpu::Export* export_entry,
    const std::tuple<Ps...>&) {}

template <size_t I = 0, typename... Ps>
    typename std::enable_if <
    I<sizeof...(Ps)>::type AppendKernelCallParams(
        StringBuffer& string_buffer, xe::cpu::Export* export_entry,
        const std::tuple<Ps...>& params) {
  if (I) {
    string_buffer.Append(", ");
  }
  auto param = std::get<I>(params);
  AppendParam(&string_buffer, param);
  AppendKernelCallParams<I + 1>(string_buffer, export_entry, params);
}

StringBuffer* thread_local_string_buffer();

template <typename Tuple>
void PrintKernelCall(cpu::Export* export_entry, const Tuple& params) {
  auto& string_buffer = *thread_local_string_buffer();
  string_buffer.Reset();
  string_buffer.Append(export_entry->name);
  string_buffer.Append('(');
  AppendKernelCallParams(string_buffer, export_entry, params);
  string_buffer.Append(')');
  if (export_entry->tags & xe::cpu::ExportTag::kImportant) {
    xe::LogLine(xe::LogLevel::LOG_LEVEL_INFO, 'i', string_buffer.GetString(),
                string_buffer.length());
  } else {
    xe::LogLine(xe::LogLevel::LOG_LEVEL_DEBUG, 'd', string_buffer.GetString(),
                string_buffer.length());
  }
}

template <typename F, typename Tuple, std::size_t... I>
auto KernelTrampoline(F&& f, Tuple&& t, std::index_sequence<I...>) {
  return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}

template <KernelModuleId MODULE, uint16_t ORDINAL, typename R, typename... Ps>
xe::cpu::Export* RegisterExport(R (*fn)(Ps&...), const char* name,
                                xe::cpu::ExportTag::type tags) {
  static const auto export_entry = new cpu::Export(
      ORDINAL, xe::cpu::Export::Type::kFunction, name,
      tags | xe::cpu::ExportTag::kImplemented | xe::cpu::ExportTag::kLog);
  static R (*FN)(Ps & ...) = fn;
  struct X {
    static void Trampoline(PPCContext* ppc_context) {
      ++export_entry->function_data.call_count;
      Param::Init init = {
          ppc_context,
          sizeof...(Ps),
          0,
      };
      auto params = std::make_tuple<Ps...>(Ps(init)...);
      if (export_entry->tags & xe::cpu::ExportTag::kLog &&
          (!(export_entry->tags & xe::cpu::ExportTag::kHighFrequency) ||
           FLAGS_log_high_frequency_kernel_calls)) {
        PrintKernelCall(export_entry, params);
      }
      auto result =
          KernelTrampoline(FN, std::forward<std::tuple<Ps...>>(params),
                           std::make_index_sequence<sizeof...(Ps)>());
      result.Store(ppc_context);
      if (export_entry->tags &
          (xe::cpu::ExportTag::kLog | xe::cpu::ExportTag::kLogResult)) {
        // TODO(benvanik): log result.
      }
    }
  };
  export_entry->function_data.trampoline = &X::Trampoline;
  return export_entry;
}

template <KernelModuleId MODULE, uint16_t ORDINAL, typename... Ps>
xe::cpu::Export* RegisterExport(void (*fn)(Ps&...), const char* name,
                                xe::cpu::ExportTag::type tags) {
  static const auto export_entry = new cpu::Export(
      ORDINAL, xe::cpu::Export::Type::kFunction, name,
      tags | xe::cpu::ExportTag::kImplemented | xe::cpu::ExportTag::kLog);
  static void (*FN)(Ps & ...) = fn;
  struct X {
    static void Trampoline(PPCContext* ppc_context) {
      ++export_entry->function_data.call_count;
      Param::Init init = {
          ppc_context,
          sizeof...(Ps),
      };
      auto params = std::make_tuple<Ps...>(Ps(init)...);
      if (export_entry->tags & xe::cpu::ExportTag::kLog &&
          (!(export_entry->tags & xe::cpu::ExportTag::kHighFrequency) ||
           FLAGS_log_high_frequency_kernel_calls)) {
        PrintKernelCall(export_entry, params);
      }
      KernelTrampoline(FN, std::forward<std::tuple<Ps...>>(params),
                       std::make_index_sequence<sizeof...(Ps)>());
    }
  };
  export_entry->function_data.trampoline = &X::Trampoline;
  return export_entry;
}

}  // namespace shim

using xe::cpu::ExportTag;

#define DECLARE_EXPORT(module_name, name, tags)                            \
  const auto EXPORT_##module_name##_##name = RegisterExport_##module_name( \
      xe::kernel::shim::RegisterExport<                                    \
          xe::kernel::shim::KernelModuleId::module_name, ordinals::name>(  \
          &name, #name, tags));

#define DECLARE_XAM_EXPORT(name, tags) DECLARE_EXPORT(xam, name, tags)
#define DECLARE_XBDM_EXPORT(name, tags) DECLARE_EXPORT(xbdm, name, tags)
#define DECLARE_XBOXKRNL_EXPORT(name, tags) DECLARE_EXPORT(xboxkrnl, name, tags)

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_SHIM_UTILS_H_
