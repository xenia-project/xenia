using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class Memory : Changeable {
    public MemoryView CreateView() {
      return new MemoryView(this);
    }
  }
}
