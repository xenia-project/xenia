/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2013 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

// This is a partial file designed to be included by other files when
// constructing various tables.


XE_EXPORT(xbdm,     0x00000004, DmCloseLoadedModules,                                       kFunction),
XE_EXPORT(xbdm,     0x00000024, DmSendNotificationString,                                   kFunction),
XE_EXPORT(xbdm,     0x00000033, DmWalkLoadedModules,                                        kFunction),
XE_EXPORT(xbdm,     0x00000052, DmCaptureStackBackTrace,                                    kFunction),
XE_EXPORT(xbdm,     0x0000008D, DmMapDevkitDrive,                                           kFunction),
XE_EXPORT(xbdm,     0x00000091, DmFindPdbSignature,                                         kFunction),
