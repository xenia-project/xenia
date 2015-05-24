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
    private readonly List<Function> functions = new List<Function>();

    // xobject: handle
    // path
    // type: user, kernel
    // if user:
    //   xex header?

    public Module(Debugger debugger, uint moduleHandle) : base(debugger, moduleHandle) {
    }

    public async Task Invalidate() {
      var fbb = Debugger.BeginRequest();
      int requestDataOffset = GetModuleRequest.CreateGetModuleRequest(fbb, Handle);
      var response = await Debugger.CommitRequest(
          fbb, RequestData.GetModuleRequest, requestDataOffset);
      GetModuleResponse responseData = new GetModuleResponse();
      response.GetResponseData(responseData);

      //
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
