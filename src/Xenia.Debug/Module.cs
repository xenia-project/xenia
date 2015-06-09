using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using xe.debug.proto;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class Module : KernelObject, IReadOnlyCollection<Function> {
    private bool hasFetched = false;
    private uint functionCount = 0;
    private readonly List<Function> functions = new List<Function>();

    public Module(Debugger debugger, uint moduleHandle) : base(debugger, moduleHandle) {
    }

    public async Task Invalidate(uint newFunctionCount) {
      if (hasFetched && newFunctionCount == functionCount) {
        // No-op.
        return;
      }

      var pendingTasks = new List<Task>();

      if (!hasFetched) {
        pendingTasks.Add(InvalidateModule());
        hasFetched = true;
      }

      if (newFunctionCount != functionCount) {
        uint functionIndexStart = functionCount;
        uint functionIndexEnd = newFunctionCount - 1;
        functionCount = newFunctionCount;
        pendingTasks.Add(InvalidateFunctions(functionIndexStart, functionIndexEnd));
      }

      await Task.WhenAll(pendingTasks);

      OnChanged();
    }

    private async Task InvalidateModule() {
      var fbb = Debugger.BeginRequest();
      int requestDataOffset = GetModuleRequest.CreateGetModuleRequest(fbb, Handle);
      var response = await Debugger.CommitRequest(
          fbb, RequestData.GetModuleRequest, requestDataOffset);

      var responseData = new GetModuleResponse();
      response.GetResponseData(responseData);

      ModuleType = responseData.Module.Type;
      Name = responseData.Module.Name;
      Path = responseData.Module.Path;
    }

    private async Task InvalidateFunctions(uint functionIndexStart, uint functionIndexEnd) {
      var fbb = Debugger.BeginRequest();
      int requestDataOffset = ListFunctionsRequest.CreateListFunctionsRequest(
          fbb, Handle, functionIndexStart, functionIndexEnd);
      var response = await Debugger.CommitRequest(
          fbb, RequestData.ListFunctionsRequest, requestDataOffset);

      var responseData = new ListFunctionsResponse();
      response.GetResponseData(responseData);

      var functionEntry = new xe.debug.proto.FunctionEntry();
      for (int i = 0; i < responseData.EntryLength; ++i) {
        responseData.GetEntry(functionEntry, i);
        var function = new Function(Debugger, this, functionEntry);
        functions.Add(function);
      }

      functions.Sort((Function a, Function b) => {
        return (int)((long)a.AddressStart - (long)b.AddressStart);
      });
    }

    public ModuleType ModuleType {
      get;
      private set;
    }

    public string Name {
      get;
      private set;
    }

    public string Path {
      get;
      private set;
    }

    public override string ToString() {
      return ToShortString();
    }

    public string ToShortString() {
      return string.Format("[{0:X4}] {1}", Handle, Name);
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
