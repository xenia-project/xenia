using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class ThreadList : Changeable, IReadOnlyCollection<Thread> {
    private readonly Debugger debugger;
    private readonly List<Thread> threads = new List<Thread>();

    public ThreadList(Debugger debugger) {
      this.debugger = debugger;
    }

    public int Count {
      get {
        return threads.Count;
      }
    }

    public IEnumerator<Thread> GetEnumerator() {
      return threads.GetEnumerator();
    }

    IEnumerator IEnumerable.GetEnumerator() {
      return threads.GetEnumerator();
    }
  }
}
