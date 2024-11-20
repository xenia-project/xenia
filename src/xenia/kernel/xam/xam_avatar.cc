/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2024 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/base/logging.h"
#include "xenia/kernel/kernel_state.h"
#include "xenia/kernel/util/shim_utils.h"
#include "xenia/kernel/xam/xam_private.h"
#include "xenia/xbox.h"

namespace xe {
namespace kernel {
namespace xam {

// Start/End
dword_result_t XamAvatarInitialize_entry(
    dword_t unk1,              // 1, 2, 4, etc
    dword_t unk2,              // 0 or 1
    dword_t processor_number,  // for thread creation?
    lpdword_t function_ptrs,   // 20b, 5 pointers
    lpunknown_t unk5,          // ptr in data segment
    dword_t unk6               // flags - 0x00300000, 0x30, etc
) {
  // This should only work for avatar editor to test avatar rendering
  auto title_id = kernel_state()->title_id();
  if (title_id == 0x584D07D1) {
    return X_STATUS_SUCCESS;
  }
  // Negative to fail. Game should immediately call XamAvatarShutdown.
  return ~0u;
}
DECLARE_XAM_EXPORT1(XamAvatarInitialize, kAvatars, kStub);

void XamAvatarShutdown_entry() {
  // Calls XMsgStartIORequestEx(0xf3,0x600002,0,0,0,0).
}
DECLARE_XAM_EXPORT1(XamAvatarShutdown, kAvatars, kStub);

// Get & Set
dword_result_t XamAvatarGetManifestLocalUser_entry(dword_t user_index,
                                                   lpdword_t avatar_metadata,
                                                   qword_t unk3) {
  // XMsgStartIORequestEx(0xf3, 0x600003, unk3, stack1, 8, 0)

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetManifestLocalUser, kAvatars, kStub);

dword_result_t XamAvatarGetManifestsByXuid_entry(dword_t unk1, qword_t unk2,
                                                 qword_t unk3, dword_t unk4,
                                                 dword_t unk5, qword_t unk6) {
  // Function_818D1738(0x600004,param_6,&uStack_d0,0x90,0)

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetManifestsByXuid, kAvatars, kStub)

dword_result_t XamAvatarGetAssetsResultSize_entry(qword_t unk1, qword_t unk2,
                                                  dword_t unk3) {
  // return XMsgInProcessCall(0xf3,0x600005,local_20,0);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetAssetsResultSize, kAvatars, kStub);

dword_result_t XamAvatarGetAssets_entry(qword_t unk1, word_t unk2, dword_t unk3,
                                        dword_t unk4, dword_t unk5,
                                        qword_t unk6) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetAssets, kAvatars, kStub);

dword_result_t XamAvatarSetCustomAsset_entry(qword_t unk1, qword_t unk2,
                                             dword_t unk3, dword_t unk4,
                                             dword_t unk5, dword_t unk6) {
  // XMsgInProcessCall(0xf3,0x60000a,local_30,0);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarSetCustomAsset, kAvatars, kStub)

dword_result_t XamAvatarSetManifest_entry(qword_t unk1, qword_t unk2,
                                          qword_t unk3) {
  // Function_818D1738(0x600009,param_3,&local_410,0x3ec,0);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarSetManifest, kAvatars, kStub);

dword_result_t XamAvatarGetMetadataRandom_entry(word_t body_type, word_t unk2,
                                                lpdword_t avatar_metadata,
                                                qword_t unk4) {
  // some_function(0x600010,param_4,local_28,0xc,local_30)

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetMetadataRandom, kAvatars, kStub);

dword_result_t XamAvatarGetMetadataSignedOutProfileCount_entry(dword_t unk1,
                                                               qword_t unk2) {
  // Function_818D1738(0x600013,param_2,local_10,4,0);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetMetadataSignedOutProfileCount, kAvatars, kStub);

dword_result_t XamAvatarGetMetadataSignedOutProfile_entry(dword_t unk1,
                                                          dword_t unk2,
                                                          qword_t unk3) {
  // Function_818D1738(0x600014,param_3,local_10,8,0);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetMetadataSignedOutProfile, kAvatars, kStub);

dword_result_t XamAvatarManifestGetBodyType_entry(qword_t body_type) {
  /* Notes:
      - Calls XMsgStartIORequestEx(0xf3, 0x60000F, unk4, stack1, 0x18,
     0x10000000)
      - return either char of 1 - male, 2 - female, else - unknown
  */

  return '1';
}
DECLARE_XAM_EXPORT1(XamAvatarManifestGetBodyType, kAvatars, kStub);

dword_result_t XamAvatarGetInstrumentation_entry(qword_t unk1, lpdword_t unk2) {
  return 1;
}
DECLARE_XAM_EXPORT1(XamAvatarGetInstrumentation, kAvatars, kStub);

dword_result_t XamAvatarGetAssetIcon_entry(lpqword_t unk1, dword_t unk2,
                                           dword_t unk3, dword_t unk4,
                                           qword_t unk5) {
  // XMsgStartIORequestEx(0xf3, 0x60000B, unk5, stack1, 0x1C, 0x10000000)

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetAssetIcon, kAvatars, kStub);

dword_result_t XamAvatarGetAssetBinary_entry(lpqword_t unk1, dword_t unk2,
                                             dword_t unk3, dword_t unk4,
                                             qword_t unk5) {
  // Function_818D1738(0x600008,param_5,&uStack_60,0x1c,auStack_70);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetAssetBinary, kAvatars, kStub);

dword_result_t XamAvatarGetInstalledAssetPackageDescription_entry(
    qword_t unk1, qword_t unk2) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGetInstalledAssetPackageDescription, kAvatars,
                    kStub);

void XamAvatarSetMocks_entry() {
  // No-op.
}
DECLARE_XAM_EXPORT1(XamAvatarSetMocks, kAvatars, kStub);

// Animation

const static std::map<uint64_t, std::string> XAnimationTypeMap = {
    // Animation Generic Stand
    {0x0040000000030003, "Animation Generic Stand 0"},
    {0x0040000000040003, "Animation Generic Stand 1"},
    {0x0040000000050003, "Animation Generic Stand 2"},
    {0x0040000000270003, "Animation Generic Stand 3"},
    {0x0040000000280003, "Animation Generic Stand 4"},
    {0x0040000000290003, "Animation Generic Stand 5"},
    {0x00400000002A0003, "Animation Generic Stand 6"},
    {0x00400000002B0003, "Animation Generic Stand 7"},
    // Animation Idle
    {0x0040000000130001, "Animation Male Idle Looks Around"},
    {0x0040000000140001, "Animation Male Idle Stretch"},
    {0x0040000000150001, "Animation Male Idle Shifts Weight"},
    {0x0040000000260001, "Animation Male Idle Checks Hand"},
    {0x0040000000090002, "Animation Female Idle Check Nails"},
    {0x00400000000A0002, "Animation Female Idle Looks Around"},
    {0x00400000000B0002, "Animation Female Idle Shifts Weight"},
    {0x00400000000C0002, "Animation Female Idle Fixes Shoe"},
};

// https://github.com/xenia-canary/xenia-canary/commit/212c99eee2724de15f471148d10197d89794ff32
dword_result_t XamAvatarLoadAnimation_entry(lpqword_t asset_id_ptr,
                                            dword_t flags, dword_t output,
                                            dword_t unk1) {
  /* Notes:
      - Calls XMsgStartIORequestEx(0xf3, 0x60000F, unk4, stack1, 0x18,
     0x10000000)
      - I cannot find an error code for when asset_id_ptr returns nullptr. It
     could be due to decompiling issues
  */
  assert_true(asset_id_ptr);
  std::string summary = "Request to load avatar animation: ";

  if (XAnimationTypeMap.find(*asset_id_ptr) != XAnimationTypeMap.cend()) {
    summary += XAnimationTypeMap.at(*asset_id_ptr);
  } else {
    summary += fmt::format("Unknown animation {}",
                           static_cast<uint64_t>(*asset_id_ptr));
  }

  XELOGD("{}", summary);
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarLoadAnimation, kAvatars, kStub);

// Enum
dword_result_t XamAvatarBeginEnumAssets_entry(qword_t unk1, word_t unk2,
                                              dword_t unk3, word_t unk4,
                                              word_t unk5, qword_t unk6) {
  // XMsgStartIORequestEx(0xf3, 0x60000c, unkn6, stack1, 0x14, stack2)

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarBeginEnumAssets, kAvatars, kStub);

dword_result_t XamAvatarEnumAssets_entry(lpqword_t unk1, dword_t unk2,
                                         qword_t unk3) {
  // XMsgStartIORequestEx(0xf3, 0x60000d, unk3, stack1, 8, stack2)

  return X_E_NO_MORE_FILES;  // Stop it from calling endlessly
}
DECLARE_XAM_EXPORT1(XamAvatarEnumAssets, kAvatars, kStub);

dword_result_t XamAvatarEndEnumAssets_entry(qword_t unk1) {
  // some_function(0x60000e,param_1,0,0,local_10)

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarEndEnumAssets, kAvatars, kStub);

// Other
dword_result_t XamAvatarGenerateMipMaps_entry(qword_t unk1, qword_t unk2,
                                              dword_t unk3, dword_t unk4,
                                              qword_t unk5) {
  // Function_818D1738(0x600007,param_5,local_30,0x10,local_40);

  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarGenerateMipMaps, kAvatars, kStub);

dword_result_t XamAvatarWearNow_entry(qword_t unk1, lpdword_t unk2,
                                      qword_t unk3) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarWearNow, kAvatars, kStub);

dword_result_t XamAvatarReinstallAwardedAsset_entry(qword_t unk1, qword_t unk2,
                                                    qword_t unk3) {
  return X_STATUS_SUCCESS;
}
DECLARE_XAM_EXPORT1(XamAvatarReinstallAwardedAsset, kAvatars, kStub);

}  // namespace xam
}  // namespace kernel
}  // namespace xe

DECLARE_XAM_EMPTY_REGISTER_EXPORTS(Avatar);
