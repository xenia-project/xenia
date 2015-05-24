using System;
using System.Collections.Generic;
using System.Linq;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using System.Threading.Tasks;

namespace Xenia.Debug.Utilities {
  public static class TaskExtensions {
    public static async Task
     WithCancellation(this Task task, CancellationToken cancellationToken) {
      var tcs = new TaskCompletionSource<bool>();
      using (cancellationToken.Register(
          s => ((TaskCompletionSource<bool>)s).TrySetResult(true), tcs)) {
        if (task != await Task.WhenAny(task, tcs.Task))
          throw new OperationCanceledException(cancellationToken);
      }
      await task;
    }

    public static async Task<T> WithCancellation<T>(
         this Task<T> task, CancellationToken cancellationToken) {
      var tcs = new TaskCompletionSource<bool>();
      using (cancellationToken.Register(
          s => ((TaskCompletionSource<bool>)s).TrySetResult(true), tcs)) {
        if (task != await Task.WhenAny(task, tcs.Task))
          throw new OperationCanceledException(cancellationToken);
      }
      return await task;
    }

    public static Task<int> SendTaskAsync(this Socket socket, byte[] buffer,
                                    int offset, int size, SocketFlags flags) {
      IAsyncResult result =
          socket.BeginSend(buffer, offset, size, flags, null, null);
      return Task.Factory.FromAsync(result, socket.EndSend);
    }
  }
}
