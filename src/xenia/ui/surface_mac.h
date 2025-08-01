/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_SURFACE_MAC_H_
#define XENIA_UI_SURFACE_MAC_H_

#include <cstdint>
#include <memory>

#include "xenia/ui/surface.h"

#ifdef __OBJC__
@class NSView;
@class CAMetalLayer;
#else
typedef struct objc_object NSView;
// CAMetalLayer forward declaration - may be typedef'd by Vulkan headers
#ifndef VULKAN_METAL_H_
typedef struct objc_object CAMetalLayer;
#endif
#endif

namespace xe {
namespace ui {

class MacNSViewSurface final : public Surface {
 public:
  explicit MacNSViewSurface(NSView* view) : view_(view) {}
  TypeIndex GetType() const override { return kTypeIndex_MacNSView; }
  NSView* view() const { return view_; }
  CAMetalLayer* GetOrCreateMetalLayer() const;

 protected:
  bool GetSizeImpl(uint32_t& width_out, uint32_t& height_out) const override;

 private:
  NSView* view_;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_SURFACE_MAC_H_
