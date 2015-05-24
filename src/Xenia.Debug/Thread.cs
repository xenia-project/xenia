using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class ThreadContext {
    // Maps to ppc_context.h:
    // r[32]
    // f[32]
    // v[128]
    // lr
    // ctr
    // xer
    // crN?
    // fpscr
  }

  public class Thread : KernelObject {
    // xobject: handle
    // module?
    // pcr address
    // thread state address
    // tls address
    // stack address, size
    // thread id
    // name
    // IsAlive
    // priority
    // affinity
    // state: running, suspended, waiting
    // creation params:
    //   stack size
    //   xapi thread startup fn
    //   start address fn
    //   start context
    //   creation flags
    // callstack at creation

    public Thread(Debugger debugger, uint threadHandle) : base(debugger, threadHandle) {
    }
  }
}
