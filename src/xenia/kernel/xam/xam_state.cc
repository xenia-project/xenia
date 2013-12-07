/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/kernel/xam/xam_state.h>

#include <xenia/emulator.h>


using namespace xe;
using namespace xe::kernel;
using namespace xe::kernel::xam;


namespace {

}


XamState::XamState(Emulator* emulator) :
    emulator_(emulator), memory_(emulator->memory()),
    export_resolver_(emulator->export_resolver()) {
}

XamState::~XamState() {
}
