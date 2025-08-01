/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2022 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include "xenia/ui/surface_mac.h"

#import <Cocoa/Cocoa.h>
#import <QuartzCore/QuartzCore.h>

namespace xe {
namespace ui {

bool MacNSViewSurface::GetSizeImpl(uint32_t& width_out,
                                   uint32_t& height_out) const {
  if (!view_) {
    width_out = 0;
    height_out = 0;
    return false;
  }

  NSRect bounds = [view_ bounds];
  NSRect backingBounds = [view_ convertRectToBacking:bounds];
  
  width_out = static_cast<uint32_t>(backingBounds.size.width);
  height_out = static_cast<uint32_t>(backingBounds.size.height);
  
  return true;
}

CAMetalLayer* MacNSViewSurface::GetOrCreateMetalLayer() const {
  if (!view_) {
    return nullptr;
  }

  // Check if the view already has a CAMetalLayer
  CALayer* layer = [view_ layer];
  if ([layer isKindOfClass:[CAMetalLayer class]]) {
    return static_cast<CAMetalLayer*>(layer);
  }

  // Create and set a new CAMetalLayer
  CAMetalLayer* metalLayer = [CAMetalLayer layer];
  [view_ setWantsLayer:YES];
  [view_ setLayer:metalLayer];
  
  // Configure the metal layer
  metalLayer.framebufferOnly = YES;
  metalLayer.opaque = YES;

  return metalLayer;
}

}  // namespace ui
}  // namespace xe
