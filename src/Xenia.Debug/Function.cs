using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class Function : Changeable {
    // status: declared, defined, failed
    // module
    // name
    // startAddress
    // endAddress
    // behavior: default, prolog, epilog, epilog_return, extern
    // extern info?

    // source map

    // disasm:
    //   source
    //   raw hir
    //   hir
    //   machine code

    // trace data:
    //   intptr into file mapping
    //   function_thread_use bitmask
    //   call count
    //   caller history
    //   instruction execution counts
  }
}
