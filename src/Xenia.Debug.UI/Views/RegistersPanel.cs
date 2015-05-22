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
  public enum RegisterClass {
    GuestGeneralPurpose,
    GuestFloatingPoint,
    GuestVector,
    HostGeneralPurpose,
    HostAvx,
  }

  public partial class RegistersPanel : BasePanel {
    private readonly Debugger debugger;
    private readonly RegisterClass registerClass;

    public RegistersPanel(Debugger debugger, RegisterClass registerClass) {
      InitializeComponent();

      this.debugger = debugger;
      this.registerClass = registerClass;
      switch (registerClass) {
      case RegisterClass.GuestGeneralPurpose:
        this.Text = "GPR";
        break;
      case RegisterClass.GuestFloatingPoint:
        this.Text = "FPR";
        break;
      case RegisterClass.GuestVector:
        this.Text = "VR";
        break;
      case RegisterClass.HostGeneralPurpose:
        this.Text = "x64";
        break;
      case RegisterClass.HostAvx:
        this.Text = "AVX";
        break;
      default:
        System.Diagnostics.Debug.Fail("Unhandled case: " + registerClass);
        break;
      }
    }
  }
}
