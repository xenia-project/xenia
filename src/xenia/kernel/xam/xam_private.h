/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_KERNEL_XAM_PRIVATE_H_
#define XENIA_KERNEL_XAM_PRIVATE_H_

#include <xenia/common.h>
#include <xenia/core.h>

#include <xenia/kernel/xam/xam_ordinals.h>


namespace xe {
namespace kernel {
namespace xam {

class XamState;


// This is a global object initialized with the XamModule.
// It references the current xam state object that all xam methods should
// be using to stash their variables.
extern XamState* shared_xam_state_;


// Registration functions, one per file.
void RegisterContentExports(ExportResolver* export_resolver, XamState* state);
void RegisterInfoExports(ExportResolver* export_resolver, XamState* state);
void RegisterInputExports(ExportResolver* export_resolver, XamState* state);
void RegisterNetExports(ExportResolver* export_resolver, XamState* state);
void RegisterNotifyExports(ExportResolver* export_resolver, XamState* state);
void RegisterUserExports(ExportResolver* export_resolver, XamState* state);
void RegisterVideoExports(ExportResolver* export_resolver, XamState* state);


}  // namespace xam
}  // namespace kernel
}  // namespace xe


#endif  // XENIA_KERNEL_XAM_PRIVATE_H_
