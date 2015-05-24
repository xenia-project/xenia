using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using xe.debug.proto;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class ModuleList : Changeable, IReadOnlyCollection<Module> {
    private readonly Debugger debugger;
    private readonly List<Module> modules = new List<Module>();

    public ModuleList(Debugger debugger) {
      this.debugger = debugger;
    }

    public async Task Invalidate() {
      var fbb = debugger.BeginRequest();
      ListModulesRequest.StartListModulesRequest(fbb);
      int requestDataOffset = ListModulesRequest.EndListModulesRequest(fbb);
      var response = await debugger.CommitRequest(
          fbb, RequestData.ListModulesRequest, requestDataOffset);
      ListModulesResponse responseData = new ListModulesResponse();
      response.GetResponseData(responseData);

      var pendingTasks = new List<Task>();
      for (int i = 0; i < responseData.ModuleIdsLength; ++i) {
        uint moduleHandle = responseData.GetModuleIds(i);
        var module = modules.Find((m) => m.Handle == moduleHandle);
        if (module == null) {
          // Module not found.
          module = new Module(debugger, moduleHandle);
          pendingTasks.Add(module.Invalidate());
        } else {
          // Module already present.
          // Modules are immutable, so ignore?
        }
      }

      await Task.WhenAll(pendingTasks);
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
