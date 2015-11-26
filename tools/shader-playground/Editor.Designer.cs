namespace shader_playground {
  partial class Editor {
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
      this.sourceCodeTextBox = new System.Windows.Forms.TextBox();
      this.outputTextBox = new System.Windows.Forms.TextBox();
      this.splitContainer1 = new System.Windows.Forms.SplitContainer();
      this.wordsTextBox = new System.Windows.Forms.TextBox();
      this.splitContainer1.Panel1.SuspendLayout();
      this.splitContainer1.Panel2.SuspendLayout();
      this.splitContainer1.SuspendLayout();
      this.SuspendLayout();
      //
      // sourceCodeTextBox
      //
      this.sourceCodeTextBox.AcceptsReturn = true;
      this.sourceCodeTextBox.AcceptsTab = true;
      this.sourceCodeTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
      this.sourceCodeTextBox.Location = new System.Drawing.Point(0, 0);
      this.sourceCodeTextBox.Multiline = true;
      this.sourceCodeTextBox.Name = "sourceCodeTextBox";
      this.sourceCodeTextBox.Size = new System.Drawing.Size(352, 360);
      this.sourceCodeTextBox.TabIndex = 0;
      //
      // outputTextBox
      //
      this.outputTextBox.AcceptsReturn = true;
      this.outputTextBox.AcceptsTab = true;
      this.outputTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
      this.outputTextBox.Location = new System.Drawing.Point(0, 0);
      this.outputTextBox.Multiline = true;
      this.outputTextBox.Name = "outputTextBox";
      this.outputTextBox.ReadOnly = true;
      this.outputTextBox.Size = new System.Drawing.Size(349, 360);
      this.outputTextBox.TabIndex = 1;
      //
      // splitContainer1
      //
      this.splitContainer1.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
      this.splitContainer1.Location = new System.Drawing.Point(12, 12);
      this.splitContainer1.Name = "splitContainer1";
      //
      // splitContainer1.Panel1
      //
      this.splitContainer1.Panel1.Controls.Add(this.sourceCodeTextBox);
      //
      // splitContainer1.Panel2
      //
      this.splitContainer1.Panel2.Controls.Add(this.outputTextBox);
      this.splitContainer1.Size = new System.Drawing.Size(705, 360);
      this.splitContainer1.SplitterDistance = 352;
      this.splitContainer1.TabIndex = 2;
      //
      // wordsTextBox
      //
      this.wordsTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom)
            | System.Windows.Forms.AnchorStyles.Left)
            | System.Windows.Forms.AnchorStyles.Right)));
      this.wordsTextBox.Location = new System.Drawing.Point(12, 378);
      this.wordsTextBox.Multiline = true;
      this.wordsTextBox.Name = "wordsTextBox";
      this.wordsTextBox.ReadOnly = true;
      this.wordsTextBox.Size = new System.Drawing.Size(705, 251);
      this.wordsTextBox.TabIndex = 4;
      //
      // Editor
      //
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(729, 641);
      this.Controls.Add(this.wordsTextBox);
      this.Controls.Add(this.splitContainer1);
      this.Name = "Editor";
      this.Text = "Shader Playground";
      this.splitContainer1.Panel1.ResumeLayout(false);
      this.splitContainer1.Panel1.PerformLayout();
      this.splitContainer1.Panel2.ResumeLayout(false);
      this.splitContainer1.Panel2.PerformLayout();
      this.splitContainer1.ResumeLayout(false);
      this.ResumeLayout(false);
      this.PerformLayout();

    }

    #endregion

    private System.Windows.Forms.TextBox sourceCodeTextBox;
    private System.Windows.Forms.TextBox outputTextBox;
    private System.Windows.Forms.SplitContainer splitContainer1;
    private System.Windows.Forms.TextBox wordsTextBox;
  }
}

