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
  public class Breakpoint : Changeable<Breakpoint> {
    // type code/data/kernel
    // address+[end address]
    // conditions? script?
    // action (suspend, trace, etc)

    public Breakpoint() {
      this.self = this;
    }
  }
}
