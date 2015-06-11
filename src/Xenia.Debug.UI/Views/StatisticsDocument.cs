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
using System.ComponentModel;
using System.Data;
using System.Drawing;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using System.Windows.Forms;
using WeifenLuo.WinFormsUI.Docking;
using Xenia.Debug.UI.Controls;

namespace Xenia.Debug.UI.Views {
  public partial class StatisticsDocument : BaseDocument {
    private readonly Debugger debugger;

    public StatisticsDocument(Debugger debugger) {
      InitializeComponent();
      this.debugger = debugger;
    }
  }
}
