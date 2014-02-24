/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#include <xenia/hid/hid.h>
#include <xenia/hid/hid-private.h>



using namespace xe;
using namespace xe::hid;


DEFINE_string(hid, "any",
    "Input system. Use: [any, nop, winkey, xinput]");


#include <xenia/hid/nop/nop_hid.h>
#if XE_PLATFORM_WIN32
#include <xenia/hid/winkey/winkey_hid.h>
#include <xenia/hid/xinput/xinput_hid.h>
#endif  // WIN32


InputSystem* xe::hid::Create(Emulator* emulator) {
  //return xe::hid::nop::Create(emulator);
  InputSystem* input_system = new InputSystem(emulator);

  if (FLAGS_hid.compare("nop") == 0) {
    input_system->AddDriver(xe::hid::nop::Create(input_system));
#if XE_PLATFORM_WIN32
  } else if (FLAGS_hid.compare("winkey") == 0) {
    input_system->AddDriver(xe::hid::winkey::Create(input_system));
  } else if (FLAGS_hid.compare("xinput") == 0) {
    input_system->AddDriver(xe::hid::xinput::Create(input_system));
#endif  // WIN32
  } else {
    // Create all available.
    bool any_created = false;

    // NOTE: in any mode we create as many as we can, falling back to nop.

#if XE_PLATFORM_WIN32
    InputDriver* winkey_driver = xe::hid::winkey::Create(input_system);
    if (winkey_driver) {
      input_system->AddDriver(winkey_driver);
      any_created = true;
    }
    InputDriver* xinput_driver = xe::hid::xinput::Create(input_system);
    if (xinput_driver) {
      input_system->AddDriver(xinput_driver);
      any_created = true;
    }
#endif  // WIN32

    // Fallback to nop if none created.
    if (!any_created) {
      input_system->AddDriver(xe::hid::nop::Create(input_system));
    }
  }

  return input_system;
}
