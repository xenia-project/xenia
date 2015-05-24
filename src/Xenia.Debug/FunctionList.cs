using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class FunctionList : Changeable, IReadOnlyCollection<Function> {
    private readonly Debugger debugger;
    private readonly List<Function> functions = new List<Function>();

    public FunctionList(Debugger debugger) {
      this.debugger = debugger;
    }

    public int Count {
      get {
        return functions.Count;
      }
    }

    public IEnumerator<Function> GetEnumerator() {
      return functions.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator() {
      return functions.GetEnumerator();
    }
  }
}
