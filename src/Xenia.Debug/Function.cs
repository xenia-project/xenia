/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using xe.debug.proto;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class Function : Changeable<Function> {
    // status: declared, defined, failed
    // behavior: default, prolog, epilog, epilog_return, extern
    // extern info?

    public readonly Debugger Debugger;
    public readonly Module Module;
    public readonly ulong Identifier;
    public readonly uint AddressStart;
    public readonly uint AddressEnd;

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

    public string DisasmPpc {
      get; private set;
    }
    public string DisasmHirUnoptimized {
      get; private set;
    }
    public string DisasmHirOptimized {
      get; private set;
    }
    public string DisasmMachineCode {
      get; private set;
    }

    public Function(Debugger debugger, Module module, xe.debug.proto.FunctionEntry functionEntry) {
      this.self = this;
      this.Debugger = debugger;
      this.Module = module;
      Identifier = functionEntry.Identifier;
      AddressStart = functionEntry.AddressStart;
      AddressEnd = functionEntry.AddressEnd;
      Name = functionEntry.Name;
    }

    private string name;
    public string Name {
      get {
        return name;
      }
      set {
        name = value;
        if (value == null) {
          name = "sub_" + AddressStart.ToString("X8");
        }
        LowerName = name.ToLowerInvariant();
      }
    }
    public string LowerName {
      get; set;
    }

    public override string ToString() {
      return Name;
    }

    public async Task Invalidate() {
      var fbb = Debugger.BeginRequest();
      int requestDataOffset = GetFunctionRequest.CreateGetFunctionRequest(fbb, Identifier);
      var response = await Debugger.CommitRequest(
          fbb, RequestData.GetFunctionRequest, requestDataOffset);

      var responseData = new GetFunctionResponse();
      response.GetResponseData(responseData);
      var functionData = responseData.Function;

      this.DisasmPpc = functionData.DisasmPpc;
      this.DisasmHirUnoptimized = functionData.DisasmHirRaw;
      this.DisasmHirOptimized = functionData.DisasmHirOpt;
      this.DisasmMachineCode = functionData.DisasmMachineCode;
      if (DisasmPpc != null) {
        DisasmPpc = DisasmPpc.Replace("\n", "\r\n");
      }
      if (DisasmHirUnoptimized != null) {
        DisasmHirUnoptimized = DisasmHirUnoptimized.Replace("\n", "\r\n");
      }
      if (DisasmHirOptimized != null) {
        DisasmHirOptimized = DisasmHirOptimized.Replace("\n", "\r\n");
      }
      if (DisasmMachineCode != null) {
        DisasmMachineCode = DisasmMachineCode.Replace("\n", "\r\n");
      }

      DisassembleX64();

      OnChanged();
    }

    private static Native.X64Disassembler disassembler = new Native.X64Disassembler();

    private void DisassembleX64() {
      var str = disassembler.GenerateString(IntPtr.Zero, 0);
      System.Diagnostics.Debug.WriteLine(str);
      disassembler.Dispose();
    }
  }
}
