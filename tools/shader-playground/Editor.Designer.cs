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
      this.wordsTextBox = new System.Windows.Forms.TextBox();
      this.tableLayoutPanel1 = new System.Windows.Forms.TableLayoutPanel();
      this.compilerTranslatedTextBox = new System.Windows.Forms.TextBox();
      this.sourceCodeTextBox = new System.Windows.Forms.TextBox();
      this.outputTextBox = new System.Windows.Forms.TextBox();
      this.compilerUcodeTextBox = new System.Windows.Forms.TextBox();
      this.label1 = new System.Windows.Forms.Label();
      this.label2 = new System.Windows.Forms.Label();
      this.label3 = new System.Windows.Forms.Label();
      this.translationComboBox = new System.Windows.Forms.ComboBox();
      this.tableLayoutPanel1.SuspendLayout();
      this.SuspendLayout();
      // 
      // wordsTextBox
      // 
      this.wordsTextBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Bottom | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.wordsTextBox.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.wordsTextBox.Location = new System.Drawing.Point(12, 657);
      this.wordsTextBox.Multiline = true;
      this.wordsTextBox.Name = "wordsTextBox";
      this.wordsTextBox.ReadOnly = true;
      this.wordsTextBox.Size = new System.Drawing.Size(1631, 137);
      this.wordsTextBox.TabIndex = 4;
      // 
      // tableLayoutPanel1
      // 
      this.tableLayoutPanel1.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.tableLayoutPanel1.ColumnCount = 4;
      this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 25F));
      this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 25F));
      this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 25F));
      this.tableLayoutPanel1.ColumnStyles.Add(new System.Windows.Forms.ColumnStyle(System.Windows.Forms.SizeType.Percent, 25F));
      this.tableLayoutPanel1.Controls.Add(this.compilerTranslatedTextBox, 3, 1);
      this.tableLayoutPanel1.Controls.Add(this.sourceCodeTextBox, 0, 1);
      this.tableLayoutPanel1.Controls.Add(this.outputTextBox, 1, 1);
      this.tableLayoutPanel1.Controls.Add(this.compilerUcodeTextBox, 2, 1);
      this.tableLayoutPanel1.Controls.Add(this.label1, 0, 0);
      this.tableLayoutPanel1.Controls.Add(this.label2, 1, 0);
      this.tableLayoutPanel1.Controls.Add(this.label3, 2, 0);
      this.tableLayoutPanel1.Controls.Add(this.translationComboBox, 3, 0);
      this.tableLayoutPanel1.Location = new System.Drawing.Point(12, 12);
      this.tableLayoutPanel1.Name = "tableLayoutPanel1";
      this.tableLayoutPanel1.RowCount = 2;
      this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Absolute, 20F));
      this.tableLayoutPanel1.RowStyles.Add(new System.Windows.Forms.RowStyle(System.Windows.Forms.SizeType.Percent, 100F));
      this.tableLayoutPanel1.Size = new System.Drawing.Size(1631, 639);
      this.tableLayoutPanel1.TabIndex = 5;
      // 
      // compilerTranslatedTextBox
      // 
      this.compilerTranslatedTextBox.AcceptsReturn = true;
      this.compilerTranslatedTextBox.AcceptsTab = true;
      this.compilerTranslatedTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
      this.compilerTranslatedTextBox.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.compilerTranslatedTextBox.Location = new System.Drawing.Point(1224, 23);
      this.compilerTranslatedTextBox.Multiline = true;
      this.compilerTranslatedTextBox.Name = "compilerTranslatedTextBox";
      this.compilerTranslatedTextBox.ReadOnly = true;
      this.compilerTranslatedTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
      this.compilerTranslatedTextBox.Size = new System.Drawing.Size(404, 613);
      this.compilerTranslatedTextBox.TabIndex = 11;
      // 
      // sourceCodeTextBox
      // 
      this.sourceCodeTextBox.AcceptsReturn = true;
      this.sourceCodeTextBox.AcceptsTab = true;
      this.sourceCodeTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
      this.sourceCodeTextBox.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.sourceCodeTextBox.Location = new System.Drawing.Point(3, 23);
      this.sourceCodeTextBox.Multiline = true;
      this.sourceCodeTextBox.Name = "sourceCodeTextBox";
      this.sourceCodeTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
      this.sourceCodeTextBox.Size = new System.Drawing.Size(401, 613);
      this.sourceCodeTextBox.TabIndex = 6;
      // 
      // outputTextBox
      // 
      this.outputTextBox.AcceptsReturn = true;
      this.outputTextBox.AcceptsTab = true;
      this.outputTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
      this.outputTextBox.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.outputTextBox.Location = new System.Drawing.Point(410, 23);
      this.outputTextBox.Multiline = true;
      this.outputTextBox.Name = "outputTextBox";
      this.outputTextBox.ReadOnly = true;
      this.outputTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
      this.outputTextBox.Size = new System.Drawing.Size(401, 613);
      this.outputTextBox.TabIndex = 5;
      // 
      // compilerUcodeTextBox
      // 
      this.compilerUcodeTextBox.AcceptsReturn = true;
      this.compilerUcodeTextBox.AcceptsTab = true;
      this.compilerUcodeTextBox.Dock = System.Windows.Forms.DockStyle.Fill;
      this.compilerUcodeTextBox.Font = new System.Drawing.Font("Consolas", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.compilerUcodeTextBox.Location = new System.Drawing.Point(817, 23);
      this.compilerUcodeTextBox.Multiline = true;
      this.compilerUcodeTextBox.Name = "compilerUcodeTextBox";
      this.compilerUcodeTextBox.ReadOnly = true;
      this.compilerUcodeTextBox.ScrollBars = System.Windows.Forms.ScrollBars.Vertical;
      this.compilerUcodeTextBox.Size = new System.Drawing.Size(401, 613);
      this.compilerUcodeTextBox.TabIndex = 4;
      // 
      // label1
      // 
      this.label1.AutoSize = true;
      this.label1.Location = new System.Drawing.Point(3, 0);
      this.label1.Name = "label1";
      this.label1.Size = new System.Drawing.Size(78, 13);
      this.label1.TabIndex = 7;
      this.label1.Text = "Input Assembly";
      // 
      // label2
      // 
      this.label2.AutoSize = true;
      this.label2.Location = new System.Drawing.Point(410, 0);
      this.label2.Name = "label2";
      this.label2.Size = new System.Drawing.Size(107, 13);
      this.label2.TabIndex = 8;
      this.label2.Text = "XNA Compiler Output";
      // 
      // label3
      // 
      this.label3.AutoSize = true;
      this.label3.Location = new System.Drawing.Point(817, 0);
      this.label3.Name = "label3";
      this.label3.Size = new System.Drawing.Size(191, 13);
      this.label3.TabIndex = 9;
      this.label3.Text = "xenia-gpu-shader-compiler Disassembly";
      // 
      // translationComboBox
      // 
      this.translationComboBox.Anchor = ((System.Windows.Forms.AnchorStyles)(((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.translationComboBox.DropDownStyle = System.Windows.Forms.ComboBoxStyle.DropDownList;
      this.translationComboBox.FormattingEnabled = true;
      this.translationComboBox.Items.AddRange(
          new object[]{"GLSL for GL4.5", "SPIRV"});
      this.translationComboBox.Location = new System.Drawing.Point(1224, 0);
      this.translationComboBox.Margin = new System.Windows.Forms.Padding(3, 0, 3, 0);
      this.translationComboBox.Name = "translationComboBox";
      this.translationComboBox.Size = new System.Drawing.Size(404, 21);
      this.translationComboBox.TabIndex = 12;
      // 
      // Editor
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(1655, 806);
      this.Controls.Add(this.tableLayoutPanel1);
      this.Controls.Add(this.wordsTextBox);
      this.Name = "Editor";
      this.Text = "Shader Playground";
      this.tableLayoutPanel1.ResumeLayout(false);
      this.tableLayoutPanel1.PerformLayout();
      this.ResumeLayout(false);
      this.PerformLayout();

    }

    #endregion
    private System.Windows.Forms.TextBox wordsTextBox;
    private System.Windows.Forms.TableLayoutPanel tableLayoutPanel1;
    private System.Windows.Forms.TextBox sourceCodeTextBox;
    private System.Windows.Forms.TextBox outputTextBox;
    private System.Windows.Forms.TextBox compilerUcodeTextBox;
    private System.Windows.Forms.Label label1;
    private System.Windows.Forms.Label label2;
    private System.Windows.Forms.Label label3;
    private System.Windows.Forms.TextBox compilerTranslatedTextBox;
    private System.Windows.Forms.ComboBox translationComboBox;
  }
}

