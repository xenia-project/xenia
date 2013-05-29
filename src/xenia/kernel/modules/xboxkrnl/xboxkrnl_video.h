/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_MODULES_XBOXKRNL_VIDEO_H_
#define XENIA_KERNEL_MODULES_XBOXKRNL_VIDEO_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xbox.h>


namespace xe {
namespace kernel {
namespace xboxkrnl {


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
    uint32_t Reserved[5];
}
X_VIDEO_MODE;
#pragma pack(pop)
XEASSERTSTRUCTSIZE(X_VIDEO_MODE, 48);

void xeVdQueryVideoMode(X_VIDEO_MODE *video_mode, bool swap);

void xeVdInitializeEngines(uint32_t unk0, uint32_t callback, uint32_t unk1,
                           uint32_t unk2_ptr, uint32_t unk3_ptr);
void xeVdSetGraphicsInterruptCallback(uint32_t callback, uint32_t user_data);


}  // namespace xboxkrnl
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_MODULES_XBOXKRNL_VIDEO_H_
