/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2020 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_BASE_DMA_H_
#define XENIA_BASE_DMA_H_
#include "memory.h"
#include "threading.h"
namespace xe::dma {
struct XeDMAJob;
using DmaPrecall = void (*)(XeDMAJob* job);
using DmaPostcall = void (*)(XeDMAJob* job);
struct XeDMAJob {
  //threading::Event* signal_on_done;
  uintptr_t dmac_specific_;
  uint8_t* destination;
  uint8_t* source;
  size_t size;
  DmaPrecall precall;
  DmaPostcall postcall;
  void* userdata1;
  void* userdata2;
};
using DMACJobHandle = uint64_t;

class XeDMAC {
 public:
  virtual ~XeDMAC() {}
  virtual DMACJobHandle PushDMAJob(XeDMAJob* job) = 0;
  virtual void WaitJobDone(DMACJobHandle handle) = 0;
  virtual void WaitForIdle() = 0;
};

XeDMAC* CreateDMAC();
// must be divisible by cache line size
XE_NOINLINE
void vastcpy(uint8_t* XE_RESTRICT physaddr, uint8_t* XE_RESTRICT rdmapping,
             uint32_t written_length);

}  // namespace xe::dma

#endif  // XENIA_BASE_DMA_H_
