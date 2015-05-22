using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class MemoryView : Changeable {
    private readonly Memory memory;

    public MemoryView(Memory memory) {
      this.memory = memory;
    }
  }
}
