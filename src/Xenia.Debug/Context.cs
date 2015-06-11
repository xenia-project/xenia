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
  public enum RunState {
    Updating,
    Running,
    Paused,
  }

  public class Context : Changeable<Context> {
    private readonly Debugger debugger;

    public Context(Debugger debugger) {
      this.self = this;
      this.debugger = debugger;
    }

    public RunState RunState {
      get; private set;
    }

    public void SetRunState(RunState runState) {
      if (RunState == runState) {
        return;
      }
      RunState = runState;
      OnChanged();
    }

    public uint CurrentThreadId {
      get; private set;
    }

    public void SetThreadId(uint threadId) {
      if (CurrentThreadId == threadId) {
        return;
      }
      CurrentThreadId = threadId;
      OnChanged();
    }
  }
}
