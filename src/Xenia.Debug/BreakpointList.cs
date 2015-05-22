using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class BreakpointList : Changeable {
    private readonly List<Breakpoint> breakpoints = new List<Breakpoint>();

    public void Add(Breakpoint breakpoint) {
    }

    public void Remove(Breakpoint breakpoint) {
    }

    public void Clear() {
    }
  }
}
