/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2018 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_D3D12_D3D12_API_H_
#define XENIA_UI_D3D12_D3D12_API_H_

// This must be included before D3D and DXGI for things like NOMINMAX.
#include "xenia/base/platform_win.h"

#include <DXProgrammableCapture.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <dxgi1_4.h>

#define XELOGD3D XELOGI

#endif  // XENIA_UI_D3D12_D3D12_API_H_
