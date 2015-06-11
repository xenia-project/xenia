/**
 ******************************************************************************
 * Xenia : Xbox 360 Emulator Research Project                                 *
 ******************************************************************************
 * Copyright 2015 Ben Vanik. All rights reserved.                             *
 * Released under the BSD license - see LICENSE in the root for more details. *
 ******************************************************************************
 */

using System;
using System.Windows.Forms;

namespace Xenia.Debug.UI {
  static class Program {
    /// <summary>
    /// The main entry point for the application.
    /// </summary>
    [STAThread] static void Main() {
      Application.EnableVisualStyles();
      Application.SetCompatibleTextRenderingDefault(false);
      Application.Run(new MainWindow());
    }
  }
}
