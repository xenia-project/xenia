/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class KernelObject : Changeable<KernelObject> {
    public readonly Debugger Debugger;
    public readonly uint Handle;

    public KernelObject(Debugger debugger, uint handle) {
      this.self = this;
      this.Debugger = debugger;
      this.Handle = handle;
    }
  }
}
