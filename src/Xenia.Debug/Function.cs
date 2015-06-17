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
    private static Native.Disassembler disassembler = new Native.Disassembler();

    // status: declared, defined, failed
    // behavior: default, prolog, epilog, epilog_return, extern
    // extern info?

    public readonly Debugger Debugger;
    public readonly Module Module;
    public readonly ulong Identifier;
    public readonly uint AddressStart;
    public uint AddressEnd {
      get; private set;
    }

    public uint MachineCodeStart {
      get; private set;
    }
    public uint MachineCodeEnd {
      get; private set;
    }

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

    private string disasmPpc;
    public unsafe string DisasmPpc {
      get {
        if (disasmPpc != null) {
          return disasmPpc;
        }
        if (AddressEnd == 0) {
          return "(unavailable)";
        }
        disasmPpc = disassembler.DisassemblePPC(
            new IntPtr(Debugger.TranslateVirtual(AddressStart)),
            AddressEnd - AddressStart + 4);
        return disasmPpc;
      }
    }

    public string DisasmHirUnoptimized {
      get; private set;
    }
    public string DisasmHirOptimized {
      get; private set;
    }

    private string disasmMachineCode;
    public string DisasmMachineCode {
      get {
        if (disasmMachineCode != null) {
          return disasmMachineCode;
        }
        if (MachineCodeStart == 0) {
          return null;
        }
        disasmMachineCode = disassembler.DisassembleX64(
            new IntPtr(MachineCodeStart), MachineCodeEnd - MachineCodeStart + 1);
        disasmMachineCode = disasmMachineCode.Replace("\n", "\r\n");
        return disasmMachineCode;
      }
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

      this.AddressEnd = functionData.AddressEnd;

      this.MachineCodeStart = functionData.MachineCodeStart;
      this.MachineCodeEnd = functionData.MachineCodeEnd;

      this.DisasmHirUnoptimized = functionData.DisasmHirRaw;
      this.DisasmHirOptimized = functionData.DisasmHirOpt;
      if (DisasmHirUnoptimized != null) {
        DisasmHirUnoptimized = DisasmHirUnoptimized.Replace("\n", "\r\n");
      }
      if (DisasmHirOptimized != null) {
        DisasmHirOptimized = DisasmHirOptimized.Replace("\n", "\r\n");
      }

      OnChanged();
    }
  }
}
