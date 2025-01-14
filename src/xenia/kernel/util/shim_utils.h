/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_UTIL_SHIM_UTILS_H_
#define XENIA_KERNEL_UTIL_SHIM_UTILS_H_

#include <cstring>
#include <string>
#include <type_traits>

#include "third_party/fmt/include/fmt/format.h"
#include "xenia/base/byte_order.h"
#include "xenia/base/logging.h"
#include "xenia/base/memory.h"
#include "xenia/base/string_buffer.h"
#include "xenia/cpu/export_resolver.h"
#include "xenia/cpu/ppc/ppc_context.h"
#include "xenia/kernel/kernel_flags.h"
#include "xenia/kernel/kernel_state.h"

namespace xe {
namespace kernel {

using PPCContext = xe::cpu::ppc::PPCContext;

#define SHIM_CALL void
#define SHIM_SET_MAPPING(library_name, export_name, shim_data) \
  export_resolver->SetFunctionMapping(                         \
      library_name, ordinals::export_name,                     \
      (xe::cpu::xe_kernel_export_shim_fn)export_name##_entry);

#define SHIM_MEM_ADDR(a) ((a) ? ppc_context->TranslateVirtual(a) : nullptr)

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

inline std::string_view TranslateAnsiString(const Memory* memory,
                                            const X_ANSI_STRING* ansi_string) {
  if (!ansi_string || !ansi_string->length) {
    return "";
  }
  return std::string_view(
      memory->TranslateVirtual<const char*>(ansi_string->pointer),
      ansi_string->length);
}

inline std::string TranslateAnsiPath(const Memory* memory,
                                     const X_ANSI_STRING* ansi_string) {
  return string_util::trim(
      std::string(TranslateAnsiString(memory, ansi_string)));
}

inline std::string_view TranslateAnsiStringAddress(const Memory* memory,
                                                   uint32_t guest_address) {
  if (!guest_address) {
    return "";
  }
  return TranslateAnsiString(
      memory, memory->TranslateVirtual<const X_ANSI_STRING*>(guest_address));
}

inline std::u16string TranslateUnicodeString(
    const Memory* memory, const X_UNICODE_STRING* unicode_string) {
  if (!unicode_string) {
    return u"";
  }
  uint16_t length = unicode_string->length;
  if (!length) {
    return u"";
  }
  const xe::be<uint16_t>* guest_string =
      memory->TranslateVirtual<const xe::be<uint16_t>*>(
          unicode_string->pointer);
  std::u16string translated_string;
  translated_string.reserve(length);
  for (uint16_t i = 0; i < length / sizeof(uint16_t); ++i) {
    translated_string += char16_t(uint16_t(guest_string[i]));
  }
  return translated_string;
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
  explicit Param(Init& init) : ordinal_(init.ordinal++) {}

  template <typename V>
  void LoadValue(Init& init, V* out_value) {
    if (ordinal_ <= 7) {
      *out_value = V(init.ppc_context->r[3 + ordinal_]);
    } else {
      uint32_t stack_ptr =
          uint32_t(init.ppc_context->r[1]) + 0x54 + (ordinal_ - 8) * 8;
      *out_value =
          xe::load_and_swap<V>(init.ppc_context->TranslateVirtual(stack_ptr));
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

class ContextParam : public Param {
 public:
  ContextParam() : Param(), ctx_(nullptr) {}
  ContextParam(PPCContext* value) : Param(), ctx_(value) {}
  ContextParam(Init& init) : Param(init), ctx_(init.ppc_context) {}

  operator PPCContext*() const { return ctx_; }
  PPCContext* value() const { return ctx_; }

  PPCContext* operator->() const { return ctx_; }

  template <typename T>
  inline T TranslateVirtual(uint32_t guest_addr) const {
    return ctx_->TranslateVirtual<T>(guest_addr);
  }

  template <typename T>
  inline T TranslateGPR(uint32_t which_gpr) const {
    return ctx_->TranslateVirtualGPR<T>(ctx_->r[which_gpr]);
  }

  X_KPCR* GetPCR() const { return TranslateGPR<X_KPCR*>(13); }

  XThread* CurrentXThread() const;

 protected:
  PPCContext* XE_RESTRICT ctx_;
};

class PointerParam : public ParamBase<uint32_t> {
 public:
  PointerParam(Init& init) : ParamBase(init) {
    host_ptr_ = value_ ? init.ppc_context->TranslateVirtual(value_) : nullptr;
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
    host_ptr_ = value_ ? init.ppc_context->TranslateVirtual<xe::be<T>*>(value_)
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
    host_ptr_ =
        value_ ? init.ppc_context->TranslateVirtual<CHAR*>(value_) : nullptr;
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
        value_ ? init.ppc_context->TranslateVirtual<T*>(value_) : nullptr;
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

class Result {
 public:
  virtual void Store(PPCContext* ppc_context) = 0;
};

template <typename T>
class ResultBase : public Result {
 public:
  ResultBase(T value) : value_(value) {}
  void Store(PPCContext* ppc_context) {
    ppc_context->r[3] = uint64_t(int32_t(value_));
  }
  ResultBase() = delete;
  ResultBase& operator=(const ResultBase&) = delete;
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
using lpu16string_t = const shim::StringPointerParam<char16_t, std::u16string>&;
using function_t = const shim::ParamBase<uint32_t>&;
using unknown_t = const shim::ParamBase<uint32_t>&;
using lpunknown_t = const shim::PointerParam&;
template <typename T>
using pointer_t = const shim::TypedPointerParam<T>&;

using int_result_t = shim::ResultBase<int32_t>;
using dword_result_t = shim::ResultBase<uint32_t>;
using qword_result_t = shim::ResultBase<uint64_t>;
using pointer_result_t = shim::ResultBase<uint32_t>;
using X_HRESULT_result_t = shim::ResultBase<X_HRESULT>;
using ppc_context_t = shim::ContextParam;

// Exported from kernel_state.cc.
KernelState* kernel_state();
inline Memory* kernel_memory() { return kernel_state()->memory(); }

namespace shim {

inline void AppendParam(StringBuffer* string_buffer, int_t param) {
  string_buffer->AppendFormat("{}", int32_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, word_t param) {
  string_buffer->AppendFormat("{:04X}", uint16_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, dword_t param) {
  string_buffer->AppendFormat("{:08X}", uint32_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, qword_t param) {
  string_buffer->AppendFormat("{:016X}", uint64_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, float_t param) {
  string_buffer->AppendFormat("{:G}", static_cast<float>(param));
}
inline void AppendParam(StringBuffer* string_buffer, double_t param) {
  string_buffer->AppendFormat("{:G}", static_cast<double>(param));
}
inline void AppendParam(StringBuffer* string_buffer, lpvoid_t param) {
  string_buffer->AppendFormat("{:08X}", uint32_t(param));
}
inline void AppendParam(StringBuffer* string_buffer, lpdword_t param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("({:08X})", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpqword_t param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("({:016X})", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpfloat_t param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("({:G})", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpdouble_t param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("({:G})", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, ppc_context_t param) {
  string_buffer->Append("ContextArg");
}
inline void AppendParam(StringBuffer* string_buffer, lpstring_t param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("({})", param.value());
  }
}
inline void AppendParam(StringBuffer* string_buffer, lpu16string_t param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
  if (param) {
    string_buffer->AppendFormat("({})", xe::to_utf8(param.value()));
  }
}
inline void AppendParam(StringBuffer* string_buffer,
                        pointer_t<X_OBJECT_ATTRIBUTES> record) {
  string_buffer->AppendFormat("{:08X}", record.guest_address());
  if (record) {
    auto name_string =
        kernel_memory()->TranslateVirtual<X_ANSI_STRING*>(record->name_ptr);
    std::string_view name =
        name_string == nullptr
            ? "(null)"
            : util::TranslateAnsiString(kernel_memory(), name_string);
    string_buffer->AppendFormat("({:08X},{},{:08X})",
                                uint32_t(record->root_directory), name,
                                uint32_t(record->attributes));
  }
}
inline void AppendParam(StringBuffer* string_buffer,
                        pointer_t<X_EX_TITLE_TERMINATE_REGISTRATION> reg) {
  string_buffer->AppendFormat("{:08X}({:08X}, {:08X})", reg.guest_address(),
                              static_cast<uint32_t>(reg->notification_routine),
                              static_cast<uint32_t>(reg->priority));
}
inline void AppendParam(StringBuffer* string_buffer,
                        pointer_t<X_EXCEPTION_RECORD> record) {
  string_buffer->AppendFormat("{:08X}({:08X})", record.guest_address(),
                              uint32_t(record->code));
}
template <typename T>
void AppendParam(StringBuffer* string_buffer, pointer_t<T> param) {
  string_buffer->AppendFormat("{:08X}", param.guest_address());
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
XE_NOALIAS void PrintKernelCall(cpu::Export* export_entry,
                                const Tuple& params) {
  auto& string_buffer = *thread_local_string_buffer();
  string_buffer.Reset();
  string_buffer.Append(export_entry->name);
  string_buffer.Append('(');
  AppendKernelCallParams(string_buffer, export_entry, params);
  string_buffer.Append(')');
  if (export_entry->tags & xe::cpu::ExportTag::kImportant) {
    xe::logging::AppendLogLine(xe::LogLevel::Info, 'i',
                               string_buffer.to_string_view(), LogSrc::Kernel);
  } else {
    xe::logging::AppendLogLine(xe::LogLevel::Debug, 'd',
                               string_buffer.to_string_view(), LogSrc::Kernel);
  }
}
/*
        todo: need faster string formatting/concatenation (all arguments are
   always turned into strings except if kHighFrequency)

*/
template <typename F, typename Tuple, std::size_t... I>
XE_FORCEINLINE static auto KernelTrampoline(F&& f, Tuple&& t,
                                            std::index_sequence<I...>) {
  return std::forward<F>(f)(std::get<I>(std::forward<Tuple>(t))...);
}

template <KernelModuleId MODULE, uint16_t ORDINAL, typename R, typename... Ps>
struct ExportRegistrerHelper {
  template <R (*fn)(Ps&...), xe::cpu::ExportTag::type tags>
  static xe::cpu::Export* RegisterExport(const char* name) {
    static_assert(
        std::is_void<R>::value || std::is_base_of<shim::Result, R>::value,
        "R must be void or derive from shim::Result");
    static_assert((std::is_base_of_v<shim::Param, Ps> && ...),
                  "Ps must derive from shim::Param");
    constexpr auto TAGS =
        tags | xe::cpu::ExportTag::kImplemented | xe::cpu::ExportTag::kLog;

    static const auto export_entry =
        new cpu::Export(ORDINAL, xe::cpu::Export::Type::kFunction, name, TAGS);
    struct X {
      static void Trampoline(PPCContext* ppc_context) {
        Param::Init init = {
            ppc_context,
            0,
        };
        // Using braces initializer instead of make_tuple because braces
        // enforce execution order across compilers.
        // The make_tuple order is undefined per the C++ standard and
        // cause inconsitencies between msvc and clang.
        std::tuple<Ps...> params = {Ps(init)...};
        if (TAGS & xe::cpu::ExportTag::kLog &&
            (!(TAGS & xe::cpu::ExportTag::kHighFrequency) ||
             cvars::log_high_frequency_kernel_calls)) {
          PrintKernelCall(export_entry, params);
        }
        if constexpr (std::is_void<R>::value) {
          KernelTrampoline(fn, std::forward<std::tuple<Ps...>>(params),
                           std::make_index_sequence<sizeof...(Ps)>());
        } else {
          auto result =
              KernelTrampoline(fn, std::forward<std::tuple<Ps...>>(params),
                               std::make_index_sequence<sizeof...(Ps)>());
          result.Store(ppc_context);
          if (TAGS &
              (xe::cpu::ExportTag::kLog | xe::cpu::ExportTag::kLogResult)) {
            // TODO(benvanik): log result.
          }
        }
      }
    };
    struct Y {
      static void Trampoline(PPCContext* ppc_context) {
        Param::Init init = {
            ppc_context,
            0,
        };
        std::tuple<Ps...> params = {Ps(init)...};
        if constexpr (std::is_void<R>::value) {
          KernelTrampoline(fn, std::forward<std::tuple<Ps...>>(params),
                           std::make_index_sequence<sizeof...(Ps)>());
        } else {
          auto result =
              KernelTrampoline(fn, std::forward<std::tuple<Ps...>>(params),
                               std::make_index_sequence<sizeof...(Ps)>());
          result.Store(ppc_context);
        }
      }
    };
    export_entry->function_data.trampoline = &X::Trampoline;
    return export_entry;
  }
};
template <KernelModuleId MODULE, uint16_t ORDINAL, typename R, typename... Ps>
auto GetRegister(R (*fngetter)(Ps&...)) {
  return static_cast<ExportRegistrerHelper<MODULE, ORDINAL, R, Ps...>*>(
      nullptr);
}

}  // namespace shim

using xe::cpu::ExportTag;

#define DECLARE_EXPORT(module_name, name, category, tags)                  \
  using _register_##module_name##_##name =                                 \
      std::remove_cv_t<std::remove_reference_t<                            \
          decltype(*xe::kernel::shim::GetRegister<                         \
                   xe::kernel::shim::KernelModuleId::module_name,          \
                   ordinals::name>(&name##_entry))>>;                      \
  const auto EXPORT_##module_name##_##name = RegisterExport_##module_name( \
      _register_##module_name##_##name ::RegisterExport<                   \
          &name##_entry, tags | (static_cast<xe::cpu::ExportTag::type>(    \
                                     xe::cpu::ExportCategory::category)    \
                                 << xe::cpu::ExportTag::CategoryShift)>(   \
          #name));

#define DECLARE_EMPTY_REGISTER_EXPORTS(module_name, group_name) \
  void xe::kernel::module_name::Register##group_name##Exports(  \
      xe::cpu::ExportResolver* export_resolver,                 \
      xe::kernel::KernelState* kernel_state) {}

#define DECLARE_XAM_EXPORT_(name, category, tags) \
  DECLARE_EXPORT(xam, name, category, tags)
#define DECLARE_XAM_EXPORT1(name, category, tag) \
  DECLARE_EXPORT(xam, name, category, xe::cpu::ExportTag::tag)
#define DECLARE_XAM_EXPORT2(name, category, tag1, tag2) \
  DECLARE_EXPORT(xam, name, category,                   \
                 xe::cpu::ExportTag::tag1 | xe::cpu::ExportTag::tag2)

#define DECLARE_XAM_EMPTY_REGISTER_EXPORTS(group_name) \
  DECLARE_EMPTY_REGISTER_EXPORTS(xam, group_name)

#define DECLARE_XBDM_EXPORT_(name, category, tags) \
  DECLARE_EXPORT(xbdm, name, category, tags)
#define DECLARE_XBDM_EXPORT1(name, category, tag) \
  DECLARE_EXPORT(xbdm, name, category, xe::cpu::ExportTag::tag)

#define DECLARE_XBDM_EMPTY_REGISTER_EXPORTS(group_name) \
  DECLARE_EMPTY_REGISTER_EXPORTS(xbdm, group_name)

#define DECLARE_XBOXKRNL_EXPORT_(name, category, tags) \
  DECLARE_EXPORT(xboxkrnl, name, category, tags)
#define DECLARE_XBOXKRNL_EXPORT1(name, category, tag) \
  DECLARE_EXPORT(xboxkrnl, name, category, xe::cpu::ExportTag::tag)
#define DECLARE_XBOXKRNL_EXPORT2(name, category, tag1, tag2) \
  DECLARE_EXPORT(xboxkrnl, name, category,                   \
                 xe::cpu::ExportTag::tag1 | xe::cpu::ExportTag::tag2)
#define DECLARE_XBOXKRNL_EXPORT3(name, category, tag1, tag2, tag3)     \
  DECLARE_EXPORT(xboxkrnl, name, category,                             \
                 xe::cpu::ExportTag::tag1 | xe::cpu::ExportTag::tag2 | \
                     xe::cpu::ExportTag::tag3)
#define DECLARE_XBOXKRNL_EXPORT4(name, category, tag1, tag2, tag3, tag4) \
  DECLARE_EXPORT(xboxkrnl, name, category,                               \
                 xe::cpu::ExportTag::tag1 | xe::cpu::ExportTag::tag2 |   \
                     xe::cpu::ExportTag::tag3 | xe::cpu::ExportTag::tag4)

#define DECLARE_XBOXKRNL_EMPTY_REGISTER_EXPORTS(group_name) \
  DECLARE_EMPTY_REGISTER_EXPORTS(xboxkrnl, group_name)

}  // namespace kernel
}  // namespace xe

#endif  // XENIA_KERNEL_UTIL_SHIM_UTILS_H_
