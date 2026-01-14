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

// Must be included before D3D and DXGI for things like NOMINMAX.
#include "xenia/base/platform_win.h"

// Include the up to date versions of the headers of DXGI and Direct3D 12 rather
// than the ones from the Windows SDK before other headers that may also
// potentially include their headers.

#include "xenia/ui/dxgi_include_win.h"

#include "third_party/DirectX-Headers/include/directx/d3d12.h"
#include "third_party/DirectX-Headers/include/directx/d3d12sdklayers.h"

#include <DXProgrammableCapture.h>
#include <d3dcompiler.h>
#include <dxgidebug.h>
// For Microsoft::WRL::ComPtr.
#include <wrl/client.h>

#include "third_party/DirectXShaderCompiler/include/dxc/dxcapi.h"
#include "third_party/DirectXShaderCompiler/projects/dxilconv/include/DxbcConverter.h"

#define XELOGD3D XELOGI

#endif  // XENIA_UI_D3D12_D3D12_API_H_
