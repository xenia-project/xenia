using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class ModuleList : Changeable, IReadOnlyCollection<Module> {
    private readonly Debugger debugger;
    private readonly List<Module> modules = new List<Module>();

    public ModuleList(Debugger debugger) {
      this.debugger = debugger;
    }

    public int Count {
      get {
        return modules.Count;
      }
    }

    public IEnumerator<Module> GetEnumerator() {
      return modules.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator() {
      return modules.GetEnumerator();
    }
  }
}
