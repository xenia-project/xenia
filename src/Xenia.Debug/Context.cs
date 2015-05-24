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

  public class Context : Changeable {
    private readonly Debugger debugger;

    public Context(Debugger debugger) {
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
