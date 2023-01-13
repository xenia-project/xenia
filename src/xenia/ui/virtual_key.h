/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2021 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

#ifndef XENIA_UI_VIRTUAL_KEY_H_
#define XENIA_UI_VIRTUAL_KEY_H_

#include <cstdint>

namespace xe {
namespace ui {

// Windows and Xbox 360 / XInput virtual key enumeration.
// This is what platform-specific keys should be translated to, for both HID
// keystroke emulation and Xenia-internal UI events. On Windows, the translation
// is a simple cast.
// This is uint16_t as it's WPARAM (which was 16-bit back in Win16 days, where
// virtual key codes were added), and XINPUT_KEYSTROKE stores the virtual key as
// a WORD. In some cases (see kPacket), bits above 16 may be used as well, but
// VK_ on Windows are defined up to 0xFF (0xFE not counting the reserved 0xFF)
// as of Windows SDK 10.0.19041.0, and XInput virtual key codes are 16-bit.
// Base virtual key codes as of _WIN32_WINNT 0x0500 (Windows 2000, which the
// Xbox 360's kernel is based on), virtual key codes added later are marked
// explicitly as such.
enum class VirtualKey : uint16_t {
  // Not a valid key - MapVirtualKey returns zero when there is no translation.
  kNone,

  kLButton = 0x01,
  kRButton = 0x02,
  kCancel = 0x03,   // Control-break.
  kMButton = 0x04,  // Not contiguous with kLButton and kRButton.

  kXButton1 = 0x05,  // Not contiguous with kLButton and kRButton.
  kXButton2 = 0x06,  // Not contiguous with kLButton and kRButton.

  kBack = 0x08,  // Backspace.
  kTab = 0x09,

  kClear = 0x0C,
  kReturn = 0x0D,  // Enter.

  kShift = 0x10,
  kControl = 0x11,  // Ctrl.
  kMenu = 0x12,     // Alt.
  kPause = 0x13,
  kCapital = 0x14,  // Caps Lock.

  kKana = 0x15,
  kHangeul = 0x15,  // Old name.
  kHangul = 0x15,
  kImeOn = 0x16,
  kJunja = 0x17,
  kFinal = 0x18,
  kHanja = 0x19,
  kKanji = 0x19,
  kImeOff = 0x1A,

  kEscape = 0x1B,

  kConvert = 0x1C,
  kNonConvert = 0x1D,
  kAccept = 0x1E,
  kModeChange = 0x1F,

  kSpace = 0x20,
  kPrior = 0x21,  // Page Up.
  kNext = 0x22,   // Page Down.
  kEnd = 0x23,
  kHome = 0x24,
  kLeft = 0x25,
  kUp = 0x26,
  kRight = 0x27,
  kDown = 0x28,
  kSelect = 0x29,
  kPrint = 0x2A,
  kExecute = 0x2B,
  kSnapshot = 0x2C,
  kInsert = 0x2D,
  kDelete = 0x2E,
  kHelp = 0x2F,

  // Same as ASCII '0' - '9'.
  k0 = 0x30,
  k1 = 0x31,
  k2 = 0x32,
  k3 = 0x33,
  k4 = 0x34,
  k5 = 0x35,
  k6 = 0x36,
  k7 = 0x37,
  k8 = 0x38,
  k9 = 0x39,

  // Same as ASCII 'A' - 'Z'.
  kA = 0x41,
  kB = 0x42,
  kC = 0x43,
  kD = 0x44,
  kE = 0x45,
  kF = 0x46,
  kG = 0x47,
  kH = 0x48,
  kI = 0x49,
  kJ = 0x4A,
  kK = 0x4B,
  kL = 0x4C,
  kM = 0x4D,
  kN = 0x4E,
  kO = 0x4F,
  kP = 0x50,
  kQ = 0x51,
  kR = 0x52,
  kS = 0x53,
  kT = 0x54,
  kU = 0x55,
  kV = 0x56,
  kW = 0x57,
  kX = 0x58,
  kY = 0x59,
  kZ = 0x5A,

  kLWin = 0x5B,
  kRWin = 0x5C,
  kApps = 0x5D,

  kSleep = 0x5F,

  kNumpad0 = 0x60,
  kNumpad1 = 0x61,
  kNumpad2 = 0x62,
  kNumpad3 = 0x63,
  kNumpad4 = 0x64,
  kNumpad5 = 0x65,
  kNumpad6 = 0x66,
  kNumpad7 = 0x67,
  kNumpad8 = 0x68,
  kNumpad9 = 0x69,
  kMultiply = 0x6A,
  kAdd = 0x6B,
  kSeparator = 0x6C,
  kSubtract = 0x6D,
  kDecimal = 0x6E,
  kDivide = 0x6F,
  kF1 = 0x70,
  kF2 = 0x71,
  kF3 = 0x72,
  kF4 = 0x73,
  kF5 = 0x74,
  kF6 = 0x75,
  kF7 = 0x76,
  kF8 = 0x77,
  kF9 = 0x78,
  kF10 = 0x79,
  kF11 = 0x7A,
  kF12 = 0x7B,
  kF13 = 0x7C,
  kF14 = 0x7D,
  kF15 = 0x7E,
  kF16 = 0x7F,
  kF17 = 0x80,
  kF18 = 0x81,
  kF19 = 0x82,
  kF20 = 0x83,
  kF21 = 0x84,
  kF22 = 0x85,
  kF23 = 0x86,
  kF24 = 0x87,

  // VK_NAVIGATION_* added in _WIN32_WINNT 0x0604, but marked as reserved in
  // WinUser.h and not documented on MSDN.
  kNavigationView = 0x88,
  kNavigationMenu = 0x89,
  kNavigationUp = 0x8A,
  kNavigationDown = 0x8B,
  kNavigationLeft = 0x8C,
  kNavigationRight = 0x8D,
  kNavigationAccept = 0x8E,
  kNavigationCancel = 0x8F,

  kNumLock = 0x90,
  kScroll = 0x91,

  // NEC PC-9800 keyboard.
  kOemNecEqual = 0x92,  // '=' key on the numpad.

  // Fujitsu/OASYS keyboard.
  kOemFjJisho = 0x92,    // 'Dictionary' key.
  kOemFjMasshou = 0x93,  // 'Unregister word' key.
  kOemFjTouroku = 0x94,  // 'Register word' key.
  kOemFjLOya = 0x95,     // 'Left OYAYUBI' key.
  kOemFjROya = 0x96,     // 'Right OYAYUBI' key.

  // Left and right Alt, Ctrl and Shift virtual keys.
  // On Windows (from WinUser.h):
  // "Used only as parameters to GetAsyncKeyState() and GetKeyState().
  //  No other API or message will distinguish left and right keys in this way."
  kLShift = 0xA0,
  kRShift = 0xA1,
  kLControl = 0xA2,
  kRControl = 0xA3,
  kLMenu = 0xA4,
  kRMenu = 0xA5,

  kBrowserBack = 0xA6,
  kBrowserForward = 0xA7,
  kBrowserRefresh = 0xA8,
  kBrowserStop = 0xA9,
  kBrowserSearch = 0xAA,
  kBrowserFavorites = 0xAB,
  kBrowserHome = 0xAC,

  kVolumeMute = 0xAD,
  kVolumeDown = 0xAE,
  kVolumeUp = 0xAF,
  kMediaNextTrack = 0xB0,
  kMediaPrevTrack = 0xB1,
  kMediaStop = 0xB2,
  kMediaPlayPause = 0xB3,
  kLaunchMail = 0xB4,
  kLaunchMediaSelect = 0xB5,
  kLaunchApp1 = 0xB6,
  kLaunchApp2 = 0xB7,

  kOem1 = 0xBA,       // ';:' for the US.
  kOemPlus = 0xBB,    // '+' for any country.
  kOemComma = 0xBC,   // ',' for any country.
  kOemMinus = 0xBD,   // '-' for any country.
  kOemPeriod = 0xBE,  // '.' for any country.
  kOem2 = 0xBF,       // '/?' for the US.
  kOem3 = 0xC0,       // '`~' for the US.

  // VK_GAMEPAD_* (since _WIN32_WINNT 0x0604) virtual key codes are marked as
  // reserved in WinUser.h and are mostly not documented on MSDN (with the
  // exception of the Xbox Device Portal Remote Input REST API in the "UWP on
  // Xbox One" section).
  // Xenia uses VK_PAD_* (kXInputPad*) for HID emulation internally instead
  // because XInput is the API used for the Xbox 360 controller.
  // To avoid confusion between VK_GAMEPAD_* and VK_PAD_*, here they are
  // prefixed with kXboxOne and kXInput respectively.
  // https://learn.microsoft.com/en-us/uwp/api/windows.system.virtualkey
  kXboxOneGamepadA = 0xC3,
  kXboxOneGamepadB = 0xC4,
  kXboxOneGamepadX = 0xC5,
  kXboxOneGamepadY = 0xC6,
  kXboxOneGamepadRightShoulder = 0xC7,
  kXboxOneGamepadLeftShoulder = 0xC8,
  kXboxOneGamepadLeftTrigger = 0xC9,
  kXboxOneGamepadRightTrigger = 0xCA,
  kXboxOneGamepadDpadUp = 0xCB,
  kXboxOneGamepadDpadDown = 0xCC,
  kXboxOneGamepadDpadLeft = 0xCD,
  kXboxOneGamepadDpadRight = 0xCE,
  kXboxOneGamepadMenu = 0xCF,
  kXboxOneGamepadView = 0xD0,
  kXboxOneGamepadLeftThumbstickButton = 0xD1,
  kXboxOneGamepadRightThumbstickButton = 0xD2,
  kXboxOneGamepadLeftThumbstickUp = 0xD3,
  kXboxOneGamepadLeftThumbstickDown = 0xD4,
  kXboxOneGamepadLeftThumbstickRight = 0xD5,
  kXboxOneGamepadLeftThumbstickLeft = 0xD6,
  kXboxOneGamepadRightThumbstickUp = 0xD7,
  kXboxOneGamepadRightThumbstickDown = 0xD8,
  kXboxOneGamepadRightThumbstickRight = 0xD9,
  kXboxOneGamepadRightThumbstickLeft = 0xDA,

  kOem4 = 0xDB,  // '[{' for the US.
  kOem5 = 0xDC,  // '\|' for the US.
  kOem6 = 0xDD,  // ']}' for the US.
  kOem7 = 0xDE,  // ''"' for the US.
  kOem8 = 0xDF,

  kOemAx = 0xE1,    // 'AX' key on the Japanese AX keyboard.
  kOem102 = 0xE2,   // "<>" or "\|" on the RT 102-key keyboard.
  kIcoHelp = 0xE3,  // Help key on the Olivetti keyboard (ICO).
  kIco00 = 0xE4,    // 00 key on the ICO.

  kProcessKey = 0xE5,

  kIcoClear = 0xE6,

  // From MSDN:
  // "Used to pass Unicode characters as if they were keystrokes. The VK_PACKET
  //  key is the low word of a 32-bit Virtual Key value used for non-keyboard
  //  input methods."
  kPacket = 0xE7,

  // Nokia/Ericsson.
  kOemReset = 0xE9,
  kOemJump = 0xEA,
  kOemPa1 = 0xEB,
  kOemPa2 = 0xEC,
  kOemPa3 = 0xED,
  kOemWsCtrl = 0xEE,
  kOemCuSel = 0xEF,
  kOemAttn = 0xF0,
  kOemFinish = 0xF1,
  kOemCopy = 0xF2,
  kOemAuto = 0xF3,
  kOemEnlW = 0xF4,
  kOemBackTab = 0xF5,

  kAttn = 0xF6,
  kCrSel = 0xF7,
  kExSel = 0xF8,
  kErEof = 0xF9,
  kPlay = 0xFA,
  kZoom = 0xFB,
  kNoName = 0xFC,
  kPa1 = 0xFD,
  kOemClear = 0xFE,

  // VK_PAD_* from XInput.h for XInputGetKeystroke. kXInput prefix added to
  // distinguish from VK_GAMEPAD_*, added much later for the Xbox One
  // controller.
  // https://learn.microsoft.com/en-us/windows/win32/api/xinput/ns-xinput-xinput_keystroke
  kXInputPadA = 0x5800,
  kXInputPadB = 0x5801,
  kXInputPadX = 0x5802,
  kXInputPadY = 0x5803,
  // RShoulder before LShoulder, not a typo.
  kXInputPadRShoulder = 0x5804,
  kXInputPadLShoulder = 0x5805,
  kXInputPadLTrigger = 0x5806,
  kXInputPadRTrigger = 0x5807,

  kXInputPadDpadUp = 0x5810,
  kXInputPadDpadDown = 0x5811,
  kXInputPadDpadLeft = 0x5812,
  kXInputPadDpadRight = 0x5813,
  kXInputPadStart = 0x5814,
  kXInputPadBack = 0x5815,
  kXInputPadLThumbPress = 0x5816,
  kXInputPadRThumbPress = 0x5817,

  kXInputPadLThumbUp = 0x5820,
  kXInputPadLThumbDown = 0x5821,
  kXInputPadLThumbRight = 0x5822,
  kXInputPadLThumbLeft = 0x5823,
  kXInputPadLThumbUpLeft = 0x5824,
  kXInputPadLThumbUpRight = 0x5825,
  kXInputPadLThumbDownRight = 0x5826,
  kXInputPadLThumbDownLeft = 0x5827,

  kXInputPadRThumbUp = 0x5830,
  kXInputPadRThumbDown = 0x5831,
  kXInputPadRThumbRight = 0x5832,
  kXInputPadRThumbLeft = 0x5833,
  kXInputPadRThumbUpLeft = 0x5834,
  kXInputPadRThumbUpRight = 0x5835,
  kXInputPadRThumbDownRight = 0x5836,
  kXInputPadRThumbDownLeft = 0x5837,
  // Undocumented therefore kNone however using 0x5838 for now.
  kXInputPadGuide = 0x5838,
};

}  // namespace ui
}  // namespace xe

#endif  // XENIA_UI_VIRTUAL_KEY_H_
