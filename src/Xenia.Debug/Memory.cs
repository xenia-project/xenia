using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class Memory : Changeable {
    private readonly Debugger debugger;

    public Memory(Debugger debugger) {
      this.debugger = debugger;
    }

    public MemoryView CreateView() {
      return new MemoryView(this);
    }
  }
}
