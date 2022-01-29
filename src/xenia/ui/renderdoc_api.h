/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_RENDERDOC_API_H_
#define XENIA_UI_RENDERDOC_API_H_

#include "third_party/renderdoc/renderdoc_app.h"

namespace xe {
namespace ui {

class RenderdocApi {
 public:
  RenderdocApi() = default;
  RenderdocApi(const RenderdocApi& renderdoc_api) = delete;
  RenderdocApi& operator=(const RenderdocApi& renderdoc_api) = delete;
  ~RenderdocApi() { Shutdown(); }

  bool Initialize();
  void Shutdown();

  // nullptr if not attached.
  const RENDERDOC_API_1_0_0* api_1_0_0() const { return api_1_0_0_; }

 private:
  void* library_ = nullptr;
  const RENDERDOC_API_1_0_0* api_1_0_0_ = nullptr;
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_RENDERDOC_API_H_