/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2026 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_DXGI_INCLUDE_WIN_H_
#define XENIA_UI_DXGI_INCLUDE_WIN_H_

// Must be included before Windows headers for definitions like NOMINMAX.
#include "xenia/base/platform_win.h"

// Include the up to date versions of the DXGI header dependencies rather than
// the ones from the Windows SDK before including the DXGI header.
#include "third_party/DirectX-Headers/include/directx/dxgicommon.h"
#include "third_party/DirectX-Headers/include/directx/dxgiformat.h"

#include <dxgi1_5.h>

#endif  // XENIA_UI_DXGI_INCLUDE_WIN_H_
