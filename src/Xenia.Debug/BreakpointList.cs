using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class BreakpointList : Changeable, IReadOnlyCollection<Breakpoint> {
    private readonly Debugger debugger;
    private readonly List<Breakpoint> breakpoints = new List<Breakpoint>();

    public BreakpointList(Debugger debugger) {
      this.debugger = debugger;
    }

    public int Count {
      get {
        return breakpoints.Count;
      }
    }

    public IEnumerator<Breakpoint> GetEnumerator() {
      return breakpoints.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator() {
      return breakpoints.GetEnumerator();
    }
  }
}
