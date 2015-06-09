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
  public partial class ModulesPanel : BasePanel {
    private readonly Debugger debugger;

    public ModulesPanel(Debugger debugger) {
      InitializeComponent();
      this.debugger = debugger;

      debugger.ModuleList.Changed += UpdateModulesList;
      UpdateModulesList(debugger.ModuleList);
    }

    private void UpdateModulesList(ModuleList sender) {
      modulesListView.BeginUpdate();
      modulesListView.Items.Clear();
      foreach (Module module in debugger.ModuleList) {
        var item = new ListViewItem(new string[]{
            module.Handle.ToString("X4"),
            module.ModuleType == xe.debug.proto.ModuleType.Kernel ? "Kernel"
                                                                  : "User",
            module.Name,
            module.Path,
        });
        modulesListView.Items.Add(item);
      }
      modulesListView.EndUpdate();
    }
  }
}
