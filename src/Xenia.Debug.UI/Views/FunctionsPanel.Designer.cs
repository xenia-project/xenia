namespace Xenia.Debug.UI.Views {
  partial class FunctionsPanel {
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
      this.modulesComboBox = new System.Windows.Forms.ComboBox();
      this.filterTextBox = new System.Windows.Forms.TextBox();
      this.functionsListBox = new System.Windows.Forms.ListBox();
      this.SuspendLayout();
      // 
      // moduleComboBox
      // 
      this.modulesComboBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.modulesComboBox.FormattingEnabled = true;
      this.modulesComboBox.Location = new System.Drawing.Point(12, 12);
      this.modulesComboBox.Name = "moduleComboBox";
      this.modulesComboBox.Size = new System.Drawing.Size(234, 21);
      this.modulesComboBox.TabIndex = 0;
      // 
      // filterTextBox
      // 
      this.filterTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.filterTextBox.AutoCompleteMode = System.Windows.Forms.AutoCompleteMode.SuggestAppend;
      this.filterTextBox.AutoCompleteSource = System.Windows.Forms.AutoCompleteSource.CustomSource;
      this.filterTextBox.Location = new System.Drawing.Point(12, 569);
      this.filterTextBox.Name = "filterTextBox";
      this.filterTextBox.Size = new System.Drawing.Size(234, 20);
      this.filterTextBox.TabIndex = 1;
      // 
      // functionsListBox
      // 
      this.functionsListBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.functionsListBox.FormattingEnabled = true;
      this.functionsListBox.Location = new System.Drawing.Point(12, 39);
      this.functionsListBox.Name = "functionsListBox";
      this.functionsListBox.Size = new System.Drawing.Size(234, 524);
      this.functionsListBox.TabIndex = 2;
      // 
      // FunctionsPanel
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(258, 600);
      this.Controls.Add(this.functionsListBox);
      this.Controls.Add(this.filterTextBox);
      this.Controls.Add(this.modulesComboBox);
      this.Name = "FunctionsPanel";
      this.Text = "Functions";
      this.ResumeLayout(false);
      this.PerformLayout();

    }

    #endregion

    private System.Windows.Forms.ComboBox modulesComboBox;
    private System.Windows.Forms.TextBox filterTextBox;
    private System.Windows.Forms.ListBox functionsListBox;
  }
}