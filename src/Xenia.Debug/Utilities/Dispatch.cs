using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace Xenia.Debug.Utilities {
  public delegate void AsyncTask();
  public delegate void AsyncTaskRunner(AsyncTask task);

  public static class Dispatch {
    public static AsyncTaskRunner AsyncTaskRunner {
      get; set;
    }

    public static void Issue(AsyncTask task) {
      AsyncTaskRunner(task);
    }
  }
}
