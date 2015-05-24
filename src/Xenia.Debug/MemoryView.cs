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

    // startAddress
    // endAddress

    // padding: 2 64k pages on each side?
    // history (last N snapshots)
    // snapshot on break, or manually

    // colored: text on modification
    //          bg on heap alloc #
    // focus details: protection, region, allocation
    // stacks for all allocations
  }
}
