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

namespace Xenia.Debug.UI {
  public partial class MainWindow : Form {
    private DeserializeDockContent deserializeDockContent;

    private BreakpointsPanel breakpointsPanel;
    private CallstackPanel callstackPanel;
    private List<CodeDocument> codeDocuments = new List<CodeDocument>();
    private FilesystemPanel filesystemPanel;
    private FunctionsPanel functionsPanel;
    private HeapDocument heapDocument;
    private List<MemoryDocument> memoryDocuments = new List<MemoryDocument>();
    private ModulesPanel modulesPanel;
    private ProfilePanel profilePanel;
    private List<RegistersPanel> registersPanels = new List<RegistersPanel>();
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

      this.Debugger = new Debugger();

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
  }
}
