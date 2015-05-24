namespace Xenia.Debug.UI.Views {
  partial class CodeDocument {
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
      this.sourceTextBox = new System.Windows.Forms.TextBox();
      this.SuspendLayout();
      // 
      // sourceTextBox
      // 
      this.sourceTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.sourceTextBox.Location = new System.Drawing.Point(12, 75);
      this.sourceTextBox.Multiline = true;
      this.sourceTextBox.Name = "sourceTextBox";
      this.sourceTextBox.Size = new System.Drawing.Size(740, 586);
      this.sourceTextBox.TabIndex = 0;
      // 
      // CodeDocument
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(764, 673);
      this.Controls.Add(this.sourceTextBox);
      this.Name = "CodeDocument";
      this.Text = "Code";
      this.ResumeLayout(false);
      this.PerformLayout();

    }

    #endregion

    private System.Windows.Forms.TextBox sourceTextBox;
  }
}