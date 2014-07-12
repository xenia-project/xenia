/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XBOXKRNL_VIDEO_H_
#define XENIA_KERNEL_XBOXKRNL_VIDEO_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/xbox.h>


namespace xe {
namespace kernel {


// http://ffplay360.googlecode.com/svn/trunk/Common/XTLOnPC.h
#pragma pack(push, 1)
typedef struct {
    uint32_t display_width;
    uint32_t display_height;
    uint32_t is_interlaced;
    uint32_t is_widescreen;
    uint32_t is_hi_def;
    float    refresh_rate;
    uint32_t video_standard;
    uint32_t unknown_0x8a;
    uint32_t unknown_0x01;
    uint32_t reserved[3];
}
X_VIDEO_MODE;
#pragma pack(pop)
static_assert_size(X_VIDEO_MODE, 48);

void xeVdGetCurrentDisplayGamma(uint32_t* arg0, float* arg1);
uint32_t xeVdQueryVideoFlags();
void xeVdQueryVideoMode(X_VIDEO_MODE *video_mode, bool swap);

void xeVdInitializeEngines(uint32_t unk0, uint32_t callback, uint32_t unk1,
                           uint32_t unk2_ptr, uint32_t unk3_ptr);
void xeVdSetGraphicsInterruptCallback(uint32_t callback, uint32_t user_data);
void xeVdEnableRingBufferRPtrWriteBack(uint32_t ptr, uint32_t block_size);


}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XBOXKRNL_VIDEO_H_
