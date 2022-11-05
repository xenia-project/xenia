#pragma once
// requires windows.h
#include <stdint.h>

namespace lightweight_nvapi {

using nvstatus_t = int;

using nvintfid_t = unsigned int;

#ifndef LIGHTWEIGHT_NVAPI_EXCLUDE_D3D12
constexpr nvintfid_t id_NvAPI_D3D12_QueryCpuVisibleVidmem = 0x26322BC3;

using cb_NvAPI_D3D12_QueryCpuVisibleVidmem = nvstatus_t (*)(
    ID3D12Device* pDevice, uint64_t* pTotalBytes, uint64_t* pFreeBytes);

constexpr nvintfid_t id_NvAPI_D3D12_UseDriverHeapPriorities = 0xF0D978A8;
using cb_NvAPI_D3D12_UseDriverHeapPriorities =
    nvstatus_t (*)(ID3D12Device* pDevice);
enum NV_D3D12_RESOURCE_FLAGS {
  NV_D3D12_RESOURCE_FLAG_NONE = 0,
  NV_D3D12_RESOURCE_FLAG_HTEX = 1,  //!< Create HTEX texture
  NV_D3D12_RESOURCE_FLAG_CPUVISIBLE_VIDMEM =
      2,  //!< Hint to create resource in cpuvisible vidmem
};

struct NV_RESOURCE_PARAMS {
  uint32_t version;  //!< Version of structure. Must always be first member
  NV_D3D12_RESOURCE_FLAGS
  NVResourceFlags;  //!< Additional NV specific flags (set the
                    //!< NV_D3D12_RESOURCE_FLAG_HTEX bit to create HTEX
                    //!< texture)
};

using cb_NvAPI_D3D12_CreateCommittedResource = nvstatus_t (*)(
    ID3D12Device* pDevice, const D3D12_HEAP_PROPERTIES* pHeapProperties,
    D3D12_HEAP_FLAGS HeapFlags, const D3D12_RESOURCE_DESC* pDesc,
    D3D12_RESOURCE_STATES InitialState,
    const D3D12_CLEAR_VALUE* pOptimizedClearValue,
    const NV_RESOURCE_PARAMS* pNVResourceParams, REFIID riid,
    void** ppvResource, bool* pSupported);
constexpr nvintfid_t id_NvAPI_D3D12_CreateCommittedResource = 0x27E98AEu;
#endif
class nvapi_state_t {
  HMODULE nvapi64_;
  void* (*queryinterface_)(unsigned int intfid);
  bool available_;
  bool init_ptrs();

  bool call_init_interface();
  void call_deinit_interface();

 public:
  nvapi_state_t() : nvapi64_(LoadLibraryA("nvapi64.dll")), available_(false) {
    available_ = init_ptrs();
  }
  ~nvapi_state_t();
  template <typename T>
  T* query_interface(unsigned int intfid) {
    if (queryinterface_ == nullptr) {
      return nullptr;
    }
    return reinterpret_cast<T*>(queryinterface_(intfid));
  }

  bool is_available() const { return available_; }
};
inline bool nvapi_state_t::call_init_interface() {
  int result = -1;
  auto initInterfaceEx = query_interface<int(int)>(0xAD298D3F);
  if (!initInterfaceEx) {
    auto initInterface = query_interface<int()>(0x150E828u);
    if (initInterface) {
      result = initInterface();
    }
  } else {
    result = initInterfaceEx(0);
  }
  return result == 0;
}
inline void nvapi_state_t::call_deinit_interface() {
  auto deinitinterfaceex = query_interface<void(int)>(0xD7C61344);
  if (deinitinterfaceex) {
    deinitinterfaceex(1);  // or 0? im not sure what the proper value is
  } else {
    auto deinitinterface = query_interface<void()>(0xD22BDD7E);
    if (deinitinterface) {
      deinitinterface();
    }
  }
}
inline bool nvapi_state_t::init_ptrs() {
  if (!nvapi64_) return false;
  queryinterface_ = reinterpret_cast<void* (*)(unsigned)>(
      GetProcAddress(nvapi64_, "nvapi_QueryInterface"));

  if (!queryinterface_) {
    return false;
  }
  if (!call_init_interface()) {
    return false;
  }

  return true;
}
inline nvapi_state_t::~nvapi_state_t() {
  if (available_) {
    call_deinit_interface();
  }
}
}  // namespace lightweight_nvapi