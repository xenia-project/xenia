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
  public partial class CodeDocument : BaseDocument {
    private readonly Debugger debugger;

    private Function function;

    public CodeDocument(Debugger debugger) {
      InitializeComponent();
      this.debugger = debugger;
    }

    public async void Show(Function function) {
      this.function = function;

      this.sourceTextBox.Text = "Loading...";
      await function.Invalidate();

      UpdateSourceTextBox();
    }

    private void UpdateSourceTextBox() {
      string combinedString = "";

      if (disasmPpcCheckBox.Checked) {
        combinedString += "Source PPC" + Environment.NewLine;
        if (function.DisasmPpc != null) {
          combinedString += function.DisasmPpc;
        } else {
          combinedString += "(unavailable)";
        }
        combinedString += Environment.NewLine + Environment.NewLine;
      }
      if (disasmUnoptimizedHirCheckBox.Checked) {
        combinedString += "Unoptimized HIR" + Environment.NewLine;
        if (function.DisasmHirUnoptimized != null) {
          combinedString += function.DisasmHirUnoptimized;
        } else {
          combinedString += "(unavailable)";
        }
        combinedString += Environment.NewLine + Environment.NewLine;
      }
      if (disasmOptimizedHirCheckBox.Checked) {
        combinedString += "Optimized HIR" + Environment.NewLine;
        if (function.DisasmHirOptimized != null) {
          combinedString += function.DisasmHirOptimized;
        } else {
          combinedString += "(unavailable)";
        }
        combinedString += Environment.NewLine + Environment.NewLine;
      }
      if (disasmMachineCodeCheckBox.Checked) {
        combinedString += "Machine Code" + Environment.NewLine;
        if (function.DisasmMachineCode != null) {
          combinedString += function.DisasmMachineCode;
        } else {
          combinedString += "(unavailable)";
        }
        combinedString += Environment.NewLine + Environment.NewLine;
      }

      this.sourceTextBox.Text = combinedString;
    }

    private void DisasmCheckBoxChanged(object sender, EventArgs e) {
      UpdateSourceTextBox();
    }
  }
}
