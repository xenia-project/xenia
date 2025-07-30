#include "xenia/cpu/backend/a64/a64_code_cache.h"

#if XE_PLATFORM_MAC

#include <mach/mach.h>

extern "C" {

// From libunwind/src/libunwind.h
struct unw_info_t {
  uintptr_t start_ip;
  uintptr_t end_ip;
  uintptr_t unwind_info;
};

extern int __unw_add_find_dynamic_proc(unw_info_t* unw_info);

}  // extern "C"

namespace xe {
namespace cpu {
namespace backend {
namespace a64 {

void unwind_add(uintptr_t function_address, uint32_t unwind_info,
                size_t function_size) {
  unw_info_t unw_info;
  unw_info.start_ip = function_address;
  unw_info.end_ip = function_address + function_size;
  unw_info.unwind_info = unwind_info;
  __unw_add_find_dynamic_proc(&unw_info);
}

}  // namespace a64
}  // namespace backend
}  // namespace cpu
}  // namespace xe

#endif  // XE_PLATFORM_MAC
