namespace Xenia.Debug.UI {
  partial class MainWindow {
    /// <summary>
    /// Required designer variable.
    /// </summary>
    private System.ComponentModel.IContainer components = null;

    /// <summary>
    /// Clean up any resources being used.
    /// </summary>
    /// <param name="disposing">true if managed resources should be disposed; otherwise, false.</param>
    protected override void Dispose(bool disposing) {
      if (disposing && (components != null)) {
        components.Dispose();
      }
      base.Dispose(disposing);
    }

    #region Windows Form Designer generated code

    /// <summary>
    /// Required method for Designer support - do not modify
    /// the contents of this method with the code editor.
    /// </summary>
    private void InitializeComponent() {
      System.ComponentModel.ComponentResourceManager resources = new System.ComponentModel.ComponentResourceManager(typeof(MainWindow));
      this.mainMenuStrip = new System.Windows.Forms.MenuStrip();
      this.fileToolStripMenuItem = new System.Windows.Forms.ToolStripMenuItem();
      this.mainToolStrip = new System.Windows.Forms.ToolStrip();
      this.toolStripButton1 = new System.Windows.Forms.ToolStripButton();
      this.statusStrip = new System.Windows.Forms.StatusStrip();
      this.statusMessageLabel = new System.Windows.Forms.ToolStripStatusLabel();
      this.controlToolStrip = new System.Windows.Forms.ToolStrip();
      this.threadToolStripLabel = new System.Windows.Forms.ToolStripLabel();
      this.threadToolStripComboBox = new System.Windows.Forms.ToolStripComboBox();
      this.toolStripSeparator1 = new System.Windows.Forms.ToolStripSeparator();
      this.continueToolStripButton = new System.Windows.Forms.ToolStripButton();
      this.breakToolStripButton = new System.Windows.Forms.ToolStripButton();
      this.stopToolStripButton = new System.Windows.Forms.ToolStripButton();
      this.toolStripSeparator2 = new System.Windows.Forms.ToolStripSeparator();
      this.stepInToolStripButton = new System.Windows.Forms.ToolStripButton();
      this.stepOverToolStripButton = new System.Windows.Forms.ToolStripButton();
      this.stepOutToolStripButton = new System.Windows.Forms.ToolStripButton();
      this.mainMenuStrip.SuspendLayout();
      this.mainToolStrip.SuspendLayout();
      this.statusStrip.SuspendLayout();
      this.controlToolStrip.SuspendLayout();
      this.SuspendLayout();
      // 
      // mainMenuStrip
      // 
      this.mainMenuStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.fileToolStripMenuItem});
      this.mainMenuStrip.Location = new System.Drawing.Point(0, 0);
      this.mainMenuStrip.Name = "mainMenuStrip";
      this.mainMenuStrip.Size = new System.Drawing.Size(1571, 24);
      this.mainMenuStrip.TabIndex = 0;
      // 
      // fileToolStripMenuItem
      // 
      this.fileToolStripMenuItem.Name = "fileToolStripMenuItem";
      this.fileToolStripMenuItem.Size = new System.Drawing.Size(37, 20);
      this.fileToolStripMenuItem.Text = "&File";
      // 
      // mainToolStrip
      // 
      this.mainToolStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.toolStripButton1});
      this.mainToolStrip.Location = new System.Drawing.Point(0, 24);
      this.mainToolStrip.Name = "mainToolStrip";
      this.mainToolStrip.Size = new System.Drawing.Size(1571, 25);
      this.mainToolStrip.TabIndex = 3;
      this.mainToolStrip.Text = "toolStrip1";
      // 
      // toolStripButton1
      // 
      this.toolStripButton1.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
      this.toolStripButton1.Image = ((System.Drawing.Image)(resources.GetObject("toolStripButton1.Image")));
      this.toolStripButton1.ImageTransparentColor = System.Drawing.Color.Magenta;
      this.toolStripButton1.Name = "toolStripButton1";
      this.toolStripButton1.Size = new System.Drawing.Size(23, 22);
      this.toolStripButton1.Text = "toolStripButton1";
      // 
      // statusStrip
      // 
      this.statusStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.statusMessageLabel});
      this.statusStrip.Location = new System.Drawing.Point(0, 1081);
      this.statusStrip.Name = "statusStrip";
      this.statusStrip.Size = new System.Drawing.Size(1571, 22);
      this.statusStrip.TabIndex = 4;
      this.statusStrip.Text = "statusStrip1";
      // 
      // statusMessageLabel
      // 
      this.statusMessageLabel.Name = "statusMessageLabel";
      this.statusMessageLabel.Size = new System.Drawing.Size(0, 17);
      // 
      // controlToolStrip
      // 
      this.controlToolStrip.Items.AddRange(new System.Windows.Forms.ToolStripItem[] {
            this.threadToolStripLabel,
            this.threadToolStripComboBox,
            this.toolStripSeparator1,
            this.continueToolStripButton,
            this.breakToolStripButton,
            this.stopToolStripButton,
            this.toolStripSeparator2,
            this.stepInToolStripButton,
            this.stepOverToolStripButton,
            this.stepOutToolStripButton});
      this.controlToolStrip.Location = new System.Drawing.Point(0, 49);
      this.controlToolStrip.Name = "controlToolStrip";
      this.controlToolStrip.Size = new System.Drawing.Size(1571, 25);
      this.controlToolStrip.TabIndex = 6;
      this.controlToolStrip.Text = "toolStrip1";
      // 
      // threadToolStripLabel
      // 
      this.threadToolStripLabel.Name = "threadToolStripLabel";
      this.threadToolStripLabel.Size = new System.Drawing.Size(47, 22);
      this.threadToolStripLabel.Text = "Thread:";
      // 
      // threadToolStripComboBox
      // 
      this.threadToolStripComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
      this.threadToolStripComboBox.Name = "threadToolStripComboBox";
      this.threadToolStripComboBox.Size = new System.Drawing.Size(250, 25);
      // 
      // toolStripSeparator1
      // 
      this.toolStripSeparator1.Name = "toolStripSeparator1";
      this.toolStripSeparator1.Size = new System.Drawing.Size(6, 25);
      // 
      // continueToolStripButton
      // 
      this.continueToolStripButton.Image = global::Xenia.Debug.UI.Properties.Resources.play;
      this.continueToolStripButton.ImageTransparentColor = System.Drawing.Color.Transparent;
      this.continueToolStripButton.Name = "continueToolStripButton";
      this.continueToolStripButton.Size = new System.Drawing.Size(76, 22);
      this.continueToolStripButton.Text = "Continue";
      this.continueToolStripButton.Click += new System.EventHandler(this.continueToolStripButton_Click);
      // 
      // breakToolStripButton
      // 
      this.breakToolStripButton.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
      this.breakToolStripButton.Image = global::Xenia.Debug.UI.Properties.Resources.pause;
      this.breakToolStripButton.ImageTransparentColor = System.Drawing.Color.Magenta;
      this.breakToolStripButton.Name = "breakToolStripButton";
      this.breakToolStripButton.Size = new System.Drawing.Size(23, 22);
      this.breakToolStripButton.Text = "Break";
      this.breakToolStripButton.Click += new System.EventHandler(this.breakToolStripButton_Click);
      // 
      // stopToolStripButton
      // 
      this.stopToolStripButton.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
      this.stopToolStripButton.Image = global::Xenia.Debug.UI.Properties.Resources.stop;
      this.stopToolStripButton.ImageTransparentColor = System.Drawing.Color.Magenta;
      this.stopToolStripButton.Name = "stopToolStripButton";
      this.stopToolStripButton.Size = new System.Drawing.Size(23, 22);
      this.stopToolStripButton.Text = "Stop";
      this.stopToolStripButton.Click += new System.EventHandler(this.stopToolStripButton_Click);
      // 
      // toolStripSeparator2
      // 
      this.toolStripSeparator2.Name = "toolStripSeparator2";
      this.toolStripSeparator2.Size = new System.Drawing.Size(6, 25);
      // 
      // stepInToolStripButton
      // 
      this.stepInToolStripButton.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
      this.stepInToolStripButton.Image = global::Xenia.Debug.UI.Properties.Resources.step_in;
      this.stepInToolStripButton.ImageTransparentColor = System.Drawing.Color.Transparent;
      this.stepInToolStripButton.Name = "stepInToolStripButton";
      this.stepInToolStripButton.Size = new System.Drawing.Size(23, 22);
      this.stepInToolStripButton.Text = "Step In";
      this.stepInToolStripButton.Click += new System.EventHandler(this.stepInToolStripButton_Click);
      // 
      // stepOverToolStripButton
      // 
      this.stepOverToolStripButton.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
      this.stepOverToolStripButton.Image = global::Xenia.Debug.UI.Properties.Resources.step_over;
      this.stepOverToolStripButton.ImageTransparentColor = System.Drawing.Color.Transparent;
      this.stepOverToolStripButton.Name = "stepOverToolStripButton";
      this.stepOverToolStripButton.Size = new System.Drawing.Size(23, 22);
      this.stepOverToolStripButton.Text = "Step Over";
      this.stepOverToolStripButton.Click += new System.EventHandler(this.stepOverToolStripButton_Click);
      // 
      // stepOutToolStripButton
      // 
      this.stepOutToolStripButton.DisplayStyle = System.Windows.Forms.ToolStripItemDisplayStyle.Image;
      this.stepOutToolStripButton.Image = global::Xenia.Debug.UI.Properties.Resources.step_out;
      this.stepOutToolStripButton.ImageTransparentColor = System.Drawing.Color.Transparent;
      this.stepOutToolStripButton.Name = "stepOutToolStripButton";
      this.stepOutToolStripButton.Size = new System.Drawing.Size(23, 22);
      this.stepOutToolStripButton.Text = "Step Out";
      this.stepOutToolStripButton.Click += new System.EventHandler(this.stepOutToolStripButton_Click);
      // 
      // MainWindow
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(1571, 1103);
      this.Controls.Add(this.controlToolStrip);
      this.Controls.Add(this.statusStrip);
      this.Controls.Add(this.mainToolStrip);
      this.Controls.Add(this.mainMenuStrip);
      this.IsMdiContainer = true;
      this.MainMenuStrip = this.mainMenuStrip;
      this.Name = "MainWindow";
      this.Text = "Xenia Debugger";
      this.mainMenuStrip.ResumeLayout(false);
      this.mainMenuStrip.PerformLayout();
      this.mainToolStrip.ResumeLayout(false);
      this.mainToolStrip.PerformLayout();
      this.statusStrip.ResumeLayout(false);
      this.statusStrip.PerformLayout();
      this.controlToolStrip.ResumeLayout(false);
      this.controlToolStrip.PerformLayout();
      this.ResumeLayout(false);
      this.PerformLayout();

    }

    #endregion

    private System.Windows.Forms.MenuStrip mainMenuStrip;
    private System.Windows.Forms.ToolStripMenuItem fileToolStripMenuItem;
    private System.Windows.Forms.ToolStrip mainToolStrip;
    private System.Windows.Forms.ToolStripButton toolStripButton1;
    private System.Windows.Forms.StatusStrip statusStrip;
    private System.Windows.Forms.ToolStripStatusLabel statusMessageLabel;
    private WeifenLuo.WinFormsUI.Docking.DockPanel dockPanel;
    private System.Windows.Forms.ToolStrip controlToolStrip;
    private System.Windows.Forms.ToolStripLabel threadToolStripLabel;
    private System.Windows.Forms.ToolStripComboBox threadToolStripComboBox;
    private System.Windows.Forms.ToolStripSeparator toolStripSeparator1;
    private System.Windows.Forms.ToolStripButton continueToolStripButton;
    private System.Windows.Forms.ToolStripButton breakToolStripButton;
    private System.Windows.Forms.ToolStripButton stopToolStripButton;
    private System.Windows.Forms.ToolStripSeparator toolStripSeparator2;
    private System.Windows.Forms.ToolStripButton stepInToolStripButton;
    private System.Windows.Forms.ToolStripButton stepOverToolStripButton;
    private System.Windows.Forms.ToolStripButton stepOutToolStripButton;
  }
}