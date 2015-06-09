namespace Xenia.Debug.UI.Views {
  partial class ModulesPanel {
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
      System.Windows.Forms.ColumnHeader columnHeader1;
      System.Windows.Forms.ColumnHeader columnHeader2;
      System.Windows.Forms.ColumnHeader columnHeader3;
      System.Windows.Forms.ColumnHeader columnHeader4;
      this.modulesListView = new System.Windows.Forms.ListView();
      columnHeader1 = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
      columnHeader2 = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
      columnHeader3 = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
      columnHeader4 = ((System.Windows.Forms.ColumnHeader)(new System.Windows.Forms.ColumnHeader()));
      this.SuspendLayout();
      // 
      // modulesListView
      // 
      this.modulesListView.Anchor = ((System.Windows.Forms.AnchorStyles)((((System.Windows.Forms.AnchorStyles.Top | System.Windows.Forms.AnchorStyles.Bottom) 
            | System.Windows.Forms.AnchorStyles.Left) 
            | System.Windows.Forms.AnchorStyles.Right)));
      this.modulesListView.Columns.AddRange(new System.Windows.Forms.ColumnHeader[] {
            columnHeader1,
            columnHeader2,
            columnHeader3,
            columnHeader4});
      this.modulesListView.Location = new System.Drawing.Point(12, 12);
      this.modulesListView.Name = "modulesListView";
      this.modulesListView.Size = new System.Drawing.Size(748, 206);
      this.modulesListView.TabIndex = 0;
      this.modulesListView.UseCompatibleStateImageBehavior = false;
      this.modulesListView.View = System.Windows.Forms.View.Details;
      // 
      // columnHeader1
      // 
      columnHeader1.Text = "Handle";
      // 
      // columnHeader2
      // 
      columnHeader2.Text = "Type";
      columnHeader2.Width = 80;
      // 
      // columnHeader3
      // 
      columnHeader3.Text = "Name";
      columnHeader3.Width = 120;
      // 
      // columnHeader4
      // 
      columnHeader4.Text = "Path";
      columnHeader4.Width = 300;
      // 
      // ModulesPanel
      // 
      this.AutoScaleDimensions = new System.Drawing.SizeF(6F, 13F);
      this.AutoScaleMode = System.Windows.Forms.AutoScaleMode.Font;
      this.ClientSize = new System.Drawing.Size(772, 230);
      this.Controls.Add(this.modulesListView);
      this.Font = new System.Drawing.Font("Microsoft Sans Serif", 8.25F, System.Drawing.FontStyle.Regular, System.Drawing.GraphicsUnit.Point, ((byte)(0)));
      this.Name = "ModulesPanel";
      this.Text = "Modules";
      this.ResumeLayout(false);

    }

    #endregion

    private System.Windows.Forms.ListView modulesListView;
  }
}