using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class KernelObject : Changeable {
    public readonly Debugger Debugger;
    public readonly uint Handle;

    public KernelObject(Debugger debugger, uint handle) {
      this.Debugger = debugger;
      this.Handle = handle;
    }
  }
}
