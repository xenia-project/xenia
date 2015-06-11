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

namespace Xenia.Debug.Utilities {
  public delegate void ChangedEventHandler<T>(T sender);

  public class Changeable<T> {
    protected T self;
    private int changeDepth;

    public event ChangedEventHandler<T> Changed;

    protected void BeginChanging() {
      ++changeDepth;
    }

    protected void EndChanging() {
      if (--changeDepth == 0) {
        OnChanged();
      }
    }

    protected void OnChanged() {
      System.Diagnostics.Debug.Assert(changeDepth == 0);
      if (Changed != null) {
        Changed(self);
      }
    }
  }
}
