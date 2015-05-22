using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Xenia.Debug {
  public class Debugger {
    public DebugClient DebugClient {
      get;
    }

    public BreakpointList BreakpointList {
      get;
    }
    public FunctionList FunctionList {
      get;
    }
    public Memory Memory {
      get;
    }
    public ModuleList ModuleList {
      get;
    }
    public ThreadList ThreadList {
      get;
    }
  
    public Debugger() {
      this.DebugClient = new DebugClient();

      this.BreakpointList = new BreakpointList();
      this.FunctionList = new FunctionList();
      this.Memory = new Memory();
      this.ModuleList = new ModuleList();
      this.ThreadList = new ThreadList();
    }

    public bool Open() {
      return true;
    }

    public delegate void ChangedEventHandler(EventArgs e);

    public event ChangedEventHandler Changed;

    private void OnChanged(EventArgs e) {
      if (Changed != null) {
        Changed(e);
      }
    }
  }
}
