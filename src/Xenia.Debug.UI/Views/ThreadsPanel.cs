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
  public partial class ThreadsPanel : BasePanel {
    private readonly Debugger debugger;

    public ThreadsPanel(Debugger debugger) {
      InitializeComponent();
      this.debugger = debugger;
    }
  }
}
