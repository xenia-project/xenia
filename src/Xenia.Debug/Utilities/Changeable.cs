using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Xenia.Debug.Utilities {
  public delegate void ChangedEventHandler();

  public class Changeable {
    private int changeDepth;
    public event ChangedEventHandler Changed;

    protected void BeginChanging() {
      ++changeDepth;
    }

    protected void EndChanging() {
      if (--changeDepth == 0) {
        OnChanged();
      }
    }

    protected void OnChanged() {
      System.Diagnostics.Debug.Assert(changeDepth == 0);
      if (Changed != null) {
        Changed();
      }
    }
  }
}
