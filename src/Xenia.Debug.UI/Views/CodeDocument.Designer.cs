using System;

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
      this.panel1 = new System.Windows.Forms.Panel();
      this.disasmMachineCodeCheckBox = new System.Windows.Forms.CheckBox();
      this.disasmOptimizedHirCheckBox = new System.Windows.Forms.CheckBox();
      this.disasmUnoptimizedHirCheckBox = new System.Windows.Forms.CheckBox();
      this.disasmPpcCheckBox = new System.Windows.Forms.CheckBox();
      this.panel1.SuspendLayout();
      this.SuspendLayout();
      // 
      // sourceTextBox
      // 
      this.sourceTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.sourceTextBox.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.sourceTextBox.HideSelection = false;
      this.sourceTextBox.Location = new System.Drawing.Point(12, 75);
      this.sourceTextBox.MaxLength = 99999;
      this.sourceTextBox.Multiline = true;
      this.sourceTextBox.Name = "sourceTextBox";
      this.sourceTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Both;
      this.sourceTextBox.Size = new System.Drawing.Size(740, 563);
      this.sourceTextBox.TabIndex = 0;
      // 
      // panel1
      // 
      this.panel1.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.panel1.Controls.Add(this.disasmMachineCodeCheckBox);
      this.panel1.Controls.Add(this.disasmOptimizedHirCheckBox);
      this.panel1.Controls.Add(this.disasmUnoptimizedHirCheckBox);
      this.panel1.Controls.Add(this.disasmPpcCheckBox);
      this.panel1.Location = new System.Drawing.Point(12, 644);
      this.panel1.Name = "panel1";
      this.panel1.Size = new System.Drawing.Size(740, 17);
      this.panel1.TabIndex = 1;
      // 
      // disasmMachineCodeCheckBox
      // 
      this.disasmMachineCodeCheckBox.AutoSize = true;
      this.disasmMachineCodeCheckBox.Checked = true;
      this.disasmMachineCodeCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
      this.disasmMachineCodeCheckBox.Location = new System.Drawing.Point(302, 0);
      this.disasmMachineCodeCheckBox.Name = "disasmMachineCodeCheckBox";
      this.disasmMachineCodeCheckBox.Size = new System.Drawing.Size(95, 17);
      this.disasmMachineCodeCheckBox.TabIndex = 3;
      this.disasmMachineCodeCheckBox.Text = "Machine Code";
      this.disasmMachineCodeCheckBox.UseVisualStyleBackColor = true;
      this.disasmMachineCodeCheckBox.CheckedChanged += new System.EventHandler(this.DisasmCheckBoxChanged);
      // 
      // disasmOptimizedHirCheckBox
      // 
      this.disasmOptimizedHirCheckBox.AutoSize = true;
      this.disasmOptimizedHirCheckBox.Checked = true;
      this.disasmOptimizedHirCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
      this.disasmOptimizedHirCheckBox.Location = new System.Drawing.Point(202, 0);
      this.disasmOptimizedHirCheckBox.Name = "disasmOptimizedHirCheckBox";
      this.disasmOptimizedHirCheckBox.Size = new System.Drawing.Size(94, 17);
      this.disasmOptimizedHirCheckBox.TabIndex = 2;
      this.disasmOptimizedHirCheckBox.Text = "Optimized HIR";
      this.disasmOptimizedHirCheckBox.UseVisualStyleBackColor = true;
      this.disasmOptimizedHirCheckBox.CheckedChanged += new System.EventHandler(this.DisasmCheckBoxChanged);
      // 
      // disasmUnoptimizedHirCheckBox
      // 
      this.disasmUnoptimizedHirCheckBox.AutoSize = true;
      this.disasmUnoptimizedHirCheckBox.Checked = true;
      this.disasmUnoptimizedHirCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
      this.disasmUnoptimizedHirCheckBox.Location = new System.Drawing.Point(90, 0);
      this.disasmUnoptimizedHirCheckBox.Name = "disasmUnoptimizedHirCheckBox";
      this.disasmUnoptimizedHirCheckBox.Size = new System.Drawing.Size(106, 17);
      this.disasmUnoptimizedHirCheckBox.TabIndex = 1;
      this.disasmUnoptimizedHirCheckBox.Text = "Unoptimized HIR";
      this.disasmUnoptimizedHirCheckBox.UseVisualStyleBackColor = true;
      this.disasmUnoptimizedHirCheckBox.CheckedChanged += new System.EventHandler(this.DisasmCheckBoxChanged);
      // 
      // disasmPpcCheckBox
      // 
      this.disasmPpcCheckBox.AutoSize = true;
      this.disasmPpcCheckBox.Checked = true;
      this.disasmPpcCheckBox.CheckState = System.Windows.Forms.CheckState.Checked;
      this.disasmPpcCheckBox.Location = new System.Drawing.Point(0, 0);
      this.disasmPpcCheckBox.Name = "disasmPpcCheckBox";
      this.disasmPpcCheckBox.Size = new System.Drawing.Size(84, 17);
      this.disasmPpcCheckBox.TabIndex = 0;
      this.disasmPpcCheckBox.Text = "Source PPC";
      this.disasmPpcCheckBox.UseVisualStyleBackColor = true;
      this.disasmPpcCheckBox.CheckedChanged += new System.EventHandler(this.DisasmCheckBoxChanged);
      // 
      // CodeDocument
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(764, 673);
      this.Controls.Add(this.panel1);
      this.Controls.Add(this.sourceTextBox);
      this.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.Name = "CodeDocument";
      this.Text = "Code";
      this.panel1.ResumeLayout(false);
      this.panel1.PerformLayout();
      this.ResumeLayout(false);
      this.PerformLayout();

    }

    #endregion

    private System.Windows.Forms.TextBox sourceTextBox;
    private System.Windows.Forms.Panel panel1;
    private System.Windows.Forms.CheckBox disasmMachineCodeCheckBox;
    private System.Windows.Forms.CheckBox disasmOptimizedHirCheckBox;
    private System.Windows.Forms.CheckBox disasmUnoptimizedHirCheckBox;
    private System.Windows.Forms.CheckBox disasmPpcCheckBox;
  }
}