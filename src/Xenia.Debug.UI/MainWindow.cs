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
using Xenia.Debug.UI.Views;
using Xenia.Debug.Utilities;

namespace Xenia.Debug.UI {
  public partial class MainWindow : Form {
    private DeserializeDockContent deserializeDockContent;

    private BreakpointsPanel breakpointsPanel;
    private CallstackPanel callstackPanel;
    private readonly List<CodeDocument> codeDocuments = new List<CodeDocument>();
    private FilesystemPanel filesystemPanel;
    private FunctionsPanel functionsPanel;
    private HeapDocument heapDocument;
    private readonly List<MemoryDocument> memoryDocuments = new List<MemoryDocument>();
    private ModulesPanel modulesPanel;
    private ProfilePanel profilePanel;
    private readonly List<RegistersPanel> registersPanels = new List<RegistersPanel>();
    private StatisticsDocument statisticsDocument;
    private ThreadsPanel threadsPanel;
    private TracePanel tracePanel;

    public Debugger Debugger {
      get;
    }

    public MainWindow() {
      InitializeComponent();

      dockPanel = new DockPanel();
      dockPanel.Dock = System.Windows.Forms.DockStyle.Fill;
      dockPanel.DockBackColor = System.Drawing.SystemColors.AppWorkspace;
      dockPanel.DockBottomPortion = 200D;
      dockPanel.DockLeftPortion = 350D;
      dockPanel.Name = "dockPanel";
      Controls.Add(dockPanel);
      Controls.SetChildIndex(dockPanel, 0);

      Debugger = new Debugger((AsyncTask task) => {
        BeginInvoke(task);
      });

      breakpointsPanel = new BreakpointsPanel(Debugger);
      callstackPanel = new CallstackPanel(Debugger);
      codeDocuments.Add(new CodeDocument(Debugger));
      filesystemPanel = new FilesystemPanel(Debugger);
      functionsPanel = new FunctionsPanel(Debugger);
      heapDocument = new HeapDocument(Debugger);
      memoryDocuments.Add(new MemoryDocument(Debugger));
      modulesPanel = new ModulesPanel(Debugger);
      profilePanel = new ProfilePanel(Debugger);
      registersPanels.Add(new RegistersPanel(Debugger, RegisterClass.GuestGeneralPurpose));
      registersPanels.Add(new RegistersPanel(Debugger, RegisterClass.GuestFloatingPoint));
      registersPanels.Add(new RegistersPanel(Debugger, RegisterClass.GuestVector));
      statisticsDocument = new StatisticsDocument(Debugger);
      threadsPanel = new ThreadsPanel(Debugger);
      tracePanel = new TracePanel(Debugger);

      // deserializeDockContent =
      //    new DeserializeDockContent(GetContentFromPersistString);

      SetupDefaultLayout();

      // For hotkeys.
      KeyPreview = true;

      Debugger.StateChanged += Debugger_StateChanged;
      Debugger_StateChanged(this, Debugger.CurrentState);
      Debugger.CurrentContext.Changed += CurrentContext_Changed;
      CurrentContext_Changed();

      Debugger.Attach();
    }

    private void Debugger_StateChanged(object sender, Debugger.State e) {
      bool enabled = false;
      switch (e) {
      case Debugger.State.Idle:
        statusMessageLabel.Text = "Idle";
        break;
      case Debugger.State.Attaching:
        statusMessageLabel.Text = "Attaching";
        break;
      case Debugger.State.Attached:
        statusMessageLabel.Text = "Attached";
        enabled = true;
        break;
      case Debugger.State.Detached:
        statusMessageLabel.Text = "Detached";
        break;
      }

      controlToolStrip.Enabled = enabled;
    }

    private void CurrentContext_Changed() {
      bool enabled = false;
      switch (Debugger.CurrentContext.RunState) {
      case RunState.Updating:
        enabled = false;
        break;
      case RunState.Running:
        enabled = false;
        break;
      case RunState.Paused:
        enabled = true;
        break;
      }
      breakToolStripButton.Enabled = !enabled;
      continueToolStripButton.Enabled = enabled;
      stepInToolStripButton.Enabled = enabled;
      stepOverToolStripButton.Enabled = enabled;
      stepOutToolStripButton.Enabled = enabled;

      // TODO(benvanik): set thread info/etc.
    }

    private void SetupDefaultLayout() {
      dockPanel.SuspendLayout(true);

      filesystemPanel.Show(dockPanel, DockState.DockLeft);
      functionsPanel.Show(filesystemPanel.Pane, filesystemPanel);

      breakpointsPanel.Show(dockPanel, DockState.DockBottom);
      callstackPanel.Show(breakpointsPanel.Pane, breakpointsPanel);

      modulesPanel.Show(breakpointsPanel.Pane, DockAlignment.Right, 0.5);
      threadsPanel.Show(modulesPanel.Pane, modulesPanel);

      registersPanels[registersPanels.Count - 1].Show(filesystemPanel.Pane,
                                                      DockAlignment.Right, 0.5);
      for (int i = registersPanels.Count - 2; i >= 0; --i) {
        registersPanels[i].Show(registersPanels[i + 1].Pane, registersPanels[i + 1]);
      }

      foreach (var codeDocument in codeDocuments) {
        codeDocument.Show(dockPanel, DockState.Document);
      }

      heapDocument.Show(codeDocuments[0].Pane, DockAlignment.Right, 0.5);
      for (int i = 0; i < memoryDocuments.Count; ++i) {
        memoryDocuments[i].Show(heapDocument.Pane, heapDocument);
      }

      tracePanel.Show(heapDocument.Pane, DockAlignment.Bottom, 0.5);
      statisticsDocument.Show(tracePanel.Pane, tracePanel);

      dockPanel.ResumeLayout(true, true);
    }

    protected override bool ProcessCmdKey(ref Message msg, Keys keyData) {
      //if (keyData == (Keys.Control | Keys.F)) {
      //  MessageBox.Show("What the Ctrl+F?");
      //  return true;
      //}
      if (keyData == Keys.F11) {
        stepInToolStripButton_Click(this, EventArgs.Empty);
        return true;
      } else if (keyData == Keys.F10) {
        stepOverToolStripButton_Click(this, EventArgs.Empty);
        return true;
      } else if (keyData == (Keys.Shift | Keys.F11)) {
        stepOutToolStripButton_Click(this, EventArgs.Empty);
        return true;
      } else if (keyData == (Keys.Pause)) {
        breakToolStripButton_Click(this, EventArgs.Empty);
        return true;
      } else if (keyData == (Keys.Shift | Keys.F5)) {
        stopToolStripButton_Click(this, EventArgs.Empty);
        return true;
      }
      return base.ProcessCmdKey(ref msg, keyData);
    }

    private void continueToolStripButton_Click(object sender, EventArgs e) {
      Debugger.Continue();
    }

    private void breakToolStripButton_Click(object sender, EventArgs e) {
      Debugger.Break();
    }

    private void stopToolStripButton_Click(object sender, EventArgs e) {
      Debugger.Stop();
    }

    private void stepInToolStripButton_Click(object sender, EventArgs e) {
      Debugger.StepIn();
    }

    private void stepOverToolStripButton_Click(object sender, EventArgs e) {
      Debugger.StepOver();
    }

    private void stepOutToolStripButton_Click(object sender, EventArgs e) {
      Debugger.StepOut();
    }
  }
}
