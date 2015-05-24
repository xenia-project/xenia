using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Xenia.Debug.Utilities {
  public class Disposable : IDisposable {
    private bool disposed = false;

    protected virtual void OnDispose() {
    }

    protected virtual void Dispose(bool disposing) {
      if (!disposed) {
        if (disposing) {
          // TODO: dispose managed state (managed objects).
        }
        OnDispose();
        disposed = true;
      }
    }

    ~Disposable() {
      Dispose(false);
    }

    public void Dispose() {
      Dispose(true);
      GC.SuppressFinalize(this);
    }
  }
}
