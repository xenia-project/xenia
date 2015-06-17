/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

using System;
using System.Collections;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using xe.debug.proto;
using Xenia.Debug.Utilities;

namespace Xenia.Debug {
  public class ThreadList : Changeable<ThreadList>, IReadOnlyCollection<Thread> {
    private readonly Debugger debugger;
    private readonly List<Thread> threads = new List<Thread>();

    public ThreadList(Debugger debugger) {
      this.self = this;
      this.debugger = debugger;
    }

    public async Task Invalidate() {
      var fbb = debugger.BeginRequest();
      ListThreadsRequest.StartListThreadsRequest(fbb);
      int requestDataOffset = ListThreadsRequest.EndListThreadsRequest(fbb);
      var response = await debugger.CommitRequest(
          fbb, RequestData.ListThreadsRequest, requestDataOffset);
      ListThreadsResponse responseData = new ListThreadsResponse();
      response.GetResponseData(responseData);

      for (int i = 0; i < responseData.ThreadLength; ++i) {
        var threadData = responseData.GetThread(i);
        // threadData.Name;
      }

      OnChanged();
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
